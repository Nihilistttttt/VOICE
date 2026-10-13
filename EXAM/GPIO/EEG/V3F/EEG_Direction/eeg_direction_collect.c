#include "eeg_direction_collect.h"
#include "signal_analysis.h"
#include "eeg_direction_feature.h"
#include "eeg_cmd_parser.h"
#include "Serial.h"
#include "Message_Parser.h"
#include "dualcore_ipc_diag.h"
#include "dualcore_ipc_shared.h"
#include <stdio.h>
#include <string.h>

extern uint8_t g_eeg_app_mode;

uint8_t dir_phase = DIR_PHASE_REST;
uint8_t dir_next_label = DIR_LABEL_LEFT;
uint16_t dir_phase_row_count = 0;
uint16_t dir_skip_row_count = 0;
uint16_t dir_round_count = 0;

uint8_t csp_collect_row_tick = 0;
uint32_t csp_window_id = 0;
uint32_t csp_filtered_sample_count = 0;

#if CMD_MODE_ENABLE
WorkMode_t g_work_mode = WORK_MODE_IDLE;
TrialState_t g_trial_state = TRIAL_IDLE;
uint8_t g_trial_label = 0;
uint16_t g_trial_row_count = 0;
uint8_t g_paused = 0;
uint8_t g_skip_rows = 4;
#endif

/*
 * 训练采集同步状态。
 * 结构体明确放入普通 .bss，不占用 RISC-V gp 小数据区，
 * 避免 V3F 工程在链接阶段出现 R_RISCV_GPREL_I 越界。
 */
typedef struct
{
    uint32_t last_v5f_fft_count;
    uint32_t last_trial_state;
    uint32_t reserve;
} DirectionCollectSync_t;

static DirectionCollectSync_t s_collect_sync
    __attribute__((section(".bss")));

/*
 * CSP采集输出状态。
 * 4通道×256点快照约占4KB，明确放入普通.bss，避免进入RISC-V gp小数据区。
 * 快照后再逐帧发送，保证发送期间环形缓冲区继续更新也不会改变当前训练窗口。
 */
typedef enum
{
    CSP_STREAM_IDLE = 0,
    CSP_STREAM_BEGIN,
    CSP_STREAM_ROWS,
    CSP_STREAM_END
} CSP_StreamPhase_t;

typedef struct
{
    int32_t sample[CSP_WIN_SIZE][4];
    uint32_t win_id;
    uint32_t trial_id;
    uint16_t row;
    uint8_t label;
    uint8_t phase;
} CSP_StreamState_t;

static CSP_StreamState_t s_csp_stream
    __attribute__((section(".bss"), aligned(4)));

static uint32_t s_csp_trial_id
    __attribute__((section(".bss")));
static uint32_t s_csp_last_trial_state
    __attribute__((section(".bss")));
static uint8_t s_csp_trial_done_pending
    __attribute__((section(".bss")));

static uint8_t CSP_TrySendBothBinary(uint8_t cmd, const uint8_t *payload, uint16_t len, uint16_t reserve_bytes)
{
    uint16_t needed;

    if (payload == 0) {
        return 0u;
    }

    needed = (uint16_t)(len + reserve_bytes);
    if ((Serial_TxFreeBytes(SERIAL_PORT_DEBUG) < needed) ||
        (Serial_TxFreeBytes(SERIAL_PORT_WIFI) < needed)) {
        return 0u;
    }

    Pack_Frame(SERIAL_PORT_DEBUG, cmd, payload, len);
    Pack_Frame(SERIAL_PORT_WIFI, cmd, payload, len);
    return 1u;
}

static int32_t CSP_VoltToUVX1000(float v)
{
    float x = v * CSP_OUTPUT_SCALE_UVX1000;
    if (x > 2147483000.0f) return 2147483000L;
    if (x < -2147483000.0f) return -2147483000L;
    return (x >= 0.0f) ? (int32_t)(x + 0.5f) : (int32_t)(x - 0.5f);
}

/*
 * 快照最近256点滤波数据。
 * V3F和V5F使用相同的去慢漂移、50Hz陷波及带通系数，
 * 因此该窗口可直接用于训练V5F端的CSP空间滤波器。
 */
