package com.nihilisttt.eegdoctor;

import android.util.Log;

public class CommandSender {
    private static final String TAG = "CommandSender";
    private static final CommandSender INSTANCE = new CommandSender();

    public static CommandSender getInstance() { return INSTANCE; }

    public void setTarget(String ip, int port) {}
    public void setTarget(String ip) {}

    public boolean connect() {
        return TcpServerManager.getInstance().isDeviceConnected();
    }

    public void disconnect() {}

    public boolean isConnected() {
        return TcpServerManager.getInstance().isDeviceConnected();
    }

    /** 兼容旧文本命令入口：由 TcpServerManager 映射为二进制帧。 */
    public boolean sendCommand(String cmd) {
        Log.i(TAG, "sendCommand: [" + cmd + "]");
        TcpServerManager.getInstance().sendToDevice(cmd);
        return true;
    }

    public boolean setMode(int mode) {
        Log.i(TAG, "setMode: " + mode);
        TcpServerManager.getInstance().sendBinaryToDevice(
                EegProtocol.CMD_MODE_SET, new byte[]{(byte) mode});
        return true;
    }

    public boolean setModeCollect() { return setMode(1); }
    public boolean setModeInfer() { return setMode(2); }
    public boolean setModeCalibrate() { return setMode(3); }

    public boolean startTraining() {
        Log.i(TAG, "startTraining");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_MODE_TRAIN, null);
        return true;
    }

    public boolean startTest() {
        Log.i(TAG, "startTest");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_MODE_TEST, null);
        return true;
    }

    public boolean trialLeft() {
        Log.i(TAG, "trialLeft");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_TRIAL, new byte[]{0});
        return true;
    }

    public boolean trialRight() {
        Log.i(TAG, "trialRight");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_TRIAL, new byte[]{1});
        return true;
    }

    public boolean sendPage(int page) {
        TcpServerManager.getInstance().sendDisplayToOutput(EegProtocol.CMD_PAGE, new byte[]{(byte) page});
        return true;
    }

    public boolean ssvepStart(int freqIndex) {
        Log.i(TAG, "ssvepStart freqIndex=" + freqIndex);
        TcpServerManager.getInstance().sendBinaryToDevice(
                EegProtocol.CMD_SSVEP_START, new byte[]{(byte) freqIndex});
        return true;
    }

    public boolean ssvepStop() {
        Log.i(TAG, "ssvepStop");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_SSVEP_STOP, null);
        return true;
    }

    public boolean ssvepSelftestStart(int freqIndex) {
        Log.i(TAG, "ssvepSelftestStart freqIndex=" + freqIndex);
        TcpServerManager.getInstance().sendBinaryToDevice(
                EegProtocol.CMD_SSVEP_SELFTEST_START, new byte[]{(byte) freqIndex});
        return true;
    }

    public boolean ssvepSelftestStop() {
        Log.i(TAG, "ssvepSelftestStop");
        TcpServerManager.getInstance().sendBinaryToDevice(EegProtocol.CMD_SSVEP_SELFTEST_STOP, null);
        return true;
    }
}
