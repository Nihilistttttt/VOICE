package com.nihilisttt.eegdoctor;

/** Immutable progress/state update for the doctor SSVEP page. */
public final class SsvepProgress {
    public enum State { IDLE, PREPARED, WAITING_WAVE_SOURCE, COLLECTING, ANALYZING, STOPPED, ERROR }

    private final State state;
    private final int bufferedSamples;
    private final int windowSamples;
    private final int samplesUntilNextUpdate;
    private final int waveCommand;
    private final boolean synthetic;
    private final String message;

    public SsvepProgress(State state,
                         int bufferedSamples,
                         int windowSamples,
                         int samplesUntilNextUpdate,
                         int waveCommand,
                         boolean synthetic,
                         String message) {
        this.state = state;
        this.bufferedSamples = bufferedSamples;
        this.windowSamples = windowSamples;
        this.samplesUntilNextUpdate = samplesUntilNextUpdate;
        this.waveCommand = waveCommand;
        this.synthetic = synthetic;
        this.message = message == null ? "" : message;
    }

    public State getState() { return state; }
    public int getBufferedSamples() { return bufferedSamples; }
    public int getWindowSamples() { return windowSamples; }
    public int getSamplesUntilNextUpdate() { return samplesUntilNextUpdate; }
    public int getWaveCommand() { return waveCommand; }
    public boolean isSynthetic() { return synthetic; }
    public String getMessage() { return message; }

    public int getPercent() {
        if (windowSamples <= 0) return 0;
        return Math.max(0, Math.min(100, Math.round(bufferedSamples * 100f / windowSamples)));
    }
}
