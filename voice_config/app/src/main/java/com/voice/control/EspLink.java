package com.voice.control;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * VOICE 配置 App 通信层 —— 协议 v2(统一二进制成帧), TCP server 模式
 * (对齐 EEG 医生端架构)。
 *
 * 拓扑: MCU(UART6 v2帧) ⇄ ESP8266(TCP client, 连网关:41002) ⇄ 本类(TCP
 *       server, 手机=热点网关)。ESP 由 EEG 同款 ino 提供, 只透传+PING 拦截。
 *
 * 帧格式: 0xAA | LEN | TYPE | PAYLOAD(LEN B) | CRC8
 * TYPE: 0x01 PARAM_SET(id+val16) 0x02 CMD(sub) 0x03 WAVE(s1+s2) 0x04 PARAM_ECHO
 *
 * 线程模型: 全部网络操作在后台线程(接收线程 + 发送线程池), 主线程零网络。
 */
public class EspLink {

    public interface Listener {
        void onClientConnected(String ip);    /* ESP 连入 */
        void onClientDisconnected();
        void onWave(int s1, int s2);
        void onParamEcho(int id, int value);
        void onLog(String line);
    }

    public static final byte FRAME_HEAD = (byte) 0xAA;
    public static final byte TYPE_PARAM_SET = 0x01;
    public static final byte TYPE_CMD = 0x02;
    public static final byte TYPE_WAVE = 0x03;
    public static final byte TYPE_PARAM_ECHO = 0x04;

    public static final int SERVER_PORT = 41002;

    private final Listener mListener;
    private ServerSocket mServer;
    private Socket mClient;
    private OutputStream mOut;
    private volatile boolean mRun = false;
    private Thread mAcceptThread;
    private Thread mRxThread;
    private ExecutorService mTx;

    public EspLink(Listener l) {
        mListener = l;
        mTx = Executors.newSingleThreadExecutor();
    }

    /* ---- 帧构造: 0xAA LEN TYPE PAY CRC8 (总长 = PAYLOAD + 4) ---- */
    public static byte[] buildFrame(byte type, byte[] payload) {
        int n = payload == null ? 0 : payload.length;
        byte[] f = new byte[n + 4];
        f[0] = FRAME_HEAD;
        f[1] = (byte) (n + 1);
        f[2] = type;
        byte crc = type;
        int o = 3;
        if (payload != null) {
            for (byte b : payload) {
                f[o++] = b;
                crc ^= b;
            }
        }
        f[o] = crc;
        return f;
    }

    public static byte[] buildParamSet(int id, int value) {
        return buildFrame(TYPE_PARAM_SET, new byte[]{
                (byte) id, (byte) (value >> 8), (byte) value});
    }

    public static byte[] buildCmd(int sub) {
        return buildFrame(TYPE_CMD, new byte[]{(byte) sub});
    }

    /* ---- 生命周期: 监听 41002, 等待 ESP 连入 ---- */
    public synchronized boolean start() {
        if (mRun) return true;
        mRun = true;
        mAcceptThread = new Thread(this::acceptLoop, "EspLink-Accept");
        mAcceptThread.start();
        return true;
    }

    public synchronized void stop() {
        mRun = false;
        closeClient();
        try { if (mServer != null) mServer.close(); } catch (Exception ignored) { }
        mServer = null;
        if (mAcceptThread != null) mAcceptThread.interrupt();
        mAcceptThread = null;
    }

    public boolean isRunning() { return mRun; }
    public boolean isClientConnected() {
        return mClient != null && mClient.isConnected() && !mClient.isClosed();
    }

