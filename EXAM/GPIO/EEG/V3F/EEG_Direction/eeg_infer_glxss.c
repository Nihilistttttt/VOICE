#include "hardware.h"
#include "eeg_infer_glxss.h"
#include "Message_Parser.h"
#include "Serial.h"
#include "dualcore_ipc.h"
#include "eeg_protocol.h"
#include "eeg_cmd_parser.h"
#include "signal_analysis.h"
#include "wav_player.h"
#include <string.h>
#include <stdlib.h>

#ifdef GLXSS_ENABLED
#include "ipc_log.h"
#endif

extern uint8_t g_eeg_app_mode;
extern volatile uint8_t g_v5f_active;

#define GLXSS_INFER_STATE_IDLE       0
#define GLXSS_INFER_STATE_TARGET     1
#define GLXSS_INFER_STATE_COLLECT    2
#define GLXSS_INFER_STATE_RESULT     3   /* 最终投票结果显示窗口 */

static uint8_t s_state = GLXSS_INFER_STATE_IDLE;
static uint8_t s_mode = GLXSS_INFER_MODE_CYCLE;
static uint8_t s_rounds = 3;
static uint8_t s_cur_dir = 0;          /* 0=LEFT, 1=RIGHT */
static uint32_t s_frame_count = 0;
static uint32_t s_cycle_rounds_left = 0;
static uint32_t s_cycle_rounds_right = 0;
static uint32_t s_total_targets = 0;

/* 投票累积: COLLECT 窗口内多次推理取多数 */
static uint32_t s_vote_left = 0;
static uint32_t s_vote_right = 0;
static uint8_t s_final_pred = 0;
static int32_t s_final_sl = 0;
static int32_t s_final_sr = 0;
static int32_t s_final_conf = 0;

void GLXSS_Infer_Config(uint8_t mode, uint8_t rounds)
{
    s_mode = (mode == GLXSS_INFER_MODE_RANDOM) ? GLXSS_INFER_MODE_RANDOM
                                              : GLXSS_INFER_MODE_CYCLE;
    if (rounds == 0) rounds = 1;
    s_rounds = rounds;
}

void GLXSS_Infer_Start(void)
{
    if (s_state != GLXSS_INFER_STATE_IDLE) return;
    s_cycle_rounds_left = s_rounds;
    s_cycle_rounds_right = s_rounds;
    s_total_targets = 0;
    s_cur_dir = 0;
    s_vote_left = 0;
    s_vote_right = 0;
    s_final_sl = 0;
    s_final_sr = 0;
    s_final_conf = 0;
    s_state = GLXSS_INFER_STATE_TARGET;
    s_frame_count = 0;
#ifdef GLXSS_ENABLED
    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] INFER start mode=%u rounds=%u\r\n",
                  (unsigned)s_mode, (unsigned)s_rounds);
    IPC_Cmd_Send_V3F(IPC_CMD_ARROW, 0);
    wav_player_play("left.wav");
#endif
}

void GLXSS_Infer_Stop(void)
{
    if (s_state == GLXSS_INFER_STATE_IDLE) return;
    s_state = GLXSS_INFER_STATE_IDLE;
#ifdef GLXSS_ENABLED
    IPC_Cmd_Send_V3F(IPC_CMD_RESET, 0);
#endif
}

uint8_t GLXSS_Infer_IsActive(void)
{
    return (s_state != GLXSS_INFER_STATE_IDLE) ? 1u : 0u;
}

static uint8_t next_direction(void)
{
    if (s_mode == GLXSS_INFER_MODE_RANDOM) {
        uint32_t r = (s_total_targets * 2654435761u) + 0x9E3779B9u;
        r = (r >> 13) ^ r;
        return (uint8_t)(r & 1u);
    }
    /* cycle: LEFT x rounds then RIGHT x rounds, repeat */
    if (s_cycle_rounds_left > 0) {
        s_cycle_rounds_left--;
        return 0;
    }
    if (s_cycle_rounds_right > 0) {
        s_cycle_rounds_right--;
        return 1;
    }
    /* flip back */
    s_cycle_rounds_left = s_rounds;
    s_cycle_rounds_right = s_rounds;
    return next_direction();
}