static uint8_t CSP_QueueWindow(uint8_t label, RingBuffer_t *filt_buf)
{
    uint16_t j;
    int start;

    if ((filt_buf == 0) ||
        (csp_filtered_sample_count < CSP_WIN_SIZE) ||
        (s_csp_stream.phase != CSP_STREAM_IDLE)) {
        return 0u;
    }

    start = filt_buf->WriteIdx; /* WriteIdx指向最旧点，也是256点窗口的起点。 */
    s_csp_stream.win_id = ++csp_window_id;
    s_csp_stream.trial_id = s_csp_trial_id;
    s_csp_stream.label = label;
    s_csp_stream.row = 0u;

    for (j = 0; j < CSP_WIN_SIZE; j++) {
        uint16_t idx = (uint16_t)((start + j) & (CSP_WIN_SIZE - 1u));
        s_csp_stream.sample[j][0] = CSP_VoltToUVX1000(filt_buf->CH4[idx]); /* CP3 */
        s_csp_stream.sample[j][1] = CSP_VoltToUVX1000(filt_buf->CH5[idx]); /* CP4 */
        s_csp_stream.sample[j][2] = CSP_VoltToUVX1000(filt_buf->CH6[idx]); /* C3  */
        s_csp_stream.sample[j][3] = CSP_VoltToUVX1000(filt_buf->CH7[idx]); /* C4  */
    }

    s_csp_stream.phase = CSP_STREAM_BEGIN;
    return 1u;
}

static void CSP_SendTrialDone(void)
{
#if CMD_MODE_ENABLE
    uint8_t payload[PROTO_TASK_PAYLOAD];

    g_trial_state = TRIAL_DONE;
    s_csp_last_trial_state = TRIAL_DONE;
    payload[0] = Retry_GetSeq();
    payload[1] = g_trial_label;
    payload[2] = (uint8_t)(g_trial_row_count & 0xFF);
    payload[3] = (uint8_t)((g_trial_row_count >> 8) & 0xFF);
    payload[4] = 0;
    payload[5] = 0;
    memcpy(payload + 6, &csp_window_id, 4);
    memcpy(payload + 10, &s_csp_trial_id, 4);
    Pack_Frame(SERIAL_PORT_DEBUG, CMD_TASK, payload, PROTO_TASK_PAYLOAD);
    Pack_Frame(SERIAL_PORT_WIFI, CMD_TASK, payload, PROTO_TASK_PAYLOAD);
    Retry_Store(payload, PROTO_TASK_PAYLOAD, CMD_TASK);
#endif
}

