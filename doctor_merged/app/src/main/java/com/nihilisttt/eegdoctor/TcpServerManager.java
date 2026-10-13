package com.nihilisttt.eegdoctor;

import android.os.Handler;
import android.os.Looper;
import android.os.NetworkOnMainThreadException;
import android.util.Log;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.List;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicBoolean;

public class TcpServerManager {
    private static final int TCP_PORT = 41002;
    private static final int FORWARD_PORT = 41003;
    private static final int DOCTOR_TO_PATIENT_PORT = 41004;
    private static final int DISCOVERY_PORT = 41005;
    private static final long PATIENT_HEARTBEAT_INTERVAL_MS = 2000L;
    private static final long PATIENT_HEARTBEAT_TIMEOUT_MS = 7000L;
    private static final TcpServerManager INSTANCE = new TcpServerManager();

    private ServerSocket serverSocket;
    private boolean running;
    private DataDispatcher dispatcher = DataDispatcher.getInstance();

    private final UdpSender udpSender = new UdpSender();
    private ServerSocket forwardServerSocket;
    private TcpForwardManager forwardManager;


    private ServerSocket doctorToPatientServerSocket;
    private Socket deviceClient;
    private Socket doctorToPatientClient;
    private DatagramSocket discoverySocket;

    private final CopyOnWriteArrayList<ConnectionListener> connectionListeners = new CopyOnWriteArrayList<>();