static void report_window_result(void)
{
    uint8_t result_buf[17];
    uint8_t seq = Retry_GetSeq();
    uint8_t pred = (s_final_sl >= s_final_sr) ? 0u : 1u;
    result_buf[0] = seq;
    result_buf[1] = pred;
    memcpy(result_buf + 2, &s_final_sl, 4);
    memcpy(result_buf + 6, &s_final_sr, 4);
    memcpy(result_buf + 10, &s_final_conf, 4);
    result_buf[14] = (uint8_t)(DualCore_IPC_GetLastV5FModelTrained() ? 1u : 0u);
    result_buf[15] = (uint8_t)DualCore_IPC_GetLastV5FModelUsed();
    result_buf[16] = s_cur_dir;       /* ground-truth target label */
    Pack_Frame(SERIAL_PORT_DEBUG, CMD_RESULT_MI, result_buf, sizeof(result_buf));
    Pack_Frame(SERIAL_PORT_WIFI, CMD_RESULT_MI, result_buf, sizeof(result_buf));
    Retry_Store(result_buf, sizeof(result_buf), CMD_RESULT_MI);
    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] INFER win tgt=%s pred=%s sl=%ld sr=%ld\r\n",
                  s_cur_dir ? "R" : "L", pred ? "R" : "L",
                  (long)s_final_sl, (long)s_final_sr);
#ifdef GLXSS_ENABLED
    /* 最终投票结果命令驱动 V5F 显示结果箭头 */
    IPC_Cmd_Send_V3F(IPC_CMD_MI_RESULT, s_final_pred);
#endif
}

void GLXSS_Infer_Poll(void)
{
    if (s_state == GLXSS_INFER_STATE_IDLE) return;
    if (g_eeg_app_mode != EEG_APP_MODE_INFER) return;

    s_frame_count++;

    if (s_state == GLXSS_INFER_STATE_TARGET) {
        /* 目标显示+投票采集合并为4s: 全程显示白色目标箭头 */
#if GYRO_DIR_INFER_ENABLE
        /* 陀螺仪俯仰角投票: 0~180度=LEFT, 181~359度=RIGHT (每帧一票) */
        {
            int32_t pitch_deg10 = IPC_LOG_SHARED->v3f_pitch_deg10;
            if (pitch_deg10 > 1800) s_vote_right++;
            else                     s_vote_left++;
        }
#else
        uint32_t infer_cnt = DualCore_IPC_GetLastV5FInferCount();
        uint32_t infer_valid = DualCore_IPC_GetLastV5FInferValid();
        static uint32_t s_last_cnt = 0xFFFFFFFFu;
        if (infer_valid && infer_cnt != s_last_cnt) {
            s_last_cnt = infer_cnt;
            uint8_t p = (uint8_t)DualCore_IPC_GetLastV5FPred();
            int32_t sl = DualCore_IPC_GetLastV5FScoreLeft();
            int32_t sr = DualCore_IPC_GetLastV5FScoreRight();
            int32_t conf = DualCore_IPC_GetLastV5FConfidence();
            if (p == IPC_PRED_LEFT || p == IPC_PRED_RIGHT) {
                if (p == IPC_PRED_LEFT) s_vote_left++;
                else s_vote_right++;
                s_final_sl = sl;
                s_final_sr = sr;
                s_final_conf = conf;
            }
        }
#endif

        if (s_frame_count >= GLXSS_INFER_TARGET_FRAMES) {
            /* 4s结束: 投票决定最终结果 */
            s_final_pred = (s_vote_right >= s_vote_left) ? 1u : 0u;
#if GYRO_DIR_INFER_ENABLE
            /* 陀螺仪模式: confidence随机0~70% */
            {
                static uint32_t s_rng = 12345u;
                s_rng = s_rng * 1103515245u + 12345u;
                s_final_conf = (int32_t)(s_rng % 7001u);
            }
#endif
            if (s_final_pred == 0u) {
                s_final_sl = 10000 - s_final_conf;
                s_final_sr = s_final_conf;
            } else {
                s_final_sl = s_final_conf;
                s_final_sr = 10000 - s_final_conf;
            }
            report_window_result();
            s_total_targets++;
            s_state = GLXSS_INFER_STATE_RESULT;
            s_frame_count = 0;
        }
        return;
    }

    /* RESULT 状态: 等待 V5F 显示结果(1s)+消失(0.5s), 期间不投票不推理 */
    if (s_state == GLXSS_INFER_STATE_RESULT) {
        if (s_frame_count >= GLXSS_INFER_RESULT_FRAMES) {
            s_cur_dir = next_direction();
            s_state = GLXSS_INFER_STATE_TARGET;
            s_frame_count = 0;
            s_vote_left = 0;
            s_vote_right = 0;
            s_final_sl = 0;
            s_final_sr = 0;
            s_final_conf = 0;
#ifdef GLXSS_ENABLED
            IPC_Cmd_Send_V3F(IPC_CMD_ARROW, s_cur_dir);
            wav_player_play(s_cur_dir == 0u ? "left.wav" : "right.wav");
#endif
        }
    }
}
