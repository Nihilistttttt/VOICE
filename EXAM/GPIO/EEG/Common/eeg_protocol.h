#ifndef EEG_PROTOCOL_H
#define EEG_PROTOCOL_H

#include <stdint.h>
#include <string.h>

#define PROTO_FRAME_HEADER0      0xAA
#define PROTO_FRAME_HEADER1      0x55
#define PROTO_FRAME_TAIL         0x7E
#define PROTO_ESCAPE_CHAR        0x7D
#define PROTO_ESCAPE_XOR         0x20

#define PROTO_HEADER_LEN         2
#define PROTO_ADDR_LEN           1
#define PROTO_CMD_LEN            1
#define PROTO_LEN_LEN            2
#define PROTO_TS_LEN             4
#define PROTO_CRC_LEN            2
#define PROTO_TAIL_LEN           1

#define PROTO_FIXED_OVERHEAD    (PROTO_HEADER_LEN + PROTO_ADDR_LEN + PROTO_CMD_LEN + \
                                 PROTO_LEN_LEN + PROTO_TS_LEN + PROTO_CRC_LEN + PROTO_TAIL_LEN)
#define PROTO_BODY_BASE         (PROTO_ADDR_LEN + PROTO_CMD_LEN + PROTO_LEN_LEN + PROTO_TS_LEN)

typedef enum {
    PROTO_ADDR_MCU      = 0x01,
    PROTO_ADDR_DOCTOR   = 0x02,
    PROTO_ADDR_PATIENT  = 0x03,
    PROTO_ADDR_PYTHON   = 0x04
} ProtoAddr_t;

typedef enum {
    CMD_NULL               = 0x00,

    CMD_MODE_SET           = 0x01,
    CMD_MODE_TRAIN         = 0x02,
    CMD_MODE_TEST          = 0x03,
    CMD_MODEL_SET          = 0x04,
    CMD_MODEL_GET          = 0x05,
    CMD_TRIAL              = 0x06,
    CMD_STOP               = 0x07,
    CMD_STATUS             = 0x08,
    CMD_IPCDIAG            = 0x09,
    CMD_POSTURE            = 0x0A,
    CMD_DISPLAY_CFG        = 0x0B,
    CMD_SSVEP_START        = 0x0C,
    CMD_SSVEP_STOP         = 0x0D,
    CMD_SSVEP_SELFTEST_START = 0x0E,
    CMD_SSVEP_SELFTEST_STOP  = 0x0F,
    CMD_ACK                = 0x10,

    CMD_WAVE               = 0x20,
    CMD_SPECTRUM           = 0x21,
    CMD_FOCUS              = 0x22,
    CMD_RESULT_MI          = 0x23,
    CMD_RESULT_SSVEP       = 0x24,
    CMD_DIAG               = 0x25,
    CMD_DIRCSV             = 0x26,
    CMD_CSP                = 0x27,
    CMD_TASK               = 0x28,
    CMD_INTENT             = 0x29,
    CMD_RESULT_DIR         = 0x2A,
    CMD_ANNOUNCE           = 0x2B,
    CMD_EVENT              = 0x2C,

    CMD_PING               = 0x30,
    CMD_PONG               = 0x31,
    CMD_PATIENT_READY      = 0x32,
    CMD_CONTROL_READY      = 0x33,
    CMD_PAGE               = 0x34,
    CMD_SSVEP_START_P      = 0x35,
    CMD_SSVEP_STOP_P       = 0x36,
    CMD_READY_TRAIN        = 0x37,
    CMD_TRAIN_STOP         = 0x38,
    CMD_READY_TEST         = 0x39,
    CMD_TASK_START         = 0x3A,
    CMD_TASK_DONE          = 0x3B,
    CMD_TASK_STOPPED       = 0x3C,
    CMD_MODE_SET_OK        = 0x3D,
    CMD_TARGET             = 0x3E,
    CMD_SSVEP_STIM_STARTED = 0x41,
    CMD_SSVEP_RESULT       = 0x42,
    CMD_IMPEDANCE_CHECK    = 0x43,
    CMD_IMPEDANCE_RESULT   = 0x44,
    CMD_INFER_CFG          = 0x45,
    CMD_INFER_CFG_OK       = 0x46,
    CMD_ALARM_ACK          = 0x47,
    CMD_RECORD_START       = 0x48,
    CMD_RECORD_STOP        = 0x49,
    CMD_DIR_WEIGHT         = 0x4A,
    CMD_DIR_MODEL_PUSH     = 0x4B,
    CMD_DIR_MODEL_SCAN     = 0x4C,
    CMD_DIR_MODEL_LIST     = 0x4D,
    CMD_GAME_START         = 0x4E,
    CMD_GAME_STOP          = 0x4F,
    CMD_GAME_RESULT        = 0x50,
    CMD_FOCUS_START        = 0x51,
    CMD_FOCUS_STOP         = 0x52
} ProtoCmd_t;

typedef enum {
    PROTO_WAVE_RAW      = 0,
    PROTO_WAVE_FILT     = 1,
    PROTO_WAVE_BASELINE = 2,
    PROTO_WAVE_NONE     = 0xFF
} ProtoWaveType_t;

typedef enum {
    PROTO_SPEC_RAW         = 0,
    PROTO_SPEC_FREQ_FILTER = 1,
    PROTO_SPEC_TIME_FILTER = 2,
    PROTO_SPEC_NONE        = 0xFF
} ProtoSpecType_t;

typedef enum {
    DIAG_TYPE_IPCDIAG       = 0,
    DIAG_TYPE_V5F_DIAG      = 1,
    DIAG_TYPE_SSVEP_DIAG    = 2,
    DIAG_TYPE_MODE_DIAG     = 3,
    DIAG_TYPE_V5F_RAW       = 4,
    DIAG_TYPE_POSTURE       = 5,
    DIAG_TYPE_POSTURE_ANGLE = 6
} ProtoDiagType_t;

typedef enum {
    EVENT_POSTURE_STATE = 0,
    EVENT_TURN          = 1,
    EVENT_FALL          = 2,
    EVENT_NO_TURN       = 3
} ProtoEventType_t;

#define PROTO_CSP_BEGIN           0
#define PROTO_CSP_ROW             1
#define PROTO_CSP_END             2
#define PROTO_CSP_MAX_PAYLOAD     23

#define PROTO_TASK_PAYLOAD        14

#define PROTO_WAVE_NUM_CH   8
#define PROTO_WAVE_HEADER   1
#define PROTO_WAVE_PAYLOAD  (PROTO_WAVE_HEADER + PROTO_WAVE_NUM_CH * 4)

#define PROTO_SPECTRUM_FLOAT_PER_FRAG 16
#define PROTO_SPECTRUM_FRAG_SIZE      (PROTO_SPECTRUM_FLOAT_PER_FRAG * 4)
#define PROTO_SPECTRUM_HEADER         4

#define PROTO_FOCUS_PAYLOAD 18

uint16_t Proto_Checksum16(const uint8_t *data, uint16_t len);
uint32_t Proto_GetTimestampMs(void);

#endif