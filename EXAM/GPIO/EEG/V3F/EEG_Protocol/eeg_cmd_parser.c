#include "eeg_cmd_parser.h"
#include "signal_analysis.h"
#include "eeg_direction_collect.h"
#include "eeg_direction_infer.h"
#include "eeg_direction_feature.h"
#include "eeg_fft.h"
#include "Message_Parser.h"
#include "Serial.h"
#include "dualcore_ipc.h"
#include "eeg_protocol.h"
#include "ADS1299.h"
#include "eeg_infer_glxss.h"
#include "wav_player.h"
#include "eeg_record.h"
#include "dir_model_sd.h"
#ifdef HAS_ICM42605
#include "patient_monitor.h"
#endif
#ifdef GLXSS_ENABLED
#include "ipc_log.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern uint8_t g_focus_sim_active;
extern float   g_focus_sim_offset;

extern uint8_t g_eeg_app_mode;
extern uint8_t g_ipc_diag_enable;
extern volatile uint8_t g_ssvep_active;
#ifdef HAS_ICM42605
extern uint8_t g_posture_diag_enable;
#endif
extern volatile uint32_t g_icm42605_ms_tick;

#define RETRY_TIMEOUT_MS 300

static uint8_t s_retry_payload[256];
static uint16_t s_retry_len = 0;
static uint8_t s_retry_cmd = 0;
static uint8_t s_retry_seq = 0;
static uint8_t s_retry_count = 0;
static uint8_t s_retry_max = 10;
static uint32_t s_retry_send_tick = 0;

uint8_t Retry_GetSeq(void)
{
    return s_retry_seq + 1u;
}

static void Retry_SendNow(void)
{
    if (s_retry_len == 0) return;
    Pack_Frame(SERIAL_PORT_WIFI, s_retry_cmd, s_retry_payload, s_retry_len);
}

void Retry_Store(const uint8_t *payload, uint16_t len, uint8_t cmd)
{
    s_retry_seq++;
    s_retry_cmd = cmd;
    s_retry_len = len > sizeof(s_retry_payload) ? (uint16_t)sizeof(s_retry_payload) : len;
    memcpy(s_retry_payload, payload, s_retry_len);
    s_retry_count = 0;
    s_retry_max = 10;
    s_retry_send_tick = g_icm42605_ms_tick;
}

void Retry_StoreEx(const uint8_t *payload, uint16_t len, uint8_t cmd, uint8_t max_count)
{
    s_retry_seq++;
    s_retry_cmd = cmd;
    s_retry_len = len > sizeof(s_retry_payload) ? (uint16_t)sizeof(s_retry_payload) : len;
    memcpy(s_retry_payload, payload, s_retry_len);
    s_retry_count = 0;
    s_retry_max = max_count;
    s_retry_send_tick = g_icm42605_ms_tick;
}

void Retry_Tick(void)
{
    if (s_retry_len == 0) return;
    if (s_retry_count >= s_retry_max) {
        s_retry_len = 0;
        return;
    }
    uint32_t elapsed = g_icm42605_ms_tick - s_retry_send_tick;
    if (elapsed >= RETRY_TIMEOUT_MS) {
        s_retry_count++;
        s_retry_send_tick = g_icm42605_ms_tick;
        Retry_SendNow();
    }
}

static void Retry_Ack(uint8_t seq)
{
    if (seq == s_retry_seq && s_retry_len != 0) {
        s_retry_len = 0;
    }
}

static void Send_Resp(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    Pack_Frame(SERIAL_PORT_DEBUG, cmd, payload, len);
    Pack_Frame(SERIAL_PORT_WIFI, cmd, payload, len);
}

static void Send_RespOk(uint8_t cmd)
{
    uint8_t payload[1] = {0};
    Send_Resp(cmd, payload, 1);
}

static void Send_RespErr(uint8_t code)
{
    uint8_t payload[1] = {(uint8_t)code};
    Send_Resp(CMD_NULL, payload, 1);
    (void)code;
}