    private void acceptLoop() {
        try {
            mServer = new ServerSocket();                    /* 先构造后绑定 */
            mServer.setReuseAddress(true);                   /* 端口复用, 防"监听失败" */
            mServer.bind(new java.net.InetSocketAddress(SERVER_PORT));
            mListener.onLog("TCP 监听 :" + SERVER_PORT + ", 等待 ESP 连入...");
        } catch (Exception e) {
            mListener.onLog("监听失败: " + e.getMessage());
            return;
        }
        while (mRun) {
            try {
                Socket s = mServer.accept();          /* 阻塞等 ESP 连入 */
                synchronized (this) {
                    closeClient();
                    mClient = s;
                    mOut = s.getOutputStream();
                    s.setTcpNoDelay(true);
                }
                String ip = s.getInetAddress().getHostAddress();
                mListener.onClientConnected(ip);

                mRxThread = new Thread(() -> rxLoop(s), "EspLink-RX");
                mRxThread.start();
            } catch (Exception e) {
                if (mRun) mListener.onLog("accept 异常: " + e.getMessage());
            }
        }
    }

    private void closeClient() {
        try { if (mClient != null) mClient.close(); } catch (Exception ignored) { }
        mClient = null;
        mOut = null;
    }

    private void rxLoop(Socket s) {
        byte[] buf = new byte[1024];
        try {
            InputStream in = s.getInputStream();
            int n;
            while (mRun && (n = in.read(buf)) > 0) {
                for (int i = 0; i < n; i++) {
                    feedByte(buf[i]);
                }
            }
        } catch (Exception ignored) {
        }
        if (mRun) {
            mListener.onClientDisconnected();
            mListener.onLog("ESP 断开, 等待重连...");
        }
    }

    /* ---- 发送: 提交后台线程池(主线程零网络; 无客户端时静默丢弃) ---- */
    public void sendAsync(byte[] frame) {
        if (!mRun) return;
        mTx.execute(() -> {
            try {
                OutputStream out = mOut;
                if (out == null) return;              /* ESP 未连入: 丢弃 */
                synchronized (EspLink.class) {
                    out.write(frame);
                    out.flush();
                }
            } catch (Exception e) {
                mListener.onLog("发送失败: " + e.getMessage());
            }
        });
    }

    public void sendParamAsync(int id, int value) {
        sendAsync(buildParamSet(id, value));
    }

    public void sendCmdAsync(int sub) {
        sendAsync(buildCmd(sub));
    }

    /* ---- 帧解析状态机(TCP 流上, 与固件同款) ---- */
    private byte rHead;
    private int rLen, rType, rIdx, rCrc;
    private byte[] rPay = new byte[64];

    private void feedByte(byte b) {
        switch (rHead) {
        case 0:
            if (b == FRAME_HEAD) rHead = 1;
            break;
        case 1:
            if (b >= 1 && b <= 65) { rLen = b - 1; rHead = 2; }
            else rHead = 0;
            break;
        case 2:
            rType = b & 0xFF; rCrc = rType; rIdx = 0;
            rHead = (byte) ((rLen == 0) ? 4 : 3);
            break;
        case 3:
            if (rIdx < rPay.length) rPay[rIdx++] = b;
            rCrc ^= b;
            if (--rLen == 0) rHead = 4;
            break;
        case 4:
            if ((b & 0xFF) == rCrc) {
                dispatch(rType, rPay, rIdx);
            } else {
                mListener.onLog("错帧(CRC)");
            }
            rHead = 0;
            break;
        }
    }

    private void dispatch(int type, byte[] pay, int len) {
        switch (type) {
        case TYPE_WAVE:
            if (len >= 4) {
                int s1 = (short)(((pay[0] & 0xFF) << 8) | (pay[1] & 0xFF));
                int s2 = (short)(((pay[2] & 0xFF) << 8) | (pay[3] & 0xFF));
                mListener.onWave(s1, s2);
            }
            break;
        case TYPE_PARAM_ECHO:
            if (len >= 3) {
                int id = pay[0] & 0xFF;
                int v = ((pay[1] & 0xFF) << 8) | (pay[2] & 0xFF);
                mListener.onParamEcho(id, v);
            }
            break;
        default:
            break;
        }
    }
}