void Direction_CSPStreamTask(void)
{
    uint8_t payload[PROTO_CSP_MAX_PAYLOAD];
    uint16_t n;

    if (s_csp_stream.phase == CSP_STREAM_IDLE) {
        return;
    }

    if (s_csp_stream.phase == CSP_STREAM_BEGIN) {
        uint16_t win_size = CSP_WIN_SIZE;
        n = 0;
        payload[n++] = PROTO_CSP_BEGIN;
        payload[n++] = s_csp_stream.label;
        memcpy(payload + n, &s_csp_stream.win_id, 4); n += 4;
        memcpy(payload + n, &s_csp_stream.trial_id, 4); n += 4;
        memcpy(payload + n, &win_size, 2); n += 2;
        if (!CSP_TrySendBothBinary(CMD_CSP, payload, n, 0u)) {
            return;
        }
        s_csp_stream.phase = CSP_STREAM_ROWS;
        return;
    }

    if (s_csp_stream.phase == CSP_STREAM_ROWS) {
        for (n = 0u;
             (n < CSP_STREAM_ROWS_PER_FRAME) && (s_csp_stream.row < CSP_WIN_SIZE);
             n++) {
            uint16_t row = s_csp_stream.row;
            uint16_t p = 0;
            uint16_t row16 = (uint16_t)row;
            payload[p++] = PROTO_CSP_ROW;
            memcpy(payload + p, &s_csp_stream.win_id, 4); p += 4;
            memcpy(payload + p, &row16, 2); p += 2;
            memcpy(payload + p, &s_csp_stream.sample[row][0], 4); p += 4;
            memcpy(payload + p, &s_csp_stream.sample[row][1], 4); p += 4;
            memcpy(payload + p, &s_csp_stream.sample[row][2], 4); p += 4;
            memcpy(payload + p, &s_csp_stream.sample[row][3], 4); p += 4;
            if (!CSP_TrySendBothBinary(CMD_CSP, payload, p, 0u)) {
                return;
            }
            s_csp_stream.row++;
        }
        if (s_csp_stream.row >= CSP_WIN_SIZE) {
            s_csp_stream.phase = CSP_STREAM_END;
        }
        return;
    }

    n = 0;
    payload[n++] = PROTO_CSP_END;
    payload[n++] = s_csp_stream.label;
    memcpy(payload + n, &s_csp_stream.win_id, 4); n += 4;
    memcpy(payload + n, &s_csp_stream.trial_id, 4); n += 4;
    if (!CSP_TrySendBothBinary(CMD_CSP, payload, n, s_csp_trial_done_pending ? 64u : 0u)) {
        return;
    }

    s_csp_stream.phase = CSP_STREAM_IDLE;
    s_csp_stream.row = 0u;

    /* 最后一个窗口完整发送后再通知上位机试次结束，保证保存文件不会漏掉尾窗。 */
    if (s_csp_trial_done_pending) {
        s_csp_trial_done_pending = 0u;
        CSP_SendTrialDone();
    }
}

void Direction_CSPStreamReset(void)
{
    memset(&s_csp_stream, 0, sizeof(s_csp_stream));
    s_csp_stream.phase = CSP_STREAM_IDLE;
    s_csp_trial_done_pending = 0u;
    s_csp_trial_id = 0u;
    s_csp_last_trial_state = TRIAL_IDLE;
}

uint8_t Direction_CSPStreamBusy(void)
{
    return (s_csp_stream.phase != CSP_STREAM_IDLE) ? 1u : 0u;
}

