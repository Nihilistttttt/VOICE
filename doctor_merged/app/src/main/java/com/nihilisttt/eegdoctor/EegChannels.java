package com.nihilisttt.eegdoctor;

/**
 * EEG logical channel mapping — single source of truth.
 *
 * MCU raw_vals index → electrode name.
 * This matches Signal_Analysis.c:
 *   raw_vals[0]=OZ, [1]=O1, [2]=F3, [3]=F4,
 *   [4]=CP3, [5]=CP4, [6]=C3, [7]=C4
 *
 * Physical ADS1299 pin → ch_data index (different numbering!):
 *   ch_data[0]=C4, [1]=F4, [2]=CP4, [3]=OZ,
 *   ch_data[4]=O1, [5]=CP3, [6]=C3, [7]=F3
 * Physical pins are NOT used on the Android side.
 */
public final class EegChannels {

    public static final int NUM_CHANNELS = 8;

    public static final int CH_OZ  = 0;
    public static final int CH_O1  = 1;
    public static final int CH_F3  = 2;
    public static final int CH_F4  = 3;
    public static final int CH_CP3 = 4;
    public static final int CH_CP4 = 5;
    public static final int CH_C3  = 6;
    public static final int CH_C4  = 7;

    public static final String[] NAMES = {
        "OZ", "O1", "F3", "F4", "CP3", "CP4", "C3", "C4"
    };

    public static String nameOf(int ch) {
        return (ch >= 0 && ch < NAMES.length) ? NAMES[ch] : "CH" + ch;
    }

    // ---- Waveform command types (sent by MCU) ----
    public static final int CMD_WAVE_RAW      = 0x04;
    public static final int CMD_WAVE_FILT     = 0x10;
    public static final int CMD_WAVE_BASELINE = 0x11;

    public static boolean isWaveCmd(int cmd) {
        return cmd == CMD_WAVE_RAW || cmd == CMD_WAVE_FILT || cmd == CMD_WAVE_BASELINE;
    }

    public static int waveTypeToCmd(int waveType) {
        switch (waveType) {
            case 1:  return CMD_WAVE_FILT;
            case 2:  return CMD_WAVE_BASELINE;
            default: return CMD_WAVE_RAW;
        }
    }

    // ---- Spectrum command ranges (sent by MCU) ----
    public static final int CMD_SPEC_RAW_BASE      = 0x20;
    public static final int CMD_SPEC_RAW_END       = 0x27;
    public static final int CMD_SPEC_FREQ_FILT_BASE = 0x30;
    public static final int CMD_SPEC_FREQ_FILT_END = 0x37;
    public static final int CMD_SPEC_TIME_FILT_BASE = 0x40;
    public static final int CMD_SPEC_TIME_FILT_END  = 0x47;

    public static final int SPEC_TYPE_RAW       = 0;
    public static final int SPEC_TYPE_FREQ_FILT = 1;
    public static final int SPEC_TYPE_TIME_FILT = 2;
    public static final int SPEC_TYPE_NONE      = 0xFF;

    public static final int WAVE_TYPE_RAW      = 0;
    public static final int WAVE_TYPE_FILT     = 1;
    public static final int WAVE_TYPE_BASELINE = 2;
    public static final int WAVE_TYPE_NONE     = 0xFF;

    public static final String[] SPEC_TYPE_NAMES = {
        "原始频谱", "频域滤波频谱", "时域滤波频谱"
    };

    public static final String[] WAVE_TYPE_NAMES = {
        "原始波形", "滤波波形", "基线修复"
    };

    public static boolean isSpectrumCmd(int cmd) {
        return (cmd >= CMD_SPEC_RAW_BASE && cmd <= CMD_SPEC_RAW_END)
            || (cmd >= CMD_SPEC_FREQ_FILT_BASE && cmd <= CMD_SPEC_FREQ_FILT_END)
            || (cmd >= CMD_SPEC_TIME_FILT_BASE && cmd <= CMD_SPEC_TIME_FILT_END);
    }

    public static int spectrumCmdToChannel(int cmd) {
        if (cmd >= CMD_SPEC_RAW_BASE && cmd <= CMD_SPEC_RAW_END)
            return cmd - CMD_SPEC_RAW_BASE;
        if (cmd >= CMD_SPEC_FREQ_FILT_BASE && cmd <= CMD_SPEC_FREQ_FILT_END)
            return cmd - CMD_SPEC_FREQ_FILT_BASE;
        if (cmd >= CMD_SPEC_TIME_FILT_BASE && cmd <= CMD_SPEC_TIME_FILT_END)
            return cmd - CMD_SPEC_TIME_FILT_BASE;
        return -1;
    }

    public static int spectrumCmdToType(int cmd) {
        if (cmd >= CMD_SPEC_RAW_BASE && cmd <= CMD_SPEC_RAW_END)
            return SPEC_TYPE_RAW;
        if (cmd >= CMD_SPEC_FREQ_FILT_BASE && cmd <= CMD_SPEC_FREQ_FILT_END)
            return SPEC_TYPE_FREQ_FILT;
        if (cmd >= CMD_SPEC_TIME_FILT_BASE && cmd <= CMD_SPEC_TIME_FILT_END)
            return SPEC_TYPE_TIME_FILT;
        return -1;
    }

    public static int spectrumUiCmd(int specType, int ch) {
        int base;
        switch (specType) {
            case SPEC_TYPE_FREQ_FILT: base = CMD_SPEC_FREQ_FILT_BASE; break;
            case SPEC_TYPE_TIME_FILT: base = CMD_SPEC_TIME_FILT_BASE; break;
            default: base = CMD_SPEC_RAW_BASE; break;
        }
        return base + ch;
    }

    public static byte[] buildDisplayConfigData(int... params) {
        return buildDisplayConfigDataFromArray(params);
    }

    public static byte[] buildDisplayConfigDataFromArray(int[] params) {
        int len = Math.min(params.length, 24);
        byte[] data = new byte[len];
        for (int i = 0; i < len; i++) data[i] = (byte) params[i];
        return data;
    }

    public static byte[] buildAllNoneDisplayConfigData() {
        int[] p = new int[24];
        for (int i = 0; i < 8; i++) {
            p[i * 3] = 0;
            p[i * 3 + 1] = WAVE_TYPE_NONE;
            p[i * 3 + 2] = SPEC_TYPE_NONE;
        }
        return buildDisplayConfigDataFromArray(p);
    }

    public static byte[] build8ChannelDisplayConfigData(int waveType, int specType) {
        int[] p = new int[24];
        for (int i = 0; i < 8; i++) {
            p[i * 3] = i;
            p[i * 3 + 1] = waveType;
            p[i * 3 + 2] = specType;
        }
        return buildDisplayConfigDataFromArray(p);
    }

    private EegChannels() {}
}