    private final AtomicLong patientControlGenerationCounter = new AtomicLong(0);
    private volatile long currentPatientControlGeneration;
    private volatile long patientLastPongAt;
    private volatile boolean patientControlReady;
    private Thread patientControlHeartbeatThread;
    private final Object d2pWriteLock = new Object();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final ExecutorService patientControlWriter = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "doctor-patient-control-writer");
        thread.setDaemon(true);
        return thread;
    });

    public interface PatientSendCallback {
        void onResult(boolean success);
    }

    private volatile boolean ssvepActive = false;
    private volatile int ssvepFreqIndex = -1;
    private volatile int currentDoctorPage = 0;

    public void setSsvepActive(boolean active, int freqIndex) {
        this.ssvepActive = active;
        this.ssvepFreqIndex = freqIndex;
        Log.i("TCP", "setSsvepActive: " + active + " freqIndex=" + freqIndex);
    }

    public boolean isSsvepActive() { return ssvepActive; }
    public int getSsvepFreqIndex() { return ssvepFreqIndex; }

    private Thread senderThread;
    private final BlockingQueue<byte[]> sendQueue = new ArrayBlockingQueue<>(256);

    public interface ConnectionListener {
        void onDeviceConnected(boolean connected);
        void onPatientConnected(boolean connected);
    }

    public void addConnectionListener(ConnectionListener l) { connectionListeners.add(l); }
    public void removeConnectionListener(ConnectionListener l) { connectionListeners.remove(l); }

    public boolean isDeviceConnected() {
        return deviceClient != null && !deviceClient.isClosed() && deviceClient.isConnected();
    }

    /**
     * 患者端是否已具备可用的医生->患者控制通道。

     */
    public boolean isPatientConnected() {
        return isPatientControlConnected();
    }

    public boolean isPatientControlConnected() {
        Socket client;
        boolean ready;
        long lastPong;
        synchronized (this) {
            client = doctorToPatientClient;
            ready = patientControlReady;
            lastPong = patientLastPongAt;
        }
        return client != null
                && !client.isClosed()
                && client.isConnected()
                && ready
                && System.currentTimeMillis() - lastPong <= PATIENT_HEARTBEAT_TIMEOUT_MS;
    }


    public static TcpServerManager getInstance() { return INSTANCE; }

    // ========== 新增UDP配置接口 ==========
    /** 设置Python上位机IP，开启UDP发送 */
    public void configUdpForPython(String pythonIp) {
        udpSender.setTargetIp(pythonIp);
        udpSender.setEnableUdp(true);
        Log.i("UDP", "UDP已启用，目标IP：" + pythonIp);
    }

    /** 关闭UDP发送 */
    public void disableUdp() {
        udpSender.setEnableUdp(false);
        Log.i("UDP", "UDP已关闭");
    }

    public void start() {
        if (running) return;
        running = true;
        // 启动原有主服务器
        new Thread(() -> {
            try {
                serverSocket = new ServerSocket(TCP_PORT);
                Log.i("TCP", "Main server started on port " + TCP_PORT);
                while (running) {
                    Socket client = serverSocket.accept();
                    Log.i("TCP", "Main client connected: " + client.getRemoteSocketAddress());
                    new Thread(() -> handleClient(client)).start();
                }
            } catch (Exception e) { Log.e("TCP", "Main server error", e); }
        }).start();

        // 启动转发服务器
        forwardManager = new TcpForwardManager();
        new Thread(() -> {
            try {
                forwardServerSocket = new ServerSocket(FORWARD_PORT);
                Log.i("TCP", "Forward server started on port " + FORWARD_PORT);
                while (running) {
                    Socket forwardClient = forwardServerSocket.accept();
                    Log.i("TCP", "Forward client connected: " + forwardClient.getRemoteSocketAddress());
                    forwardManager.addClient(forwardClient);
                }
            } catch (Exception e) { Log.e("TCP", "Forward server error", e); }
        }).start();



        // 启动医生端→患者端专用控制通道服务器。
        // 患者端连接后必须先发送 PATIENT_READY，医生端回 CONTROL_READY，
        // 双向握手完成后才将控制通道标记为已连接。
        new Thread(() -> {
            try {
                doctorToPatientServerSocket = new ServerSocket(DOCTOR_TO_PATIENT_PORT);
                Log.i("TCP", "Doctor-to-patient cmd server started on port " + DOCTOR_TO_PATIENT_PORT);
                while (running) {
                    Socket client = doctorToPatientServerSocket.accept();
                    client.setKeepAlive(true);
                    client.setTcpNoDelay(true);
                    final long generation = patientControlGenerationCounter.incrementAndGet();

                    Socket oldClient;
                    boolean oldWasReady;
                    synchronized (TcpServerManager.this) {
                        oldClient = doctorToPatientClient;
                        oldWasReady = patientControlReady;
                        doctorToPatientClient = client;
                        currentPatientControlGeneration = generation;
                        patientControlReady = false;
                        patientLastPongAt = System.currentTimeMillis();
                    }
                    if (oldClient != null && oldClient != client) {
                        try { oldClient.close(); } catch (Exception ignored) {}
                    }
                    if (oldWasReady) notifyPatientConnected(false);

                    Log.i("DOCTOR", ">>> Patient D2P TCP connected: "
                            + client.getRemoteSocketAddress() + ", generation=" + generation);

                    new Thread(() -> handlePatientControlClient(client, generation),
                            "doctor-d2p-reader-" + generation).start();
                }
            } catch (Exception e) {
                if (running) Log.e("TCP", "Doctor-to-patient server error", e);
            }
        }, "doctor-d2p-server").start();

        startPatientControlHeartbeat();

        // 启动UDP发现响应服务
        new Thread(() -> {
            try {
                discoverySocket = new DatagramSocket(DISCOVERY_PORT);
                discoverySocket.setBroadcast(true);
                Log.i("UDP", "Discovery server started on port " + DISCOVERY_PORT);
                byte[] buf = new byte[256];
                while (running) {
                    DatagramPacket packet = new DatagramPacket(buf, buf.length);
                    discoverySocket.receive(packet);
                    String msg = new String(packet.getData(), 0, packet.getLength()).trim();
                    if ("EEG_DOCTOR_DISCOVERY".equals(msg)) {
                        String localIp = getLocalHotspotIp();
                        String response = "EEG_DOCTOR:" + localIp;
                        byte[] respBytes = response.getBytes("UTF-8");
                        DatagramPacket respPacket = new DatagramPacket(
                                respBytes, respBytes.length,
                                packet.getAddress(), packet.getPort());
                        discoverySocket.send(respPacket);
                        Log.i("UDP", "Discovery response sent to " + packet.getAddress().getHostAddress());
                    }
                }
            } catch (Exception e) {
                if (running) Log.e("UDP", "Discovery server error", e);
            }
        }).start();
    }

    public void stop() {
        running = false;
        try { if (serverSocket != null) serverSocket.close(); } catch (Exception ignored) {}
        try { if (forwardServerSocket != null) forwardServerSocket.close(); } catch (Exception ignored) {}

        try { if (doctorToPatientServerSocket != null) doctorToPatientServerSocket.close(); } catch (Exception ignored) {}
        try { if (discoverySocket != null) discoverySocket.close(); } catch (Exception ignored) {}
        try { if (deviceClient != null) deviceClient.close(); } catch (Exception ignored) {}
        try { if (doctorToPatientClient != null) doctorToPatientClient.close(); } catch (Exception ignored) {}
        deviceClient = null;
        synchronized (this) {
            doctorToPatientClient = null;
            patientControlReady = false;
            currentPatientControlGeneration = patientControlGenerationCounter.incrementAndGet();
        }
        stopPatientControlHeartbeat();
        if (forwardManager != null) forwardManager.stopAll();

        udpSender.release();
        stopHeartbeat();
    }

    private void startHeartbeat() {
        startSender();
    }

    private void stopHeartbeat() {
        stopSender();
    }

    private void startSender() {
        stopSender();
        senderThread = new Thread(() -> {
            Log.i("TCP_SENDER", "Sender thread started");
            while (!Thread.currentThread().isInterrupted() && running) {
                try {
                    byte[] data = sendQueue.take();
                    Socket client = deviceClient;
                    if (client != null && !client.isClosed() && client.isConnected()) {
                        OutputStream os = client.getOutputStream();
                        os.write(data);
                        os.flush();
                        Log.i("TCP_SENDER", "Sent " + data.length + " bytes to " + client.getRemoteSocketAddress());
                    } else {
                        Log.w("TCP_SENDER", "Device not connected, dropping " + data.length + " bytes");
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                } catch (Exception e) {
                    Log.e("TCP_SENDER", "Send failed", e);
                }
            }
            Log.i("TCP_SENDER", "Sender thread stopped");
        });
        senderThread.setDaemon(true);
        senderThread.start();
    }

    private void stopSender() {
        if (senderThread != null) {
            senderThread.interrupt();
            senderThread = null;
        }
        sendQueue.clear();
    }




    private void notifyDeviceConnected(boolean connected) {
        for (ConnectionListener l : connectionListeners) {
            try { l.onDeviceConnected(connected); } catch (Exception ignored) {}
        }
    }

    private void notifyPatientConnected(boolean connected) {
        for (ConnectionListener l : connectionListeners) {
            try { l.onPatientConnected(connected); } catch (Exception ignored) {}
        }
    }

    /**
     * 获取本机热点 IP（通常为 192.168.43.1 或类似）
     */
    public String getLocalHotspotIp() {
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            for (NetworkInterface intf : Collections.list(interfaces)) {
                if (intf.getName().contains("wlan") || intf.getName().contains("ap")) {
                    Enumeration<InetAddress> addresses = intf.getInetAddresses();
                    while (addresses.hasMoreElements()) {
                        InetAddress addr = addresses.nextElement();
                        if (!addr.isLoopbackAddress() && addr.getAddress().length == 4) {
                            String ip = addr.getHostAddress();
                            if (ip.startsWith("192.168.")) {
                                return ip;
                            }
                        }
                    }
                }
            }
        } catch (SocketException e) {
            Log.e("TCP", "Error getting hotspot IP", e);
        }
        return "192.168.43.1"; // 默认回退
    }

    public int getForwardPort() {
        return FORWARD_PORT;
    }



    /**
     * 兼容旧文本命令入口：将文本命令映射为二进制帧后发送到设备。
     * 协议已全链路二进制化，这里仅作过渡映射。
     */
    public void sendToDevice(String command) {
        byte[] payload = deviceTextToPayload(command);
        if (payload == null) {
            Log.w("TCP", "Unsupported text command to device: [" + command + "]");
            return;
        }
        sendBinaryPayloadToDevice(payload);
    }

    /** 直接发送二进制命令帧到设备（41002）。payload = [cmd][data...] */
    public void sendBinaryToDevice(int cmd, byte[] data) {
        if (deviceClient == null || deviceClient.isClosed() || !deviceClient.isConnected()) {
            Log.w("TCP", "Cannot send to device (not connected): cmd=0x"
                    + Integer.toHexString(cmd));
            return;
        }
        int dLen = (data == null) ? 0 : data.length;
        byte[] payload = new byte[dLen + 1];
        payload[0] = (byte) cmd;
        if (dLen > 0) {
            System.arraycopy(data, 0, payload, 1, dLen);
        }
        byte[] frame = EegProtocol.packFrame(EegProtocol.ADDR_DOCTOR, cmd, payload);
        sendQueue.offer(frame);
        Log.i("TCP", "Queued binary frame: cmd=0x" + Integer.toHexString(cmd) + " len=" + dLen);
    }

    private void sendBinaryPayloadToDevice(byte[] payload) {
        if (payload == null || payload.length == 0) return;
        int cmd = payload[0] & 0xFF;
        byte[] data = new byte[payload.length - 1];
        System.arraycopy(payload, 1, data, 0, data.length);
        sendBinaryToDevice(cmd, data);
    }

    /** 向设备回复二进制 ACK 帧（seq 为上行命令序列号）。 */
    private void sendAckBinary(int seq) {
        sendBinaryToDevice(EegProtocol.CMD_ACK, new byte[]{(byte) seq});
    }

    /** 把医生端旧文本命令解析为下行二进制 payload（[cmd][data...]）。 */
    private static byte[] deviceTextToPayload(String command) {
        String cmd = command == null ? "" : command.trim();
        if (cmd.startsWith("DISPLAY_CFG,")) {
            String[] parts = cmd.split(",");
            if (parts.length == 13) {
                byte[] payload = new byte[13];
                payload[0] = (byte) EegProtocol.CMD_DISPLAY_CFG;
                for (int i = 1; i < 13; i++) {
                    try {
                        payload[i] = (byte) Integer.parseInt(parts[i].trim());
                    } catch (NumberFormatException e) {
                        return null;
                    }
                }
                return payload;
            }
            return null;
        }
        if (cmd.startsWith("MODE,SET,")) {
            try {
                int mode = Integer.parseInt(cmd.substring("MODE,SET,".length()).trim());
                return new byte[]{ (byte) EegProtocol.CMD_MODE_SET, (byte) mode };
            } catch (NumberFormatException e) {
                return null;
            }
        }
        if (cmd.equals("MODE,TRAIN")) return new byte[]{ (byte) EegProtocol.CMD_MODE_TRAIN };
        if (cmd.equals("MODE,TEST")) return new byte[]{ (byte) EegProtocol.CMD_MODE_TEST };
        if (cmd.equals("TRIAL,LEFT")) return new byte[]{ (byte) EegProtocol.CMD_TRIAL, 0 };
        if (cmd.equals("TRIAL,RIGHT")) return new byte[]{ (byte) EegProtocol.CMD_TRIAL, 1 };
        if (cmd.equals("STOP")) return new byte[]{ (byte) EegProtocol.CMD_STOP };
        if (cmd.equals("SSVEP,START")) return new byte[]{ (byte) EegProtocol.CMD_SSVEP_START };
        if (cmd.equals("SSVEP,STOP")) return new byte[]{ (byte) EegProtocol.CMD_SSVEP_STOP };
        if (cmd.startsWith("SSVEP,SELFTEST,START,")) {
            try {
                int idx = Integer.parseInt(cmd.substring("SSVEP,SELFTEST,START,".length()).trim());
                return new byte[]{ (byte) EegProtocol.CMD_SSVEP_SELFTEST_START, (byte) idx };
            } catch (NumberFormatException e) {
                return null;
            }
        }
        if (cmd.equals("SSVEP,SELFTEST,STOP")) return new byte[]{ (byte) EegProtocol.CMD_SSVEP_SELFTEST_STOP };
        return null;
    }

    /** 把医生端旧文本命令解析为 41004 下行二进制 payload（[cmd][data...]）。 */
    private static byte[] patientTextToPayload(String command) {
        String cmd = command == null ? "" : command.trim();
        if (cmd.equals("CONTROL_READY")) return new byte[]{ (byte) EegProtocol.CMD_CONTROL_READY };
        if (cmd.equals("PING")) return new byte[]{ (byte) EegProtocol.CMD_PING };
        if (cmd.equals("READY_TRAIN")) return new byte[]{ (byte) EegProtocol.CMD_READY_TRAIN };
        if (cmd.equals("READY_TEST")) return new byte[]{ (byte) EegProtocol.CMD_READY_TEST };
        if (cmd.equals("TASK,DONE")) return new byte[]{ (byte) EegProtocol.CMD_TASK_DONE };
        if (cmd.equals("TASK,STOPPED")) return new byte[]{ (byte) EegProtocol.CMD_TASK_STOPPED };
        if (cmd.equals("TRAIN_STOP")) return new byte[]{ (byte) EegProtocol.CMD_TRAIN_STOP };
        if (cmd.equals("SSVEP,STOP")) return new byte[]{ (byte) EegProtocol.CMD_SSVEP_STOP_P };
        if (cmd.startsWith("PAGE,")) {
            try {
                int p = Integer.parseInt(cmd.substring("PAGE,".length()).trim());
                return new byte[]{ (byte) EegProtocol.CMD_PAGE, (byte) p };
            } catch (NumberFormatException e) {
                return null;
            }
        }
        if (cmd.startsWith("SSVEP,START")) {
            String[] parts = cmd.split(",");
            int idx = 0;
            if (parts.length >= 3) {
                try {
                    idx = Integer.parseInt(parts[2].trim());
                } catch (NumberFormatException e) {
                    return null;
                }
            }
            return new byte[]{ (byte) EegProtocol.CMD_SSVEP_START_P, (byte) idx };
        }
        if (cmd.startsWith("TARGET,")) {
            String dir = cmd.substring("TARGET,".length()).trim();
            return new byte[]{ (byte) EegProtocol.CMD_TARGET,
                    "LEFT".equals(dir) ? (byte) 0 : (byte) 1 };
        }
        if (cmd.startsWith("MODE_SET_OK,")) {
            try {
                int m = Integer.parseInt(cmd.substring("MODE_SET_OK,".length()).trim());
                return new byte[]{ (byte) EegProtocol.CMD_MODE_SET_OK, (byte) m };
            } catch (NumberFormatException e) {
                return null;
            }
        }
        if (cmd.startsWith("TASK,LEFT,start")) return new byte[]{ (byte) EegProtocol.CMD_TASK_START, 0 };
        if (cmd.startsWith("TASK,RIGHT,start")) return new byte[]{ (byte) EegProtocol.CMD_TASK_START, 1 };
        if (cmd.startsWith("SSVEP,RESULT,")) {
            SsvepResult r = SsvepResult.fromLine(cmd);
            if (r == null) return null;
            ByteBuffer bb = ByteBuffer.allocate(36).order(ByteOrder.LITTLE_ENDIAN);
            bb.putInt(r.getSeq());
            bb.put((byte) (r.getFreqIndex() >= 0 ? r.getFreqIndex() : 0xFF));
            bb.put((byte) (r.getRawFreqIndex() >= 0 ? r.getRawFreqIndex() : 0xFF));
            bb.putFloat(r.getRatio());
            bb.putFloat(r.getMargin());
            bb.putFloat(r.getScore11());
            bb.putFloat(r.getScore13());
            bb.putFloat(r.getScore15());
            bb.putFloat(r.getScore17());
            bb.put((byte) r.getVote11());
            bb.put((byte) r.getVote13());
            bb.put((byte) r.getVote15());
            bb.put((byte) r.getVote17());
            bb.put((byte) (r.isSynthetic() ? 1 : 0));
            bb.put((byte) (r.isChannelUsable() ? 1 : 0));
            byte[] data = bb.array();
            byte[] payload = new byte[data.length + 1];
            payload[0] = (byte) EegProtocol.CMD_SSVEP_RESULT;
            System.arraycopy(data, 0, payload, 1, data.length);
            return payload;
        }
        return null;
    }

    // ========== V2帧下行发送（Android→ESP8266，无CRC） ==========
    // 格式: 0x7E [VER=0x01] [CMD] [LEN_LO] [LEN_HI] [PAYLOAD] 0x7E
    private static final byte V2_FRAME_CHAR = 0x7E;
    private static final byte V2_ESCAPE_CHAR = 0x7D;
    private static final byte V2_ESCAPE_XOR = 0x20;

    public void sendV2FrameToDevice(byte cmd, byte[] payload) {
        if (deviceClient == null || deviceClient.isClosed() || !deviceClient.isConnected()) {
            Log.w("TCP", "Cannot send v2 frame (not connected)");
            return;
        }
        try {
            byte[] frame = packV2Frame(cmd, payload);
            sendQueue.offer(frame);
            Log.i("TCP", "Queued v2 frame: cmd=0x" + String.format("%02X", cmd) + " len=" + payload.length);
        } catch (Exception e) {
            Log.e("TCP", "Failed to queue v2 frame", e);
        }
    }

    private static byte[] packV2Frame(byte cmd, byte[] payload) {
        int payLen = payload.length;
        byte[] buf = new byte[2 + 4 * 2 + payLen * 2 + 2];
        int idx = 0;
        buf[idx++] = V2_FRAME_CHAR;
        idx = v2EscapeWrite(buf, idx, (byte) 0x01);
        idx = v2EscapeWrite(buf, idx, cmd);
        idx = v2EscapeWrite(buf, idx, (byte) (payLen & 0xFF));
        idx = v2EscapeWrite(buf, idx, (byte) ((payLen >> 8) & 0xFF));
        for (byte b : payload) idx = v2EscapeWrite(buf, idx, b);
        buf[idx++] = V2_FRAME_CHAR;
        byte[] result = new byte[idx];
        System.arraycopy(buf, 0, result, 0, idx);
        return result;
    }

    private static int v2EscapeWrite(byte[] buf, int idx, byte b) {
        if ((b & 0xFF) == (V2_FRAME_CHAR & 0xFF) || (b & 0xFF) == (V2_ESCAPE_CHAR & 0xFF)) {
            buf[idx++] = V2_ESCAPE_CHAR;
            buf[idx++] = (byte) ((b & 0xFF) ^ (V2_ESCAPE_XOR & 0xFF));
        } else {
            buf[idx++] = b;
        }
        return idx;
    }

    public void sendWifiAdd(String ssid, String password) {
        byte[] ssidBytes = ssid.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] pwdBytes = password.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] payload = new byte[2 + ssidBytes.length + 1 + pwdBytes.length + 1];
        int off = 0;
        payload[off++] = (byte) 0xF0;
        payload[off++] = 0x01;
        System.arraycopy(ssidBytes, 0, payload, off, ssidBytes.length); off += ssidBytes.length;
        payload[off++] = 0;
        System.arraycopy(pwdBytes, 0, payload, off, pwdBytes.length); off += pwdBytes.length;
        payload[off++] = 0;
        byte[] trimmed = new byte[off];
        System.arraycopy(payload, 0, trimmed, 0, off);
        sendV2FrameToDevice((byte) 0xF0, trimmed);
    }

    public void sendWifiDelete(String ssid) {
        byte[] ssidBytes = ssid.getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] payload = new byte[2 + ssidBytes.length + 1];
        int off = 0;
        payload[off++] = (byte) 0xF0;
        payload[off++] = 0x02;
        System.arraycopy(ssidBytes, 0, payload, off, ssidBytes.length); off += ssidBytes.length;
        payload[off++] = 0;
        byte[] trimmed = new byte[off];
        System.arraycopy(payload, 0, trimmed, 0, off);
        sendV2FrameToDevice((byte) 0xF0, trimmed);
    }

    /**
     * 页面和 SSVEP 控制命令只允许走 41004。

     *
     * 该方法只负责将命令加入专用后台发送队列，不在调用线程执行网络 I/O。
     * 因此可以安全地从 Activity/Fragment 的主线程调用。
     *
     * @return true 表示命令已进入发送队列；false 表示命令无效或队列不可用。
     */
    public boolean sendToPatient(String command) {
        if (command == null || command.trim().isEmpty()) {
            postPatientSendResult(null, false);
            return false;
        }
        final byte[] payload = patientTextToPayload(command);
        if (payload == null) {
            Log.w("DOCTOR", ">>> D2P UNSUPPORTED CMD: [" + command + "]");
            postPatientSendResult(null, false);
            return false;
        }
        return enqueuePatientPayload(payload, null);
    }

    /**
     * 异步发送患者控制命令，并在主线程回调实际写入结果。
     * 控制连接短暂重连时，后台任务会在 3 秒内等待新连接并重试。
     */
    public boolean sendToPatientAsync(String command, PatientSendCallback callback) {
        if (command == null || command.trim().isEmpty()) {
            postPatientSendResult(callback, false);
            return false;
        }
        final byte[] payload = patientTextToPayload(command);
        if (payload == null) {
            Log.w("DOCTOR", ">>> D2P UNSUPPORTED CMD: [" + command + "]");
            postPatientSendResult(callback, false);
            return false;
        }
        return enqueuePatientPayload(payload, callback);
    }

    /** 直接向患者端发送二进制帧（[cmd][data]），带状态快照与排队重试。 */
    public boolean sendBinaryToPatient(int cmd, byte[] data) {
        return enqueuePatientPayload(buildPatientPayload(cmd, data), null);
    }

    /** 直接异步向患者端发送二进制帧，成功后回调主线程。 */
    public boolean sendBinaryToPatientAsync(int cmd, byte[] data, PatientSendCallback callback) {
        return enqueuePatientPayload(buildPatientPayload(cmd, data), callback);
    }

    private android.content.Context mAppContext;

    public void setAppContext(android.content.Context ctx) {
        mAppContext = ctx.getApplicationContext();
    }

    public boolean isGlxssOutput() {
        return mAppContext != null && SettingsStore.isGlxssOutput(mAppContext);
    }

    private static final java.util.Set<Integer> GLXSS_DISPLAY_CMDS = java.util.Collections.unmodifiableSet(
        new java.util.HashSet<>(java.util.Arrays.asList(
            EegProtocol.CMD_SSVEP_START_P,
            EegProtocol.CMD_SSVEP_STOP_P,
            EegProtocol.CMD_TARGET,
            EegProtocol.CMD_TRAIN_STOP,
            EegProtocol.CMD_PAGE,
            EegProtocol.CMD_READY_TRAIN,
            EegProtocol.CMD_READY_TEST,
            EegProtocol.CMD_TASK_START,
            EegProtocol.CMD_TASK_DONE,
            EegProtocol.CMD_TASK_STOPPED,
            EegProtocol.CMD_MODE_SET_OK,
            EegProtocol.CMD_RESULT_MI,
            EegProtocol.CMD_SSVEP_RESULT
        )));

    public void sendDisplayToOutput(int cmd, byte[] data) {
        if (isGlxssOutput() && GLXSS_DISPLAY_CMDS.contains(cmd)) {
            if (cmd == EegProtocol.CMD_TARGET) {
                sendBinaryToDevice(cmd, data);
            }
            return;
        }
        sendBinaryToPatient(cmd, data);
    }

    public boolean sendDisplayToOutputAsync(int cmd, byte[] data, PatientSendCallback callback) {
        if (isGlxssOutput() && GLXSS_DISPLAY_CMDS.contains(cmd)) {
            if (callback != null) {
                new Handler(Looper.getMainLooper()).post(() -> callback.onResult(true));
            }
            return true;
        }
        return sendBinaryToPatientAsync(cmd, data, callback);
    }

    private static byte[] buildPatientPayload(int cmd, byte[] data) {
        int dLen = (data == null) ? 0 : data.length;
        byte[] payload = new byte[dLen + 1];
        payload[0] = (byte) cmd;
        if (dLen > 0) {
            System.arraycopy(data, 0, payload, 1, dLen);
        }
        return payload;
    }

    private boolean enqueuePatientPayload(byte[] payload, PatientSendCallback callback) {
        if (payload == null || payload.length < 1) {
            postPatientSendResult(callback, false);
            return false;
        }

        // 即使当前控制通道暂未连接，也保存期望状态。患者重连后的状态快照会恢复页面/SSVEP状态。
        updatePatientControlState(payload);

        // 未完成握手时不把命令堆积在队列中；期望状态已经保存，重连后的状态快照会自动同步。
        if (!hasReadyPatientControlSocket()) {
            Log.w("DOCTOR", ">>> D2P NOT ENQUEUED (control not ready): cmd=0x"
                    + Integer.toHexString(payload[0] & 0xFF));
            postPatientSendResult(callback, false);
            return false;
        }

        Log.i("DOCTOR", ">>> D2P ENQUEUE BIN: cmd=0x" + Integer.toHexString(payload[0] & 0xFF)
                + " len=" + (payload.length - 1));
        try {
            patientControlWriter.execute(() -> {
                boolean success = sendQueuedPatientCommand(payload, 3000L);
                postPatientSendResult(callback, success);
            });
            return true;
        } catch (RejectedExecutionException e) {
            Log.e("DOCTOR", ">>> D2P QUEUE REJECTED: cmd=0x"
                    + Integer.toHexString(payload[0] & 0xFF), e);
            postPatientSendResult(callback, false);
            return false;
        }
    }

    private boolean hasReadyPatientControlSocket() {
        synchronized (this) {
            return doctorToPatientClient != null
                    && !doctorToPatientClient.isClosed()
                    && doctorToPatientClient.isConnected()
                    && patientControlReady;
        }
    }

    private void postPatientSendResult(PatientSendCallback callback, boolean success) {
        if (callback != null) {
            mainHandler.post(() -> callback.onResult(success));
        }
    }

    /**
     * 只在 doctor-patient-control-writer 后台线程执行。
     * 若连接正处于重连阶段，等待当前 generation 的可用控制通道，而不是立即判定失败。
     */
    private boolean sendQueuedPatientCommand(byte[] payload, long waitTimeoutMs) {
        long deadline = System.currentTimeMillis() + Math.max(0L, waitTimeoutMs);
        int cmd = payload[0] & 0xFF;
        byte[] data = new byte[payload.length - 1];
        System.arraycopy(payload, 1, data, 0, data.length);

        while (running && !Thread.currentThread().isInterrupted()) {
            Socket client;
            long generation;
            boolean ready;
            synchronized (this) {
                client = doctorToPatientClient;
                generation = currentPatientControlGeneration;
                ready = patientControlReady;
            }

            if (client != null && !client.isClosed() && client.isConnected() && ready) {
                if (sendBinaryToCurrentPatient(client, generation, cmd, data, true)) {
                    return true;
                }
                // 写失败时 sendBinaryToCurrentPatient 会清理当前连接；短暂等待患者端自动重连。
            }

            if (System.currentTimeMillis() >= deadline) {
                Log.w("DOCTOR", ">>> D2P SEND TIMEOUT: cmd=0x" + Integer.toHexString(cmd));
                return false;
            }

            try {
                Thread.sleep(100L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return false;
    }

    private boolean isCurrentPatientControl(Socket client, long generation) {
        synchronized (this) {
            return doctorToPatientClient == client
                    && currentPatientControlGeneration == generation;
        }
    }

    /** 从任意线程直接向已握手的患者端发送二进制帧（无重试）。 */
    private boolean sendBinaryToPatientNow(int cmd, byte[] data) {
        Socket client;
        long generation;
        boolean ready;
        synchronized (this) {
            client = doctorToPatientClient;
            generation = currentPatientControlGeneration;
            ready = patientControlReady;
        }
        if (client == null || client.isClosed() || !client.isConnected() || !ready) {
            return false;
        }
        return sendBinaryToCurrentPatient(client, generation, cmd, data, true);
    }

    private void sendDisplayToOutputNow(int cmd, byte[] data) {
        if (isGlxssOutput() && GLXSS_DISPLAY_CMDS.contains(cmd)) {
            return;
        }
        sendBinaryToPatientNow(cmd, data);
    }

    private boolean sendBinaryToCurrentPatient(Socket client,
                                               long generation,
                                               int cmd,
                                               byte[] data,
                                               boolean closeOnFailure) {
        int dLen = (data == null) ? 0 : data.length;
        byte[] payload = new byte[dLen + 1];
        payload[0] = (byte) cmd;
        if (dLen > 0) {
            System.arraycopy(data, 0, payload, 1, dLen);
        }
        byte[] frame = EegProtocol.packFrame(EegProtocol.ADDR_DOCTOR, cmd, payload);
        synchronized (d2pWriteLock) {
            if (!isCurrentPatientControl(client, generation)
                    || client == null
                    || client.isClosed()) {
                return false;
            }
            try {
                OutputStream os = client.getOutputStream();
                os.write(frame);
                os.flush();
                Log.i("DOCTOR", ">>> D2P OK generation=" + generation + ": cmd=0x"
                        + Integer.toHexString(cmd) + " len=" + dLen);
                return true;
            } catch (NetworkOnMainThreadException e) {
                // 编程线程错误不等于 Socket 失效，禁止因此主动关闭一条正常控制连接。
                Log.e("DOCTOR", ">>> D2P PROGRAMMING ERROR: network write on main thread: cmd=0x"
                        + Integer.toHexString(cmd), e);
                return false;
            } catch (IOException e) {
                Log.e("DOCTOR", ">>> D2P NETWORK FAIL generation=" + generation + ": cmd=0x"
                        + Integer.toHexString(cmd) + " "
                        + e.getClass().getSimpleName() + ": " + e.getMessage());
                if (closeOnFailure) {
                    closeCurrentPatientControl(client, generation, "network write failed");
                }
                return false;
            } catch (RuntimeException e) {
                Log.e("DOCTOR", ">>> D2P RUNTIME FAIL generation=" + generation + ": cmd=0x"
                        + Integer.toHexString(cmd), e);
                return false;
            }
        }
    }

    private void handlePatientControlClient(Socket client, long generation) {
        try {
            InputStream in = client.getInputStream();
            byte[] buf = new byte[512];
            PatientFrameDecoder decoder = new PatientFrameDecoder();
            int n;
            while (running
                    && isCurrentPatientControl(client, generation)
                    && (n = in.read(buf)) != -1) {
                for (int i = 0; i < n; i++) {
                    decoder.parse(buf[i] & 0xFF, client, generation);
                }
            }
            Log.w("DOCTOR", ">>> Patient D2P reader ended generation=" + generation);
        } catch (IOException e) {
            if (running && isCurrentPatientControl(client, generation)) {
                Log.w("DOCTOR", ">>> Patient D2P read error generation=" + generation
                        + ": " + e.getMessage());
            }
        } finally {
            closeCurrentPatientControl(client, generation, "reader ended");
        }
    }

    /** 41004 医生→患者控制通道的二进制帧解码器。 */
    private class PatientFrameDecoder {
        private static final int STATE_WAIT_AA = 0;
        private static final int STATE_WAIT_55 = 1;
        private static final int STATE_BODY = 2;
        private static final int STATE_ESCAPE = 3;

        private int state = STATE_WAIT_AA;
        private final byte[] body = new byte[256];
        private int bodyLen = 0;

        void parse(int rawByte, Socket client, long generation) {
            switch (state) {
                case STATE_WAIT_AA:
                    if (rawByte == 0xAA) state = STATE_WAIT_55;
                    break;
                case STATE_WAIT_55:
                    if (rawByte == 0x55) {
                        state = STATE_BODY;
                        bodyLen = 0;
                    } else if (rawByte == 0xAA) {
                        state = STATE_WAIT_55;
                    } else {
                        state = STATE_WAIT_AA;
                    }
                    break;
                case STATE_BODY:
                    if (rawByte == 0x7D) {
                        state = STATE_ESCAPE;
                    } else if (rawByte == 0x7E) {
                        int minBody = EegProtocol.ADDR_LEN + EegProtocol.CMD_LEN
                                + EegProtocol.LEN_LEN + EegProtocol.TS_LEN + EegProtocol.CRC_LEN;
                        if (bodyLen >= minBody) {
                            dispatch(client, generation);
                        }
                        state = STATE_WAIT_AA;
                        bodyLen = 0;
                    } else {
                        if (bodyLen < body.length) {
                            body[bodyLen++] = (byte) rawByte;
                        } else {
                            state = STATE_WAIT_AA;
                        }
                    }
                    break;
                case STATE_ESCAPE:
                    if (bodyLen < body.length) {
                        body[bodyLen++] = (byte) (rawByte ^ EegProtocol.ESCAPE_XOR);
                    }
                    state = STATE_BODY;
                    break;
            }
        }

        private void dispatch(Socket client, long generation) {
            int cmd = body[1] & 0xFF;
            int payloadLen = EegProtocol.readU16LE(body, 2);
            int fixed = EegProtocol.ADDR_LEN + EegProtocol.CMD_LEN
                    + EegProtocol.LEN_LEN + EegProtocol.TS_LEN;
            if (bodyLen != fixed + payloadLen + EegProtocol.CRC_LEN) return;
            int calc = EegProtocol.checksum16(body, 0, fixed + payloadLen);
            int receivedCrc = EegProtocol.readU16LE(body, fixed + payloadLen);
            if (calc != receivedCrc) return;
            handlePatientFrame(cmd, payloadLen, fixed, client, generation);
        }

        private void handlePatientFrame(int cmd, int payloadLen, int off,
                                        Socket client, long generation) {
            switch (cmd) {
                case EegProtocol.CMD_PATIENT_READY: {
                    boolean becameReady = false;
                    synchronized (TcpServerManager.this) {
                        if (doctorToPatientClient == client
                                && currentPatientControlGeneration == generation) {
                            patientLastPongAt = System.currentTimeMillis();
                            if (!patientControlReady) {
                                patientControlReady = true;
                                becameReady = true;
                            }
                        }
                    }
                    if (becameReady) {
                        if (sendBinaryToCurrentPatient(client, generation,
                                EegProtocol.CMD_CONTROL_READY, null, true)
                                && sendPatientStateSnapshot(client, generation)) {
                            notifyPatientConnected(true);
                        }
                    }
                    break;
                }
                case EegProtocol.CMD_PONG:
                    synchronized (TcpServerManager.this) {
                        if (doctorToPatientClient == client
                                && currentPatientControlGeneration == generation) {
                            patientLastPongAt = System.currentTimeMillis();
                        }
                    }
                    break;
                case EegProtocol.CMD_SSVEP_STIM_STARTED:
                    if (payloadLen >= 5) {
                        int freqIndex = body[off] & 0xFF;
                        ByteBuffer bb = ByteBuffer.wrap(body, off + 1, 4)
                                .order(ByteOrder.LITTLE_ENDIAN);
                        float refreshRate = bb.getFloat();
                        SsvepAnalysisManager.getInstance()
                                .confirmStimulusStarted(freqIndex, refreshRate);
                    }
                    break;
                case EegProtocol.CMD_FOCUS_START:
                    sendBinaryToDevice(EegProtocol.CMD_FOCUS_START, null);
                    break;
                case EegProtocol.CMD_FOCUS_STOP:
                    sendBinaryToDevice(EegProtocol.CMD_FOCUS_STOP, null);
                    break;
                default:
                    Log.i("DOCTOR", ">>> D2P RECV generation=" + generation + " cmd=0x"
                            + Integer.toHexString(cmd) + " len=" + payloadLen);
                    break;
            }
        }
    }

    private void startPatientControlHeartbeat() {
        stopPatientControlHeartbeat();
        patientControlHeartbeatThread = new Thread(() -> {
            Log.i("DOCTOR", "Patient control heartbeat started");
            while (running && !Thread.currentThread().isInterrupted()) {
                try {
                    Thread.sleep(PATIENT_HEARTBEAT_INTERVAL_MS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }

                Socket client;
                long generation;
                boolean ready;
                long lastPong;
                synchronized (TcpServerManager.this) {
                    client = doctorToPatientClient;
                    generation = currentPatientControlGeneration;
                    ready = patientControlReady;
                    lastPong = patientLastPongAt;
                }
                if (client == null || !ready) continue;

                long silentMs = System.currentTimeMillis() - lastPong;
                if (silentMs > PATIENT_HEARTBEAT_TIMEOUT_MS) {
                    Log.w("DOCTOR", "Patient control heartbeat timeout: " + silentMs
                            + "ms, generation=" + generation);
                    closeCurrentPatientControl(client, generation, "heartbeat timeout");
                    continue;
                }
                sendBinaryToCurrentPatient(client, generation, EegProtocol.CMD_PING, null, true);
            }
            Log.i("DOCTOR", "Patient control heartbeat stopped");
        }, "doctor-patient-heartbeat");
        patientControlHeartbeatThread.setDaemon(true);
        patientControlHeartbeatThread.start();
    }

    private void stopPatientControlHeartbeat() {
        if (patientControlHeartbeatThread != null) {
            patientControlHeartbeatThread.interrupt();
            patientControlHeartbeatThread = null;
        }
    }

    private void closeCurrentPatientControl(Socket client, long generation, String reason) {
        boolean wasCurrent = false;
        boolean wasReady = false;
        synchronized (this) {
            if (doctorToPatientClient == client
                    && currentPatientControlGeneration == generation) {
                doctorToPatientClient = null;
                wasReady = patientControlReady;
                patientControlReady = false;
                currentPatientControlGeneration = patientControlGenerationCounter.incrementAndGet();
                wasCurrent = true;
            }
        }
        try { if (client != null) client.close(); } catch (Exception ignored) {}
        if (wasCurrent) {
            Log.w("DOCTOR", ">>> Patient control cleared: " + reason
                    + ", generation=" + generation);
            if (wasReady) notifyPatientConnected(false);
        }
    }

    private void updatePatientControlState(byte[] payload) {
        if (payload == null || payload.length < 1) return;
        int cmd = payload[0] & 0xFF;
        if (cmd == EegProtocol.CMD_PAGE && payload.length >= 2) {
            currentDoctorPage = payload[1] & 0xFF;
        } else if (cmd == EegProtocol.CMD_SSVEP_START_P && payload.length >= 2) {
            ssvepFreqIndex = payload[1] & 0xFF;
            ssvepActive = true;
        } else if (cmd == EegProtocol.CMD_SSVEP_STOP_P) {
            ssvepActive = false;
            ssvepFreqIndex = -1;
        }
    }

    private boolean sendPatientStateSnapshot(Socket client, long generation) {
        if (!sendBinaryToCurrentPatient(client, generation, EegProtocol.CMD_PAGE,
                new byte[]{(byte) currentDoctorPage}, true)) return false;

        int snapCmd;
        byte[] snapData;
        if (ssvepActive && ssvepFreqIndex >= 0) {
            snapCmd = EegProtocol.CMD_SSVEP_START_P;
            snapData = new byte[]{(byte) ssvepFreqIndex};
        } else {
            snapCmd = EegProtocol.CMD_SSVEP_STOP_P;
            snapData = null;
        }
        boolean ok = sendBinaryToCurrentPatient(client, generation, snapCmd, snapData, true);
        if (ok) {
            Log.i("DOCTOR", ">>> D2P STATE SNAPSHOT generation=" + generation
                    + ": page=" + currentDoctorPage
                    + ", ssvepActive=" + ssvepActive
                    + ", freqIndex=" + ssvepFreqIndex);
        }
        return ok;
    }

    private static final int BUFFER_POOL_SIZE = 16;
    private static final int BUFFER_SIZE = 512;

    private static final class RawPacket {
        byte[] buf;
        int len;
        RawPacket(byte[] buf, int len) { this.buf = buf; this.len = len; }
    }

    private void handleClient(Socket client) {
        synchronized (this) {
            this.deviceClient = client;
        }
        notifyDeviceConnected(true);
        startHeartbeat();

        ArrayBlockingQueue<RawPacket> rawPackets = new ArrayBlockingQueue<>(1024);
        ArrayBlockingQueue<byte[]> bufferPool = new ArrayBlockingQueue<>(BUFFER_POOL_SIZE);
        for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
            bufferPool.offer(new byte[BUFFER_SIZE]);
        }

        AtomicLong totalPacketsRead = new AtomicLong(0);
        AtomicLong totalPacketsDropped = new AtomicLong(0);
        AtomicLong totalFramesParsed = new AtomicLong(0);
        AtomicLong totalInvalidFrames = new AtomicLong(0);

        Thread parserThread = new Thread(() -> {
            FrameParser parser = new FrameParser(totalFramesParsed, totalInvalidFrames, udpSender);
            while (!Thread.currentThread().isInterrupted()) {
                try {
                    RawPacket pkt = rawPackets.take();
                    try {
                        for (int i = 0; i < pkt.len; i++) {
                            parser.parse(pkt.buf[i] & 0xFF);
                        }
                    } finally {
                        bufferPool.offer(pkt.buf);
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            Log.i("TCP", "Parser thread stopped, total frames parsed: " + totalFramesParsed.get() +
                    ", invalid: " + totalInvalidFrames.get());
        });
        parserThread.setDaemon(true);
        parserThread.start();

        try (InputStream in = client.getInputStream()) {
            byte[] buf = new byte[BUFFER_SIZE];
            int len;
            while ((len = in.read(buf)) != -1) {
                totalPacketsRead.addAndGet(1);

                if (forwardManager != null) {
                    forwardManager.broadcast(buf, len);
                }


                byte[] poolBuf = bufferPool.poll();
                if (poolBuf == null) {
                    totalPacketsDropped.incrementAndGet();
                    continue;
                }
                System.arraycopy(buf, 0, poolBuf, 0, len);
                if (!rawPackets.offer(new RawPacket(poolBuf, len))) {
                    totalPacketsDropped.incrementAndGet();
                    bufferPool.offer(poolBuf);
                }
            }
        } catch (Exception e) {
            Log.e("TCP", "Client error", e);
        } finally {
            stopHeartbeat();
            parserThread.interrupt();
            try { client.close(); } catch (Exception ignored) {}
            synchronized (this) {
                if (deviceClient == client) {
                    deviceClient = null;
                    notifyDeviceConnected(false);
                }
            }
        }
    }


    // ---------- 转发管理器（负责广播原始数据）----------
    private class TcpForwardManager {
        private final CopyOnWriteArrayList<Socket> forwardClients = new CopyOnWriteArrayList<>();
        private final CopyOnWriteArrayList<OutputStream> outputStreams = new CopyOnWriteArrayList<>();

        public boolean hasClients() {
            return !outputStreams.isEmpty();
        }

        public void addClient(Socket socket) {
            forwardClients.add(socket);
            try {
                outputStreams.add(socket.getOutputStream());
                new Thread(() -> {
                    try {
                        InputStream in = socket.getInputStream();
                        byte[] buf = new byte[512];
                        int n;
                        while ((n = in.read(buf)) != -1) {
                            byte[] chunk = new byte[n];
                            System.arraycopy(buf, 0, chunk, 0, n);
                            TcpServerManager.this.sendQueue.offer(chunk);
                        }
                    } catch (IOException ignored) {
                    } finally {
                        removeClient(socket);
                    }
                }).start();
            } catch (IOException e) {
                Log.e("TCP", "Failed to get output stream for forward client", e);
                removeClient(socket);
            }
        }

        public void broadcast(byte[] data, int len) {
            for (OutputStream os : outputStreams) {
                try {
                    os.write(data, 0, len);
                    os.flush();
                } catch (IOException e) {
                    Log.w("TCP", "Failed to send to forward client", e);
                }
            }
            cleanDisconnected();
        }

        public void broadcast(byte[] data) {
            broadcast(data, data.length);
        }

        private void cleanDisconnected() {
            List<Socket> toRemove = new ArrayList<>();
            for (Socket s : forwardClients) {
                if (s == null || s.isClosed() || !s.isConnected()) {
                    toRemove.add(s);
                }
            }
            for (Socket s : toRemove) {
                removeClient(s);
            }
        }

        private void removeClient(Socket socket) {
            forwardClients.remove(socket);
            try {
                socket.close();
            } catch (IOException ignored) {}
            List<OutputStream> rebuilt = new ArrayList<>();
            for (Socket s : forwardClients) {
                try {
                    OutputStream os = s.getOutputStream();
                    if (os != null) rebuilt.add(os);
                } catch (IOException e) {
                    Log.w("TCP", "Error re-adding output stream: " + e.getMessage());
                }
            }
            outputStreams.clear();
            outputStreams.addAll(rebuilt);
        }

        public void stopAll() {
            for (Socket s : forwardClients) {
                try { s.close(); } catch (IOException ignored) {}
            }
            forwardClients.clear();
            outputStreams.clear();
        }
    }

    // 以下为原有的 FrameParser 和 SpectrumReassembler，保持不变
    // ---------- 频谱重组器 ----------
    private static class SpectrumReassembler {
        private final java.util.Map<Integer, ReassemblyState> states = new java.util.HashMap<>();
        private static final long FRAG_TIMEOUT_MS = 5000;

        public float[] addFragment(int cmd, int fragIdx, int totalFrags,
                                   byte[] data, int offset, int len) {
            ReassemblyState state = states.get(cmd);
            long now = System.currentTimeMillis();
            if (state == null || fragIdx == 0
                || state.totalFrags != totalFrags
                || (now - state.createTime > FRAG_TIMEOUT_MS)) {
                state = new ReassemblyState(totalFrags);
                states.put(cmd, state);
            }
            int startFloatIdx = fragIdx * 4;
            long fragBit = 1L << fragIdx;
            if ((state.receivedMask & fragBit) != 0) {
                return null;
            }
            ByteBuffer bb = ByteBuffer.wrap(data, offset, len).order(ByteOrder.LITTLE_ENDIAN);
            for (int i = 0; i < len / 4; i++) {
                int idx = startFloatIdx + i;
                if (idx < state.mags.length) {
                    state.mags[idx] = bb.getFloat();
                }
            }
            state.receivedMask |= fragBit;
            state.receivedCount++;
            if (state.receivedCount == totalFrags) {
                states.remove(cmd);
                return state.mags;
            }
            return null;
        }

        private static class ReassemblyState {
            int totalFrags;
            float[] mags = new float[128];
            long receivedMask;
            int receivedCount;
            long createTime;
            ReassemblyState(int totalFrags) {
                this.totalFrags = totalFrags;
                this.receivedMask = 0;
                this.receivedCount = 0;
                this.createTime = System.currentTimeMillis();
            }
        }
    }

    private class FrameParser {
        private static final int STATE_WAIT_AA = 0;
        private static final int STATE_WAIT_55 = 1;
        private static final int STATE_BODY = 2;
        private static final int STATE_ESCAPE = 3;

        private int state = STATE_WAIT_AA;
        private byte[] body = new byte[1500];
        private int bodyLen = 0;
        private AtomicLong framesParsed;
        private AtomicLong invalidFrames;
        private SpectrumReassembler spectrumReassembler = new SpectrumReassembler();
        private final UdpSender udpSender;

        FrameParser(AtomicLong framesParsed, AtomicLong invalidFrames, UdpSender udpSender) {
            this.framesParsed = framesParsed;
            this.invalidFrames = invalidFrames;
            this.udpSender = udpSender;
        }

        void parse(int rawByte) {
            switch (state) {
                case STATE_WAIT_AA:
                    if (rawByte == 0xAA) state = STATE_WAIT_55;
                    break;
                case STATE_WAIT_55:
                    if (rawByte == 0x55) {
                        state = STATE_BODY;
                        bodyLen = 0;
                    } else if (rawByte == 0xAA) {
                        state = STATE_WAIT_55;
                    } else {
                        state = STATE_WAIT_AA;
                    }
                    break;
                case STATE_BODY:
                    if (rawByte == 0x7D) {
                        state = STATE_ESCAPE;
                    } else if (rawByte == 0x7E) {
                        int minBody = EegProtocol.ADDR_LEN + EegProtocol.CMD_LEN
                                + EegProtocol.LEN_LEN + EegProtocol.TS_LEN + EegProtocol.CRC_LEN;
                        if (bodyLen >= minBody) {
                            parseBinaryFrame();
                        } else {
                            invalidFrames.incrementAndGet();
                        }
                        state = STATE_WAIT_AA;
                        bodyLen = 0;
                    } else {
                        if (bodyLen < body.length) {
                            body[bodyLen++] = (byte) rawByte;
                        } else {
                            invalidFrames.incrementAndGet();
                            state = STATE_WAIT_AA;
                        }
                    }
                    break;
                case STATE_ESCAPE:
                    byte unescaped = (byte) (rawByte ^ EegProtocol.ESCAPE_XOR);
                    if (bodyLen < body.length) {
                        body[bodyLen++] = unescaped;
                    } else {
                        invalidFrames.incrementAndGet();
                        state = STATE_WAIT_AA;
                    }
                    state = STATE_BODY;
                    break;
            }
        }

        private void parseBinaryFrame() {
            int addr = body[0] & 0xFF;
            int cmd = body[1] & 0xFF;
            int payloadLen = EegProtocol.readU16LE(body, 2);
            int fixed = EegProtocol.ADDR_LEN + EegProtocol.CMD_LEN + EegProtocol.LEN_LEN + EegProtocol.TS_LEN;
            if (bodyLen != fixed + payloadLen + EegProtocol.CRC_LEN) {
                invalidFrames.incrementAndGet();
                return;
            }
            int calc = EegProtocol.checksum16(body, 0, fixed + payloadLen);
            int receivedCrc = EegProtocol.readU16LE(body, fixed + payloadLen);
            if (calc != receivedCrc) {
                invalidFrames.incrementAndGet();
                return;
            }
            boolean valid = true;
            switch (cmd) {
                case EegProtocol.CMD_WAVE: valid = handleWaveFrame(payloadLen); break;
                case EegProtocol.CMD_SPECTRUM: valid = handleSpectrumFrame(payloadLen); break;
                case EegProtocol.CMD_FOCUS: valid = handleFocusFrame(payloadLen); break;
                case EegProtocol.CMD_RESULT_MI: valid = handleResultMiFrame(payloadLen); break;
                case EegProtocol.CMD_RESULT_SSVEP: valid = handleResultSsvepFrame(payloadLen); break;
                case EegProtocol.CMD_DIAG: valid = handleDiagFrame(payloadLen); break;
                case EegProtocol.CMD_DIRCSV: valid = handleDirCsvFrame(payloadLen); break;
                case EegProtocol.CMD_CSP: valid = handleCspFrame(payloadLen); break;
                case EegProtocol.CMD_TASK: valid = handleTaskFrame(payloadLen); break;
                case EegProtocol.CMD_TASK_START:
                    if (payloadLen >= 1) {
                        byte side = body[fixed];
                        String sideStr = (side == 1) ? "RIGHT" : "LEFT";
                        dispatcher.postTaskStart(sideStr);
                        sendDisplayToOutputNow(EegProtocol.CMD_TASK_START, new byte[]{side});
                    } else {
                        valid = false;
                    }
                    break;
                case EegProtocol.CMD_INTENT: valid = handleIntentFrame(payloadLen); break;
                case EegProtocol.CMD_RESULT_DIR: valid = handleResultDirFrame(payloadLen); break;
                case EegProtocol.CMD_ANNOUNCE: valid = handleAnnounceFrame(payloadLen); break;
                case EegProtocol.CMD_EVENT: valid = handleEventFrame(payloadLen); break;
                case EegProtocol.CMD_IMPEDANCE_RESULT: valid = handleImpedanceFrame(payloadLen); break;

                case EegProtocol.CMD_DIR_MODEL_LIST: {
                    if (payloadLen >= 1) {
                        int count = body[fixed] & 0xFF;
                        StringBuilder sb = new StringBuilder();
                        sb.append("SD卡模型列表(").append(count).append("):\n");
                        int off = fixed + 1;
                        for (int i = 0; i < count && off + 44 <= fixed + payloadLen; i++) {
                            String pid = new String(body, off, 8).trim();
                            off += 8;
                            String pname = new String(body, off, 32).trim();
                            off += 32;
                            int accBits = (body[off] & 0xFF) | ((body[off+1] & 0xFF) << 8) |
                                          ((body[off+2] & 0xFF) << 16) | ((body[off+3] & 0xFF) << 24);
                            off += 4;
                            float acc = Float.intBitsToFloat(accBits);
                            sb.append(String.format(java.util.Locale.US, "  %s %s %.1f%%\n", pid, pname, acc * 100));
                        }
                        Log.i("FrameRx", sb.toString());
                        dispatcher.postDirModelList(sb.toString());
                    }
                    break;
                }

                case EegProtocol.CMD_GAME_RESULT: {
                    if (payloadLen >= 12) {
                        int off = fixed;
                        int distance = (body[off] & 0xFF) | ((body[off+1] & 0xFF) << 8) |
                                       ((body[off+2] & 0xFF) << 16) | ((body[off+3] & 0xFF) << 24);
                        off += 4;
                        int coins = (body[off] & 0xFF) | ((body[off+1] & 0xFF) << 8) |
                                    ((body[off+2] & 0xFF) << 16) | ((body[off+3] & 0xFF) << 24);
                        off += 4;
                        int score = (body[off] & 0xFF) | ((body[off+1] & 0xFF) << 8) |
                                    ((body[off+2] & 0xFF) << 16) | ((body[off+3] & 0xFF) << 24);
                        Log.i("FrameRx", "GameResult dist=" + distance + " coins=" + coins + " score=" + score);
                        dispatcher.postGameResult(distance, coins, score);
                    } else {
                        valid = false;
                    }
                    break;
                }

                case EegProtocol.CMD_ACK: valid = payloadLen >= 1; break;
                case EegProtocol.CMD_READY_TRAIN:
                    dispatcher.postReadyTrain();
                    sendDisplayToOutputNow(EegProtocol.CMD_READY_TRAIN, null);
                    break;
                case EegProtocol.CMD_READY_TEST:
                    dispatcher.postReadyTest();
                    sendDisplayToOutputNow(EegProtocol.CMD_READY_TEST, null);
                    break;
                case EegProtocol.CMD_TASK_DONE:
                case EegProtocol.CMD_TASK_STOPPED:
                    dispatcher.postTaskDone();
                    sendDisplayToOutputNow(cmd, null);
                    break;
                case EegProtocol.CMD_MODE_SET_OK:
                    if (payloadLen >= 2) {
                        int mode = body[fixed + 1] & 0xFF;
                        dispatcher.postModeSetOk(mode);
                        sendDisplayToOutputNow(EegProtocol.CMD_MODE_SET_OK,
                                new byte[]{(byte) mode});
                    } else {
                        valid = false;
                    }
                    break;
                case EegProtocol.CMD_STATUS:
                    valid = payloadLen >= 6;
                    break;
                default:
                    Log.w("FrameRx", "Unhandled cmd=0x" + Integer.toHexString(cmd) + " len=" + payloadLen);
                    valid = false;
                    break;
            }
            if (valid) {
                framesParsed.incrementAndGet();
            } else {
                invalidFrames.incrementAndGet();
            }
        }

        private int bodyPayloadOffset() {
            return EegProtocol.ADDR_LEN + EegProtocol.CMD_LEN + EegProtocol.LEN_LEN + EegProtocol.TS_LEN;
        }

        private boolean handleWaveFrame(int payloadLen) {
            if (payloadLen < EegProtocol.WAVE_PAYLOAD) return false;
            int type = body[bodyPayloadOffset()] & 0xFF;
            int uiCmd = EegChannels.waveTypeToCmd(type);
            ByteBuffer buf = ByteBuffer.wrap(body, bodyPayloadOffset() + 1,
                    EegProtocol.WAVE_NUM_CH * 4).order(ByteOrder.LITTLE_ENDIAN);
            float[] vals = new float[EegProtocol.WAVE_NUM_CH];
            for (int ch = 0; ch < EegProtocol.WAVE_NUM_CH; ch++) {
                vals[ch] = buf.getFloat();
                dispatcher.postWaveData(uiCmd, ch, vals[ch]);
                SsvepAnalysisManager.getInstance().offerWaveSample(uiCmd, ch, vals[ch]);
            }
            udpSender.sendWaveData(vals[0], vals[1]);
            return true;
        }

        private boolean handleSpectrumFrame(int payloadLen) {
            if (payloadLen < EegProtocol.SPECTRUM_HEADER + 4) return false;
            int off = bodyPayloadOffset();
            int ch = body[off] & 0xFF;
            int type = body[off + 1] & 0xFF;
            int fragIdx = body[off + 2] & 0xFF;
            int totalFrags = body[off + 3] & 0xFF;
            int dataLen = payloadLen - EegProtocol.SPECTRUM_HEADER;
            if (dataLen < 4 || dataLen % 4 != 0) return false;
            int uiCmd = EegChannels.spectrumUiCmd(type, ch);
            float[] fullMags = spectrumReassembler.addFragment(uiCmd, fragIdx, totalFrags,
                    body, off + EegProtocol.SPECTRUM_HEADER, dataLen);
            if (fullMags != null) {
                dispatcher.postSpectrumData(uiCmd, fullMags);
            }
            return true;
        }

        private boolean handleFocusFrame(int payloadLen) {
            if (payloadLen < EegProtocol.FOCUS_PAYLOAD) return false;
            int off = bodyPayloadOffset();
            ByteBuffer buf = ByteBuffer.wrap(body, off, 16).order(ByteOrder.LITTLE_ENDIAN);
            float a0 = buf.getFloat(), a1 = buf.getFloat(), e0 = buf.getFloat(), e1 = buf.getFloat();
            int trend = body[off + 16] & 0xFF;
            int instant = body[off + 17] & 0xFF;
            dispatcher.postFocusData(a0, a1, e0, e1, trend, instant);
            byte[] rawPayload = new byte[payloadLen];
            System.arraycopy(body, off, rawPayload, 0, payloadLen);
            sendDisplayToOutputNow(EegProtocol.CMD_FOCUS, rawPayload);
            return true;
        }

        private boolean handleImpedanceFrame(int payloadLen) {
            if (payloadLen < 32) return false;
            int off = bodyPayloadOffset();
            float[] kohm = new float[8];
            for (int i = 0; i < 8; i++) {
                int v = (body[off + i * 4] & 0xFF)
                      | ((body[off + i * 4 + 1] & 0xFF) << 8)
                      | ((body[off + i * 4 + 2] & 0xFF) << 16)
                      | ((body[off + i * 4 + 3] & 0xFF) << 24);
                kohm[i] = v / 100.0f;
            }
            boolean biasConnected = payloadLen >= 33 && body[off + 32] == 1;
            dispatcher.postImpedanceResult(kohm, biasConnected);
            return true;
        }


        private boolean handleResultMiFrame(int payloadLen) {
            if (payloadLen < 16) return false;
            int off = bodyPayloadOffset();
            int seq = body[off] & 0xFF;
            int pred = body[off + 1] & 0xFF;
            ByteBuffer buf = ByteBuffer.wrap(body, off + 2, 12).order(ByteOrder.LITTLE_ENDIAN);
            int scoreL = buf.getInt();
            int scoreR = buf.getInt();
            int conf = buf.getInt();
            boolean trained = (body[off + 14] & 0xFF) == 1;
            int target = (payloadLen >= 17) ? (body[off + 16] & 0xFF) : -1;
            InferenceResult result = new InferenceResult();
            result.setIntent(pred == 0 ? "LEFT" : "RIGHT");
            result.setScoreLeft(scoreL / 10000f);
            result.setScoreRight(scoreR / 10000f);
            result.setConfidence(conf / 10000f);
            result.setTrained(trained);
            if (target == 0 || target == 1) {
                result.setGroundTruth(target == 0 ? "LEFT" : "RIGHT");
            }
            dispatcher.postInferenceResult(result);
            byte[] rawPayload = new byte[payloadLen];
            System.arraycopy(body, off, rawPayload, 0, payloadLen);
            sendDisplayToOutputNow(EegProtocol.CMD_RESULT_MI, rawPayload);
            sendAckBinary(seq);
            return true;
        }

        private boolean handleResultSsvepFrame(int payloadLen) {
            if (payloadLen < 38) return false;
            int off = bodyPayloadOffset();
            int seq = (int) EegProtocol.readU32LE(body, off);
            int rawIndex = body[off + 4] & 0xFF;
            int votedIndex = body[off + 5] & 0xFF;
            if (rawIndex == 0xFF) rawIndex = -1;
            if (votedIndex == 0xFF) votedIndex = -1;
            ByteBuffer buf = ByteBuffer.wrap(body, off + 6, 28).order(ByteOrder.LITTLE_ENDIAN);
            float ratio = buf.getInt() / 10000f;
            float best = buf.getInt() / 10000f;
            float margin = buf.getInt() / 10000f;
            float[] scores = new float[4];
            for (int i = 0; i < 4; i++) scores[i] = buf.getInt() / 10000f;
            int[] votes = new int[4];
            for (int i = 0; i < 4; i++) votes[i] = body[off + 34 + i] & 0xFF;
            SsvepAnalysisManager mgr = SsvepAnalysisManager.getInstance();
            if (!(mgr.isRunning() && !mgr.isSynthetic())) {
                SsvepResult r = SsvepResult.fromMcu(seq, rawIndex, votedIndex,
                        ratio, best, margin, scores, votes);
                sendDisplayToOutput(EegProtocol.CMD_SSVEP_RESULT, r.toPatientData());
            }
            mgr.onMcuSsvepResult(seq, rawIndex, votedIndex, ratio, best, margin, scores, votes);
            sendAckBinary(seq);
            return true;
        }

        private boolean handleDiagFrame(int payloadLen) {
            if (payloadLen < 1) return false;
            int off = bodyPayloadOffset();
            int diagType = body[off] & 0xFF;
            switch (diagType) {
                case EegProtocol.DIAG_TYPE_IPCDIAG:
                    if (payloadLen >= 38) {
                        IpcDiagInfo diag = new IpcDiagInfo();
                        ByteBuffer buf = ByteBuffer.wrap(body, off + 1, 24).order(ByteOrder.LITTLE_ENDIAN);
                        diag.setAck(buf.getInt());
                        diag.setNotify(buf.getInt());
                        diag.setOk(buf.getInt());
                        diag.setBad(buf.getInt());
                        diag.setV5fhb(buf.getInt());
                        diag.setWfiWake(buf.getInt());
                        diag.setActive(body[off + 25] & 0xFF);
                        ByteBuffer reg = ByteBuffer.wrap(body, off + 26, 12).order(ByteOrder.LITTLE_ENDIAN);
                        diag.setEna(reg.getInt());
                        diag.setSts(reg.getInt());
                        diag.setIsr(reg.getInt());
                        dispatcher.postIpcDiag(diag);
                    }
                    break;

                default:
                    Log.i("FrameRx", "DIAG type=" + diagType + " len=" + payloadLen);
                    break;
            }
            return true;
        }

        private boolean handleDirCsvFrame(int payloadLen) {
            if (payloadLen < 1 + 24 * 4) return false;
            int off = bodyPayloadOffset();
            int label = body[off] & 0xFF;
            ByteBuffer buf = ByteBuffer.wrap(body, off + 1, 24 * 4).order(ByteOrder.LITTLE_ENDIAN);
            EegFrame frame = new EegFrame();
            frame.setLabel(Integer.toString(label));
            for (int i = 0; i < EegFrame.FEATURE_COUNT; i++) {
                frame.setFeature(i, buf.getInt());
            }
            dispatcher.postEegFrame(frame);
            return true;
        }

        private boolean handleCspFrame(int payloadLen) {
            if (payloadLen < 1) return false;
            Log.i("FrameRx", "CSP stream type=" + (body[bodyPayloadOffset()] & 0xFF) + " len=" + payloadLen);
            return true;
        }

        private boolean handleTaskFrame(int payloadLen) {
            if (payloadLen < EegProtocol.TASK_PAYLOAD) return false;
            int seq = body[bodyPayloadOffset()] & 0xFF;
            dispatcher.postTaskDone();
            sendBinaryToPatientNow(EegProtocol.CMD_TASK_DONE, null);
            sendAckBinary(seq);
            return true;
        }

        private boolean handleIntentFrame(int payloadLen) {
            Log.i("FrameRx", "INTENT len=" + payloadLen);
            return true;
        }

        private boolean handleResultDirFrame(int payloadLen) {
            Log.i("FrameRx", "RESULT_DIR len=" + payloadLen);
            return true;
        }

        private boolean handleAnnounceFrame(int payloadLen) {
            if (payloadLen >= 1) {
                Log.i("FrameRx", "ANNOUNCE mode=" + (body[bodyPayloadOffset()] & 0xFF));
            }
            return true;
        }

        private boolean handleEventFrame(int payloadLen) {
            if (payloadLen < 2) return false;
            int off = bodyPayloadOffset();
            int seq = body[off] & 0xFF;
            int eventType = body[off + 1] & 0xFF;
            switch (eventType) {
                case EegProtocol.EVENT_POSTURE_STATE: {
                    if (payloadLen < 7) return false;
                    String posture = postureName(body[off + 2] & 0xFF);
                    long turns = EegProtocol.readU32LE(body, off + 3);
                    dispatcher.postPostureState(posture, (int) turns);
                    break;
                }
                case EegProtocol.EVENT_TURN: {
                    if (payloadLen < 12) return false;
                    String from = postureName(body[off + 6] & 0xFF);
                    String to = postureName(body[off + 7] & 0xFF);
                    dispatcher.postTurnEvent(from, to);
                    break;
                }
                case EegProtocol.EVENT_FALL: {
                    if (payloadLen < 15) return false;
                    dispatcher.postFallEvent();
                    break;
                }
                case EegProtocol.EVENT_NO_TURN: {
                    if (payloadLen < 6) return false;
                    long durationMin = EegProtocol.readU32LE(body, off + 2);
                    dispatcher.postNoTurnAlert(durationMin);
                    break;
                }
                default:
                    return false;
            }
            sendAckBinary(seq);
            return true;
        }

        private String postureName(int p) {
            switch (p) {
                case 1: return "SUPINE";
                case 2: return "PRONE";
                case 3: return "LEFT_SIDE";
                case 4: return "RIGHT_SIDE";
                case 5: return "SITTING";
                default: return "UNKNOWN";
            }
        }

    }
}
