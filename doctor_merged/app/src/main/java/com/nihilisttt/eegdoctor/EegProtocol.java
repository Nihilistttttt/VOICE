package com.nihilisttt.eegdoctor;

/** Unified binary protocol constants, aligned with MCU eeg_protocol.h. */
public final class EegProtocol {
    public static final int FRAME_HEADER0 = 0xAA;
    public static final int FRAME_HEADER1 = 0x55;
    public static final int FRAME_TAIL = 0x7E;
    public static final int ESCAPE_CHAR = 0x7D;
    public static final int ESCAPE_XOR = 0x20;

    public static final int HEADER_LEN = 2;
    public static final int ADDR_LEN = 1;
    public static final int CMD_LEN = 1;
    public static final int LEN_LEN = 2;
    public static final int TS_LEN = 4;
    public static final int CRC_LEN = 2;
    public static final int TAIL_LEN = 1;

    public static final int FIXED_OVERHEAD = HEADER_LEN + ADDR_LEN + CMD_LEN + LEN_LEN + TS_LEN + CRC_LEN + TAIL_LEN;

    public static final int ADDR_MCU = 0x01;
    public static final int ADDR_DOCTOR = 0x02;
    public static final int ADDR_PATIENT = 0x03;
    public static final int ADDR_PYTHON = 0x04;

    public static final int CMD_NULL = 0x00;
    public static final int CMD_MODE_SET = 0x01;
    public static final int CMD_MODE_TRAIN = 0x02;
    public static final int CMD_MODE_TEST = 0x03;
    public static final int CMD_MODEL_SET = 0x04;
    public static final int CMD_MODEL_GET = 0x05;
    public static final int CMD_TRIAL = 0x06;
    public static final int CMD_STOP = 0x07;
    public static final int CMD_STATUS = 0x08;
    public static final int CMD_IPCDIAG = 0x09;
    public static final int CMD_POSTURE = 0x0A;
    public static final int CMD_DISPLAY_CFG = 0x0B;
    public static final int CMD_SSVEP_START = 0x0C;
    public static final int CMD_SSVEP_STOP = 0x0D;
    public static final int CMD_SSVEP_SELFTEST_START = 0x0E;
    public static final int CMD_SSVEP_SELFTEST_STOP = 0x0F;
    public static final int CMD_ACK = 0x10;

    public static final int CMD_WAVE = 0x20;
    public static final int CMD_SPECTRUM = 0x21;
    public static final int CMD_FOCUS = 0x22;
    public static final int CMD_RESULT_MI = 0x23;
    public static final int CMD_RESULT_SSVEP = 0x24;
    public static final int CMD_DIAG = 0x25;
    public static final int CMD_DIRCSV = 0x26;
    public static final int CMD_CSP = 0x27;
    public static final int CMD_TASK = 0x28;
    public static final int CMD_INTENT = 0x29;
    public static final int CMD_RESULT_DIR = 0x2A;
    public static final int CMD_ANNOUNCE = 0x2B;
    public static final int CMD_EVENT = 0x2C;

    public static final int CMD_PING = 0x30;
    public static final int CMD_PONG = 0x31;
    public static final int CMD_PATIENT_READY = 0x32;
    public static final int CMD_CONTROL_READY = 0x33;
    public static final int CMD_PAGE = 0x34;
    public static final int CMD_SSVEP_START_P = 0x35;
    public static final int CMD_SSVEP_STOP_P = 0x36;
    public static final int CMD_READY_TRAIN = 0x37;
    public static final int CMD_TRAIN_STOP = 0x38;
    public static final int CMD_READY_TEST = 0x39;
    public static final int CMD_TASK_START = 0x3A;
    public static final int CMD_TASK_DONE = 0x3B;
    public static final int CMD_TASK_STOPPED = 0x3C;
    public static final int CMD_MODE_SET_OK = 0x3D;
    public static final int CMD_TARGET = 0x3E;
    public static final int CMD_SSVEP_STIM_STARTED = 0x41;
    public static final int CMD_SSVEP_RESULT = 0x42;
    public static final int CMD_IMPEDANCE_CHECK = 0x43;
    public static final int CMD_IMPEDANCE_RESULT = 0x44;
    public static final int CMD_INFER_CFG = 0x45;
    public static final int CMD_INFER_CFG_OK = 0x46;
    public static final int CMD_ALARM_ACK = 0x47;
    public static final int CMD_RECORD_START = 0x48;
    public static final int CMD_RECORD_STOP = 0x49;
    public static final int CMD_DIR_WEIGHT = 0x4A;
    public static final int CMD_DIR_MODEL_PUSH = 0x4B;
    public static final int CMD_DIR_MODEL_SCAN = 0x4C;
    public static final int CMD_DIR_MODEL_LIST = 0x4D;
    public static final int CMD_GAME_START = 0x4E;
    public static final int CMD_GAME_STOP = 0x4F;
    public static final int CMD_GAME_RESULT = 0x50;
    public static final int CMD_FOCUS_START = 0x51;
    public static final int CMD_FOCUS_STOP = 0x52;