void Parse_CommandBinary(const uint8_t *payload, uint16_t len, const char *source)
{
    (void)source;
    if (len < 1) return;
    uint8_t cmd = payload[0];
    const uint8_t *data = payload + 1;
    uint16_t dlen = len - 1;

    switch (cmd) {
    case CMD_ACK:
        if (dlen >= 1) {
            Retry_Ack(data[0]);
        }
        break;

    case CMD_MODE_TRAIN:
        g_work_mode = WORK_MODE_TRAIN;
        g_trial_state = TRIAL_IDLE;
        g_paused = 0;
        g_v5f_active = V5F_ACTIVE_COLLECT;
        DualCore_IPC_RequestV5FReset();
        Direction_ResetRestBaseline();
        if (g_eeg_app_mode == EEG_APP_MODE_COLLECT_CSP) {
            Direction_CSPStreamReset();
        }
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_MODE_TRAIN -> IPC ARROW_TRAIN(2=rest)\r\n");
        IPC_Cmd_Send_V3F(IPC_CMD_ARROW_TRAIN, 2);
        wav_player_play("rest.wav");
#endif
        Send_RespOk(CMD_READY_TRAIN);
        break;

    case CMD_MODE_TEST:
        g_work_mode = WORK_MODE_TEST;
        g_trial_state = TRIAL_IDLE;
        g_paused = 0;
        g_v5f_active = V5F_ACTIVE_INFER;
        EEG_FFT_ResetInferState();
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_MODE_TEST -> INFER start\r\n");
        GLXSS_Infer_Start();
#endif
        Send_RespOk(CMD_READY_TEST);
        break;

    case CMD_MODEL_SET:
        if (dlen >= 1) {
            uint8_t select = data[0];
            if (select == DUALCORE_V5F_MODEL_SELECT_AUTO
                || select == DUALCORE_V5F_MODEL_SELECT_FFT24
                || select == DUALCORE_V5F_MODEL_SELECT_CSP) {
                DualCore_IPC_SetModelSelect(select);
                uint8_t resp[2] = {CMD_MODEL_SET, select};
                Send_Resp(CMD_MODE_SET_OK, resp, 2);
            } else {
                Send_RespErr(1);
            }
        }
        break;

    case CMD_MODEL_GET:
        {
            uint8_t resp[2] = {CMD_MODEL_GET, DualCore_IPC_GetModelSelect()};
            Send_Resp(CMD_MODE_SET_OK, resp, 2);
        }
        break;

    case CMD_MODE_SET:
        if (dlen >= 1) {
            uint8_t mode = data[0];
            if (mode == EEG_APP_MODE_COLLECT || mode == EEG_APP_MODE_INFER || mode == EEG_APP_MODE_COLLECT_CSP) {
                g_eeg_app_mode = mode;
                if (mode == EEG_APP_MODE_INFER) {
                    g_v5f_active = V5F_ACTIVE_INFER;
                    DualCore_IPC_RequestV5FReset();
#ifdef GLXSS_ENABLED
                    IPC_Cmd_Send_V3F(IPC_CMD_MI_INFER, 0);
#endif
                } else {
                    g_v5f_active = V5F_ACTIVE_IDLE;
#ifdef GLXSS_ENABLED
                    IPC_Cmd_Send_V3F(IPC_CMD_ARROW_TRAIN, 2);
                    wav_player_play("rest.wav");
#endif
                }
                if (mode == EEG_APP_MODE_COLLECT || mode == EEG_APP_MODE_COLLECT_CSP) {
                    g_ipc_diag_enable = 0;
                }
                uint8_t resp[2] = {CMD_MODE_SET, mode};
                Send_Resp(CMD_MODE_SET_OK, resp, 2);
                dir_phase = DIR_PHASE_REST;
                dir_phase_row_count = 0;
                dir_skip_row_count = 0;
                dir_round_count = 0;
                csp_collect_row_tick = 0;
                csp_window_id = 0;
                Direction_CSPStreamReset();
            } else {
                Send_RespErr(2);
            }
        }
        break;

    case CMD_TRIAL:
        if (g_work_mode != WORK_MODE_TRAIN) {
            Send_RespErr(3);
            break;
        }
        if (g_trial_state == TRIAL_RUNNING) {
            Send_RespErr(4);
            break;
        }
        if (dlen >= 1) {
            uint8_t side = data[0];
            if (side == 0u) {
                g_trial_label = DIR_LABEL_LEFT;
            } else if (side == 1u) {
                g_trial_label = DIR_LABEL_RIGHT;
            } else {
                Send_RespErr(5);
                break;
            }
            g_trial_state = TRIAL_RUNNING;
            g_trial_row_count = 0;
            g_paused = 0;
            g_v5f_active = V5F_ACTIVE_COLLECT;
            DualCore_IPC_RequestV5FReset();
            Direction_ResetTaskZero();
#ifdef GLXSS_ENABLED
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_TRIAL side=%u -> IPC ARROW_TRAIN(%u)\r\n", (unsigned)side, (unsigned)side);
            IPC_Cmd_Send_V3F(IPC_CMD_ARROW_TRAIN, side);
            wav_player_play(side == 0u ? "left.wav" : "right.wav");
#endif
            uint8_t resp[1] = {side};
            Send_Resp(CMD_TASK_START, resp, 1);
        }
        break;

    case CMD_STOP:
        g_work_mode = WORK_MODE_IDLE;
        g_trial_state = TRIAL_IDLE;
        g_paused = 0;
        g_eeg_app_mode = EEG_APP_MODE_COLLECT;
        g_v5f_active = V5F_ACTIVE_IDLE;
        g_ssvep_active = 0;
        DualCore_IPC_SetSsvepEnable(0);
        EEG_FFT_ResetInferState();
        Direction_Infer1sReset();
        Direction_CSPStreamReset();
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_STOP -> IPC RESET\r\n");
        GLXSS_Infer_Stop();
        IPC_Ctrl_SetFlags_V3F(0);
        IPC_Cmd_Send_V3F(IPC_CMD_RESET, 0);
        wav_player_play("rest.wav");
#endif
        Send_RespOk(CMD_TASK_STOPPED);
        break;

    case CMD_GAME_START:
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_GAME_START -> IPC GAME(1)\r\n");
        IPC_Cmd_Send_V3F(IPC_CMD_GAME, 1);
#endif
        Send_RespOk(CMD_GAME_START);
        break;

    case CMD_GAME_STOP:
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_GAME_STOP -> IPC GAME(0)\r\n");
        IPC_Cmd_Send_V3F(IPC_CMD_GAME, 0);
#endif
        Send_RespOk(CMD_GAME_STOP);
        break;

    case CMD_FOCUS_START:
        g_focus_sim_active = 1;
        g_focus_sim_offset = 0.0f;
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_FOCUS_START -> IPC FOCUS(1)\r\n");
        IPC_Cmd_Send_V3F(IPC_CMD_FOCUS, 1);
        wav_player_play("colors.wav");
#endif
        Send_RespOk(CMD_FOCUS_START);
        break;

    case CMD_FOCUS_STOP:
        g_focus_sim_active = 0;
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_FOCUS_STOP -> IPC FOCUS(0)\r\n");
        IPC_Cmd_Send_V3F(IPC_CMD_FOCUS, 0);
        wav_player_stop();
#endif
        Send_RespOk(CMD_FOCUS_STOP);
        break;

    case CMD_STATUS:
        {
            uint8_t resp[6];
            resp[0] = (uint8_t)g_work_mode;
            resp[1] = (uint8_t)g_trial_state;
            resp[2] = (uint8_t)g_trial_row_count;
            resp[3] = 0;
            resp[4] = DualCore_IPC_GetModelSelect();
            resp[5] = (uint8_t)(g_trial_row_count >> 8);
            Send_Resp(CMD_STATUS, resp, 6);
        }
        break;

    case CMD_IPCDIAG:
        if (dlen >= 1) {
            g_ipc_diag_enable = data[0] ? 1 : 0;
            uint8_t resp[2] = {CMD_IPCDIAG, g_ipc_diag_enable};
            Send_Resp(CMD_MODE_SET_OK, resp, 2);
        }
        break;

    case CMD_POSTURE:
#ifdef HAS_ICM42605
        if (dlen >= 1) {
            g_posture_diag_enable = data[0] ? 1 : 0;
            uint8_t resp[2] = {CMD_POSTURE, g_posture_diag_enable};
            Send_Resp(CMD_MODE_SET_OK, resp, 2);
        }
#endif
        break;

    case CMD_DISPLAY_CFG:
        {
            uint8_t n_slots = (dlen / 3u);
            uint8_t s;
            if (n_slots > DISPLAY_NUM_CH) n_slots = DISPLAY_NUM_CH;
            for (s = 0; s < n_slots; s++) {
                uint8_t wch = data[s * 3];
                if (wch < DISPLAY_MAX_CH) g_display_config.wave_ch[s] = wch;
                g_display_config.wave_type[s] = data[s * 3 + 1];
                g_display_config.spec_type[s] = data[s * 3 + 2];
            }
            EEG_FFT_ResetSendState();
#ifdef GLXSS_ENABLED

#endif
            uint8_t resp[24];
            uint16_t resp_len = (uint16_t)(n_slots * 3u);
            memcpy(resp, data, resp_len);
            Send_Resp(CMD_DISPLAY_CFG, resp, resp_len);
        }
        break;

    case CMD_SSVEP_START:
        g_ssvep_active = 1;
        DualCore_IPC_SetSsvepEnable(1);
        DualCore_IPC_RequestSsvepReset();
        if (g_v5f_active == V5F_ACTIVE_IDLE) {
            g_v5f_active = V5F_ACTIVE_INFER;
            DualCore_IPC_RequestV5FReset();
        }
#ifdef GLXSS_ENABLED
        {
            uint8_t freq_idx = 0;
            if (dlen >= 1 && data[0] <= 3) freq_idx = data[0];
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_SSVEP_START freq=%u -> IPC SSVEP\r\n", (unsigned)freq_idx);
            IPC_Ctrl_SetFlags_V3F(IPC_CTRL_SSVEP_ENABLE);
            IPC_Cmd_Send_V3F(IPC_CMD_SSVEP, freq_idx);
            wav_player_play("focus.wav");
        }
#endif
        Send_RespOk(CMD_SSVEP_START);
        break;

    case CMD_SSVEP_STOP:
        g_ssvep_active = 0;
        DualCore_IPC_SetSsvepEnable(0);
        DualCore_IPC_SetSsvepSelftest(0, 0);
        DualCore_IPC_RequestSsvepReset();
        if (g_v5f_active == V5F_ACTIVE_INFER
            && g_eeg_app_mode != EEG_APP_MODE_INFER) {
            g_v5f_active = V5F_ACTIVE_IDLE;
        }
#ifdef GLXSS_ENABLED
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_SSVEP_STOP -> IPC RESET\r\n");
        IPC_Ctrl_SetFlags_V3F(0);
        IPC_Cmd_Send_V3F(IPC_CMD_RESET, 0);
#endif
        Send_RespOk(CMD_SSVEP_STOP);
        break;

    case CMD_SSVEP_SELFTEST_START:
        {
            uint8_t freq_idx = 0;
            if (dlen >= 1 && data[0] <= 3) freq_idx = data[0];
            g_ssvep_active = 1;
            DualCore_IPC_SetSsvepEnable(1);
            DualCore_IPC_SetSsvepSelftest(1, freq_idx);
            DualCore_IPC_RequestSsvepReset();
            if (g_v5f_active == V5F_ACTIVE_IDLE) {
                g_v5f_active = V5F_ACTIVE_INFER;
                DualCore_IPC_RequestV5FReset();
            }
#ifdef GLXSS_ENABLED
            IPC_Ctrl_SetFlags_V3F(IPC_CTRL_SSVEP_ENABLE);
            IPC_Cmd_Send_V3F(IPC_CMD_SSVEP, freq_idx);
#endif
            uint8_t resp[1] = {freq_idx};
            Send_Resp(CMD_SSVEP_SELFTEST_START, resp, 1);
        }
        break;

    case CMD_SSVEP_SELFTEST_STOP:
        DualCore_IPC_SetSsvepSelftest(0, 0);
        DualCore_IPC_RequestSsvepReset();
        Send_RespOk(CMD_SSVEP_SELFTEST_STOP);
        break;

    case CMD_TARGET:
        if (dlen >= 1) {
            uint8_t side = data[0];
#ifdef GLXSS_ENABLED
            g_v5f_active = V5F_ACTIVE_INFER;
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CMD_TARGET side=%u -> IPC ARROW\r\n", (unsigned)side);
            IPC_Cmd_Send_V3F(IPC_CMD_ARROW, side & 1u);
#endif
            {
                uint8_t resp[2] = {CMD_TARGET, side};
                Send_Resp(CMD_MODE_SET_OK, resp, 2);
            }
        }
        break;

    case CMD_INFER_CFG:
        if (dlen >= 1) {
            uint8_t mode = data[0];
            uint8_t rounds = (dlen >= 2) ? data[1] : 3;
#ifdef GLXSS_ENABLED
            GLXSS_Infer_Config(mode, rounds);
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] INFER_CFG mode=%u rounds=%u\r\n",
                          (unsigned)mode, (unsigned)rounds);
#endif
            {
                uint8_t resp[2] = {mode, rounds};
                Send_Resp(CMD_INFER_CFG_OK, resp, 2);
            }
        }
        break;

    case CMD_IMPEDANCE_CHECK:
        {
            ADS1299_EnterImpedanceMode();
            float z_kohm[8];
            ADS1299_MeasureImpedance(z_kohm);
            ADS1299_ExitImpedanceMode();

            uint8_t resp[33];
            for (int c = 0; c < 8; c++) {
                int32_t val_i32 = (int32_t)(z_kohm[c] * 100.0f + 0.5f);
                if (val_i32 < 0) val_i32 = 0;
                resp[c * 4 + 0] = (uint8_t)(val_i32 & 0xFF);
                resp[c * 4 + 1] = (uint8_t)((val_i32 >> 8) & 0xFF);
                resp[c * 4 + 2] = (uint8_t)((val_i32 >> 16) & 0xFF);
                resp[c * 4 + 3] = (uint8_t)((val_i32 >> 24) & 0xFF);
            }
            resp[32] = ADS1299_RecheckBias();
            Send_Resp(CMD_IMPEDANCE_RESULT, resp, 33);
        }
        break;

    case CMD_ALARM_ACK:
        {
            wav_player_set_loop(0);
            wav_player_stop();
#ifdef HAS_ICM42605
            PM_ClearFall();

#endif
            Serial_Printf(SERIAL_PORT_DEBUG, "[ALARM] ack, stopped\r\n");
            uint8_t resp[2] = {CMD_ALARM_ACK, data[0]};
            Pack_Frame(SERIAL_PORT_DEBUG, CMD_ACK, resp, 2);
        }
        break;

    case CMD_RECORD_START:
        {
            if (dlen >= 40) {
                char pid[12] = {0};
                char pname[40] = {0};
                memcpy(pid, data, 8);
                memcpy(pname, data + 8, 32);
                eeg_record_set_patient(pid, pname);
                Serial_Printf(SERIAL_PORT_DEBUG, "[REC] patient=%s %s\r\n", pid, pname);
            }
            int ret = eeg_record_start();
            if (ret == 0) {
                Send_RespOk(CMD_RECORD_START);
            } else {
                uint8_t resp[1] = {(uint8_t)(-ret)};
                Send_Resp(CMD_NULL, resp, 1);
            }
        }
        break;

    case CMD_RECORD_STOP:
        eeg_record_stop();
        Send_RespOk(CMD_RECORD_STOP);
        break;

    case CMD_DIR_WEIGHT:
        {
            DualCore_IPC_UpdateDirWeight(data, dlen);
            Serial_Printf(SERIAL_PORT_DEBUG, "[DIR] weight updated, len=%u\r\n", (unsigned)dlen);
            Send_RespOk(CMD_DIR_WEIGHT);
        }
        break;

    case CMD_DIR_MODEL_PUSH:
        {
            if (dlen >= (uint16_t)(DIR_MODEL_ID_LEN + DIR_MODEL_NAME_LEN + 4u + DIR_MODEL_PAYLOAD_LEN)) {
                DirModelFile_t model;
                memset(&model, 0, sizeof(model));
                const uint8_t *p = data;
                memcpy(model.patient_id, p, DIR_MODEL_ID_LEN); p += DIR_MODEL_ID_LEN;
                memcpy(model.patient_name, p, DIR_MODEL_NAME_LEN); p += DIR_MODEL_NAME_LEN;
                memcpy(&model.balanced_accuracy, p, 4); p += 4;
                memcpy(model.model_data, p, DIR_MODEL_PAYLOAD_LEN);
                model.patient_id[DIR_MODEL_ID_LEN - 1] = '\0';
                model.patient_name[DIR_MODEL_NAME_LEN - 1] = '\0';

                DualCore_IPC_UpdateDirWeight(model.model_data, DIR_MODEL_PAYLOAD_LEN);
                int sv = DirModelSD_Save(&model);
                Serial_Printf(SERIAL_PORT_DEBUG, "[DIR] model push %s %s sd=%d\r\n",
                              model.patient_id, model.patient_name, sv);
                Send_RespOk(CMD_DIR_MODEL_PUSH);
            } else {
                Serial_Printf(SERIAL_PORT_DEBUG, "[DIR] model push len err %u\r\n", (unsigned)dlen);
                Send_RespErr(0xFE);
            }
        }
        break;

    case CMD_DIR_MODEL_SCAN:
        {
            Serial_Printf(SERIAL_PORT_DEBUG, "[DIR] model scan request\r\n");
            DirModelSD_SendList();
            Send_RespOk(CMD_DIR_MODEL_SCAN);
        }
        break;

    default:
        Send_RespErr(0xFF);
        break;
    }
}

