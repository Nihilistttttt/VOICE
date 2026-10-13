package com.nihilisttt.eegdoctor;

import java.net.DatagramSocket;
import java.net.DatagramPacket;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * 新增UDP发送器，严格适配Python上位机UDP协议
 * 协议头：16字节(魔数+版本+类型+序号+时间戳) + int32波形数据
 * 单位：1 int = 0.1 μV
 */
public class UdpSender {
    // Python 默认UDP端口（固定9000，与项目配置一致）
    public static final int PY_UDP_PORT = 9000;
    private static final int MAX_BUF = 1024;

    private DatagramSocket udpSocket;
    private InetAddress targetIp;
    private int targetPort = PY_UDP_PORT;
    private final byte[] buffer = new byte[MAX_BUF];
    private final ByteBuffer byteBuffer;
    private int frameSeq = 0; // 帧自增序号
    private boolean enableUdp = false; // UDP开关，默认关闭

    public UdpSender() {
        byteBuffer = ByteBuffer.wrap(buffer);
        byteBuffer.order(ByteOrder.LITTLE_ENDIAN); // 强制小端，和Python协议对齐
        try {
            udpSocket = new DatagramSocket();
            udpSocket.setBroadcast(true); // 支持局域网广播
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /** 设置Python设备IP */
    public void setTargetIp(String ipAddr) {
        try {
            this.targetIp = InetAddress.getByName(ipAddr);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    /** 开启/关闭UDP发送 */
    public void setEnableUdp(boolean enable) {
        this.enableUdp = enable;
    }

    /** 判断UDP是否已配置可用 */
    public boolean isUdpAvailable() {
        return enableUdp && udpSocket != null && targetIp != null;
    }

    /** 发送双通道波形数据（对外统一接口）
     * @param ch0 通道0 幅值(μV)
     * @param ch1 通道1 幅值(μV)
     */
    public void sendWaveData(float ch0, float ch1) {
        if (!isUdpAvailable()) return;

        byteBuffer.clear();
        // 1. 帧头 16字节
        byteBuffer.putShort((short) 0xA5A5);   // Magic: 0xA5A5 (2B)
        byteBuffer.put((byte) 0x01);           // Version: 0x01 (1B)
        byteBuffer.put((byte) 0x00);           // 帧类型：0=实时数据 (1B)
        byteBuffer.putInt(frameSeq++);         // 包序号 (4B)
        byteBuffer.putLong(System.currentTimeMillis()); // 毫秒时间戳 (8B)

        // 2. 波形负载：float(μV) → int(单位0.1μV)
        int rawCh0 = (int) (ch0 * 10);
        int rawCh1 = (int) (ch1 * 10);
        byteBuffer.putInt(rawCh0);
        byteBuffer.putInt(rawCh1);

        // 组装UDP包并发送
        int totalLen = byteBuffer.position();
        byte[] sendData = new byte[totalLen];
        System.arraycopy(buffer, 0, sendData, 0, totalLen);

        try {
            DatagramPacket packet = new DatagramPacket(sendData, totalLen, targetIp, targetPort);
            udpSocket.send(packet);
        } catch (Exception e) {
            // 高频数据可弱化日志，避免刷屏
        }
    }

    /** 释放资源 */
    public void release() {
        enableUdp = false;
        if (udpSocket != null) {
            udpSocket.close();
            udpSocket = null;
        }
    }
}