void Direction_AutoCollectCSPProcess(RingBuffer_t *filt_buf)
{
#if CMD_MODE_ENABLE
    /* 检测新的LEFT/RIGHT试次，为原始串口日志附加trial编号。 */
    if ((g_trial_state == TRIAL_RUNNING) &&
        (s_csp_last_trial_state != TRIAL_RUNNING)) {
        s_csp_trial_id++;
        csp_collect_row_tick = 0u;
        s_csp_trial_done_pending = 0u;
    }
    s_csp_last_trial_state = g_trial_state;

    if (g_paused) return;
    if (g_work_mode == WORK_MODE_TRAIN && g_trial_state == TRIAL_RUNNING) {
        /* 已到试次末尾时等待最后一个CSP窗口完整输出，不再重复计数。 */
        if (s_csp_trial_done_pending) {
            if (!Direction_CSPStreamBusy()) {
                s_csp_trial_done_pending = 0u;
                CSP_SendTrialDone();
            }
            return;
        }

        g_trial_row_count++;
        if (g_trial_row_count > g_skip_rows) {
            csp_collect_row_tick++;
            if (csp_collect_row_tick >= CSP_COLLECT_EVERY_ROWS) {
                csp_collect_row_tick = 0u;
                (void)CSP_QueueWindow(g_trial_label, filt_buf);
            }
        }

        if (g_trial_row_count >= TRIAL_DURATION_ROWS) {
            if (Direction_CSPStreamBusy()) {
                s_csp_trial_done_pending = 1u;
            } else {
                CSP_SendTrialDone();
            }
        }
        return;
    }
    if (g_work_mode == WORK_MODE_TEST) {
        return;
    }
    return;
#else
    uint8_t label_to_print = 0;
    if (dir_phase == DIR_PHASE_REST) {
        Direction_UpdateRestBaseline();
    } else if (dir_phase == DIR_PHASE_LEFT || dir_phase == DIR_PHASE_RIGHT) {
        if (dir_skip_row_count > 0) {
            dir_skip_row_count--;
            csp_collect_row_tick = 0;
        } else {
            csp_collect_row_tick++;
            if (csp_collect_row_tick >= CSP_COLLECT_EVERY_ROWS) {
                csp_collect_row_tick = 0;
                label_to_print = (dir_phase == DIR_PHASE_LEFT) ? DIR_LABEL_LEFT : DIR_LABEL_RIGHT;
            }
        }
    }
    if (label_to_print != 0) {
        (void)CSP_QueueWindow(label_to_print, filt_buf);
    }
    dir_phase_row_count++;
    if (dir_phase == DIR_PHASE_REST) {
        if (dir_phase_row_count >= DIR_REST_ROWS) {
            dir_phase_row_count = 0;
            dir_skip_row_count = DIR_SKIP_ROWS_AFTER_SWITCH;
            csp_collect_row_tick = 0;
            if (dir_next_label == DIR_LABEL_LEFT) {
                dir_phase = DIR_PHASE_LEFT;
                Serial_Printf(DIR_TEXT_PORT, "TASK=LEFT,start_light_grip_or_imagine,skip=%d,feature=CSPWIN\r\n", DIR_SKIP_ROWS_AFTER_SWITCH);
            } else {
                dir_phase = DIR_PHASE_RIGHT;
                Serial_Printf(DIR_TEXT_PORT, "TASK=RIGHT,start_light_grip_or_imagine,skip=%d,feature=CSPWIN\r\n", DIR_SKIP_ROWS_AFTER_SWITCH);
            }
        }
    } else if (dir_phase == DIR_PHASE_LEFT) {
        if (dir_phase_row_count >= DIR_COLLECT_ROWS_PER_PHASE) {
            dir_phase_row_count = 0;
            dir_phase = DIR_PHASE_REST;
            dir_next_label = DIR_LABEL_RIGHT;
            csp_collect_row_tick = 0;
            Direction_ResetRestBaseline();
            Serial_Printf(DIR_TEXT_PORT, "TASK=REST,next right\r\n");
        }
    } else if (dir_phase == DIR_PHASE_RIGHT) {
        if (dir_phase_row_count >= DIR_COLLECT_ROWS_PER_PHASE) {
            dir_phase_row_count = 0;
            dir_phase = DIR_PHASE_REST;
            dir_next_label = DIR_LABEL_LEFT;
            csp_collect_row_tick = 0;
            dir_round_count++;
            Direction_ResetRestBaseline();
            Serial_Printf(DIR_TEXT_PORT, "TASK=REST,next left,round=%d\r\n", dir_round_count);
        }
    }
#endif
}