    public static final int WAVE_NUM_CH = 8;
    public static final int WAVE_HEADER = 1;
    public static final int WAVE_PAYLOAD = WAVE_HEADER + WAVE_NUM_CH * 4;

    public static final int WAVE_TYPE_RAW = 0;
    public static final int WAVE_TYPE_FILT = 1;
    public static final int WAVE_TYPE_BASELINE = 2;
    public static final int WAVE_TYPE_NONE = 0xFF;

    public static final int SPEC_TYPE_RAW = 0;
    public static final int SPEC_TYPE_FREQ_FILTER = 1;
    public static final int SPEC_TYPE_TIME_FILTER = 2;
    public static final int SPEC_TYPE_NONE = 0xFF;

    public static final int DIAG_TYPE_IPCDIAG = 0;
    public static final int DIAG_TYPE_V5F_DIAG = 1;
    public static final int DIAG_TYPE_SSVEP_DIAG = 2;
    public static final int DIAG_TYPE_MODE_DIAG = 3;
    public static final int DIAG_TYPE_V5F_RAW = 4;
    public static final int DIAG_TYPE_POSTURE = 5;
    public static final int DIAG_TYPE_POSTURE_ANGLE = 6;


    public static final int EVENT_POSTURE_STATE = 0;
    public static final int EVENT_TURN = 1;
    public static final int EVENT_FALL = 2;
    public static final int EVENT_NO_TURN = 3;

    public static final int CSP_BEGIN = 0;
    public static final int CSP_ROW = 1;
    public static final int CSP_END = 2;
    public static final int CSP_MAX_PAYLOAD = 23;

    public static final int TASK_PAYLOAD = 14;

    public static final int SPECTRUM_FLOAT_PER_FRAG = 16;
    public static final int SPECTRUM_FRAG_SIZE = SPECTRUM_FLOAT_PER_FRAG * 4;
    public static final int SPECTRUM_HEADER = 4;

    public static final int FOCUS_PAYLOAD = 18;

    private EegProtocol() {}

    public static int checksum16(byte[] data, int offset, int len) {
        int sum = 0;
        for (int i = 0; i < len; i++) {
            sum = (sum + (data[offset + i] & 0xFF)) & 0xFFFF;
        }
        return sum;
    }

    public static int readU16LE(byte[] data, int offset) {
        return (data[offset] & 0xFF) | ((data[offset + 1] & 0xFF) << 8);
    }

    public static long readU32LE(byte[] data, int offset) {
        return (data[offset] & 0xFFL)
                | ((data[offset + 1] & 0xFFL) << 8)
                | ((data[offset + 2] & 0xFFL) << 16)
                | ((data[offset + 3] & 0xFFL) << 24);
    }

    public static void writeU16LE(byte[] data, int offset, int value) {
        data[offset] = (byte) (value & 0xFF);
        data[offset + 1] = (byte) ((value >> 8) & 0xFF);
    }

    public static void writeU32LE(byte[] data, int offset, long value) {
        data[offset] = (byte) (value & 0xFF);
        data[offset + 1] = (byte) ((value >> 8) & 0xFF);
        data[offset + 2] = (byte) ((value >> 16) & 0xFF);
        data[offset + 3] = (byte) ((value >> 24) & 0xFF);
    }

    public static int floatToBits(float value) {
        return Float.floatToIntBits(value);
    }

    /** Pack a complete escaped binary frame, aligned with MCU Pack_Frame(). */
    public static byte[] packFrame(int addr, int cmd, byte[] payload) {
        int payLen = (payload == null) ? 0 : payload.length;
        int bodyBase = ADDR_LEN + CMD_LEN + LEN_LEN + TS_LEN;
        int bodyLen = bodyBase + payLen + CRC_LEN;

        byte[] raw = new byte[bodyLen];
        raw[0] = (byte) addr;
        raw[1] = (byte) cmd;
        writeU16LE(raw, 2, payLen);
        long ts = System.currentTimeMillis() & 0xFFFFFFFFL;
        writeU32LE(raw, 4, ts);
        if (payLen > 0) {
            System.arraycopy(payload, 0, raw, bodyBase, payLen);
        }
        int crc = checksum16(raw, 0, bodyBase + payLen);
        writeU16LE(raw, bodyBase + payLen, crc);

        int cap = HEADER_LEN + bodyLen * 2 + TAIL_LEN;
        byte[] frame = new byte[cap];
        int idx = 0;
        frame[idx++] = (byte) FRAME_HEADER0;
        frame[idx++] = (byte) FRAME_HEADER1;
        for (int i = 0; i < bodyLen; i++) {
            int b = raw[i] & 0xFF;
            if (b == FRAME_HEADER0 || b == FRAME_HEADER1
                    || b == FRAME_TAIL || b == ESCAPE_CHAR) {
                frame[idx++] = (byte) ESCAPE_CHAR;
                frame[idx++] = (byte) (b ^ ESCAPE_XOR);
            } else {
                frame[idx++] = (byte) b;
            }
        }
        frame[idx++] = (byte) FRAME_TAIL;
        byte[] result = new byte[idx];
        System.arraycopy(frame, 0, result, 0, idx);
        return result;
    }
}