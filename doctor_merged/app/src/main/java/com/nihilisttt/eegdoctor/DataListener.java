package com.nihilisttt.eegdoctor;

public interface DataListener {
    void onWaveData(int cmd, int ch, float val);
    void onSpectrumData(int cmd, float[] mags);
    void onFocusData(float attn0, float attn1, float ema0, float ema1, int trend, int instant);
    default void onEegFrame(EegFrame frame) {}
    default void onInferenceResult(InferenceResult result) {}
    default void onIpcDiag(IpcDiagInfo diag) {}
    default void onDirConfig(String configJson) {}
    default void onTaskStart(String side) {}
    default void onTaskDone() {}
    default void onReadyTrain() {}
    default void onReadyTest() {}
    default void onModeSetOk(int mode) {}
    default void onTurnEvent(String from, String to) {}
    default void onFallEvent() {}
    default void onNoTurnAlert(long durationMin) {}
    default void onPostureState(String posture, int turnCount) {}
    default void onSsvepResult(SsvepResult result) {}
    default void onSsvepProgress(SsvepProgress progress) {}
    default void onImpedanceResult(float[] kohm, boolean biasConnected) {}
    default void onDirModelList(String listText) {}
    default void onGameResult(int distance, int coins, int score) {}


}