void Direction_AutoCollectProcess(float theta_pow[NUM_CHANNELS],
                                  float alpha_pow[NUM_CHANNELS],
                                  float beta_pow[NUM_CHANNELS])
{
#if CMD_MODE_ENABLE
    int32_t v5f_feature[DUALCORE_V5F_FEATURE_DIM];
    uint32_t v5f_fft_count = 0;

    /*
     * 进入新试次时先记住当前 V5F 窗口编号，
     * 防止把休息阶段最后一个窗口错误标成新的 LEFT/RIGHT 标签。
     */
    if (g_trial_state != s_collect_sync.last_trial_state) {
        s_collect_sync.last_trial_state = g_trial_state;
        s_collect_sync.last_v5f_fft_count = DualCore_IPC_GetLastV5FFFTCount();
    }

    if (g_paused) return;
    if (g_work_mode == WORK_MODE_TRAIN && g_trial_state == TRIAL_RUNNING) {
        /*
         * 只在 V5F 产生新特征时计数和保存。
         * theta/alpha/beta 参数仍保留，以保持原函数接口和其他模块不变。
         */
        if (!DualCore_IPC_CopyLastV5FFeature(v5f_feature,
                                                  DUALCORE_V5F_FEATURE_DIM,
                                                  &v5f_fft_count)) {
            return;
        }
        if (v5f_fft_count == s_collect_sync.last_v5f_fft_count) {
            return;
        }
        s_collect_sync.last_v5f_fft_count = v5f_fft_count;

        g_trial_row_count++;
        if (g_trial_row_count > g_skip_rows) {
            Direction_PrintV5FFeatureCSV(g_trial_label, v5f_feature);
        }
        if (g_trial_row_count >= TRIAL_DURATION_ROWS) {
            g_trial_state = TRIAL_DONE;
            s_collect_sync.last_trial_state = TRIAL_DONE;
            {
                uint8_t payload[PROTO_TASK_PAYLOAD];
                payload[0] = Retry_GetSeq();
                payload[1] = g_trial_label;
                payload[2] = (uint8_t)(g_trial_row_count & 0xFF);
                payload[3] = (uint8_t)((g_trial_row_count >> 8) & 0xFF);
                payload[4] = (uint8_t)(g_skip_rows & 0xFF);
                payload[5] = (uint8_t)((g_skip_rows >> 8) & 0xFF);
                memset(payload + 6, 0, 8);
                Pack_Frame(SERIAL_PORT_DEBUG, CMD_TASK, payload, PROTO_TASK_PAYLOAD);
                Pack_Frame(SERIAL_PORT_WIFI, CMD_TASK, payload, PROTO_TASK_PAYLOAD);
                Retry_Store(payload, PROTO_TASK_PAYLOAD, CMD_TASK);
            }
        }
        return;
    }
    if (g_work_mode == WORK_MODE_TEST) {
        return;
    }
    return;
#else
    uint8_t label_to_print = 0;
    if (dir_phase == DIR_PHASE_REST) {
        Direction_UpdateRestBaseline();
    } else if (dir_phase == DIR_PHASE_LEFT || dir_phase == DIR_PHASE_RIGHT) {
        if (dir_skip_row_count > 0) {
            Direction_UpdateTaskZero();
            dir_skip_row_count--;
        } else {
            label_to_print = (dir_phase == DIR_PHASE_LEFT) ? DIR_LABEL_LEFT : DIR_LABEL_RIGHT;
        }
    }
    if (label_to_print != 0) {
        Direction_PrintFeatureCSV(label_to_print, theta_pow, alpha_pow, beta_pow);
    }
    dir_phase_row_count++;
    if (dir_phase == DIR_PHASE_REST) {
        if (dir_phase_row_count >= DIR_REST_ROWS) {
            dir_phase_row_count = 0;
            dir_skip_row_count = DIR_SKIP_ROWS_AFTER_SWITCH;
            Direction_ResetTaskZero();
            if (dir_next_label == DIR_LABEL_LEFT) {
                dir_phase = DIR_PHASE_LEFT;
                Serial_Printf(DIR_TEXT_PORT, "TASK=LEFT,start_light_grip_or_imagine,skip=%d,feature=nn_window\r\n", DIR_SKIP_ROWS_AFTER_SWITCH);
            } else {
                dir_phase = DIR_PHASE_RIGHT;
                Serial_Printf(DIR_TEXT_PORT, "TASK=RIGHT,start_light_grip_or_imagine,skip=%d,feature=nn_window\r\n", DIR_SKIP_ROWS_AFTER_SWITCH);
            }
        }
    } else if (dir_phase == DIR_PHASE_LEFT) {
        if (dir_phase_row_count >= DIR_COLLECT_ROWS_PER_PHASE) {
            dir_phase_row_count = 0;
            dir_phase = DIR_PHASE_REST;
            dir_next_label = DIR_LABEL_RIGHT;
            Direction_ResetRestBaseline();
            Serial_Printf(DIR_TEXT_PORT, "TASK=REST,next right\r\n");
        }
    } else if (dir_phase == DIR_PHASE_RIGHT) {
        if (dir_phase_row_count >= DIR_COLLECT_ROWS_PER_PHASE) {
            dir_phase_row_count = 0;
            dir_phase = DIR_PHASE_REST;
            dir_next_label = DIR_LABEL_LEFT;
            dir_round_count++;
            Direction_ResetRestBaseline();
            Serial_Printf(DIR_TEXT_PORT, "TASK=REST,next left,round=%d\r\n", dir_round_count);
        }
    }
#endif
}
