package com.nihilisttt.eegdoctor;

/** Mode-1 FBCCA parameters. Keep these values aligned with SSVEP.py. */
public final class FbccaConfig {
    public static final double SAMPLE_RATE = 250.0;
    public static final int WINDOW_SIZE = (int) (SAMPLE_RATE * 5.0); // 1250
    public static final int STEP_SIZE = (int) (SAMPLE_RATE * 2.0);   // 500
    public static final int HARMONICS = 2;

    public static final double[] TARGET_FREQS = {11.0, 13.0, 15.0, 17.0};

    public static final int[] SSVEP_CHANNELS = {EegChannels.CH_O1, EegChannels.CH_OZ};
    public static final double[][] FILTER_BANKS = {
            {6.0, 45.0},
            {10.0, 45.0},
            {14.0, 45.0},
            {18.0, 45.0}
    };
    public static final double[] FREQ_SEARCH_OFFSETS = {-0.05, 0.0, 0.05};

    public static final double RATIO_THRESHOLD = 1.25;
    public static final double MARGIN_THRESHOLD = 0.01;
    public static final double MIN_SCORE = 0.0;

    public static final int VOTE_HISTORY_LEN = 5;
    public static final int VOTE_MIN_COUNT = 3;

    public static final double CHANNEL_MIN_STD_MV = 1e-7;
    public static final double CHANNEL_MAX_STD_MV = 100.0;
    public static final double CHANNEL_MAX_PTP_MV = 300.0;

    public static final double NOTCH_FREQ = 50.0;
    public static final double NOTCH_RHO = 0.98;

    /** Device protocol normally sends volts. Change to 1.0 if the device already sends mV. */
    public static final double INPUT_SCALE_TO_MV = 1000.0;

    /** Preferred waveform command: 0x10 filtered waveform. Raw 0x04 is used after a short fallback delay. */
    public static final int PREFERRED_WAVE_CMD = EegChannels.CMD_WAVE_FILT;
    public static final int FALLBACK_WAVE_CMD = EegChannels.CMD_WAVE_RAW;
    public static final long RAW_FALLBACK_DELAY_MS = 1000L;

    private FbccaConfig() {}

    public static double filterBankWeight(int bankIndex) {
        int m = bankIndex + 1;
        return Math.pow(m, -1.25) + 0.25;
    }
}
