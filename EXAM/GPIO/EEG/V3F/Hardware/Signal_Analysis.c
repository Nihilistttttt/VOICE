#include "Signal_Analysis.h"
#include "eeg_filter.h"
#include "eeg_fft.h"
#include "eeg_direction_feature.h"
#include "eeg_direction_collect.h"

#include "eeg_cmd_parser.h"
#include "eeg_ab_extract.h"
#include "OLED.h"
#include "Serial.h"
#include "Message_Parser.h"
#include "debug.h"
#include "ADS1299.h"

#include "hardware.h"
#include "dir_model_sd.h"
#include "wav_player.h"
#include "eeg_record.h"
#include "ipc_log.h"

#ifdef HAS_ICM42605
#include "ICM42605.h"
#include "Timer_1ms.h"
#include "posture_detect.h"
#include "patient_monitor.h"
#endif

#include "dualcore_ipc.h"
#include "eeg_infer_glxss.h"
#include <math.h>
#include <string.h>
#include <stdio.h>


#define DUALCORE_IPC_FRAME_SHARE_ENABLE 1
#define DUALCORE_IPC_FRAME_NOTIFY_EVERY_FRAMES 1
#define DUALCORE_IPC_FRAME_PRINT_EVERY_ACKS 500
uint8_t g_ipc_diag_enable = 0;
#ifdef HAS_ICM42605
uint8_t g_posture_diag_enable = 0;
#else
volatile uint32_t g_icm42605_ms_tick = 0;
#endif

#define DIR_1S_TEST_TEXT_ONLY 0
#define AB_REALTIME_NOTCH_ENABLE 1
#define AB_EXTRACT_PRINT_ENABLE 0
#define AB_FFT_DETREND_ENABLE 1

uint8_t g_eeg_app_mode = EEG_APP_MODE_COLLECT;
volatile uint8_t g_v5f_active = V5F_ACTIVE_IDLE;
volatile uint8_t g_ssvep_active = 0;

void Signal_Analysis_Start (void) {
    Serial_Init (SERIAL_PORT_DEBUG);
    Serial_Init (SERIAL_PORT_WIFI);


    EEG_FFT_Init();
    DisplayConfig_SetDefaults(&g_display_config);

#ifdef HAS_ICM42605
    ICM42605_Status icm_status = ICM42605_BCI_Init();
    Timer_1ms_Init();
    Serial_Printf (SERIAL_PORT_DEBUG, "ICM42605_INIT=%d\r\n", (int)icm_status);
#ifndef EEG_OLED_DISABLED
    OLED_ShowString(SPI,0,0,"ICM42605 = ");
    OLED_ShowNum(SPI,1,0,(int)icm_status,4);
#endif
#endif

    // Serial_Printf (SERIAL_PORT_DEBUG, "ADS1299_Init start\r\n");
    uint8_t id = ADS1299_Init();
    Serial_Printf (SERIAL_PORT_DEBUG, "ADS1299_Init done, id=%d\r\n", id);
#ifndef EEG_OLED_DISABLED
    OLED_ShowString(SPI,3,0,"ADS1299 id = ");
    OLED_ShowNum(SPI,3,13,id,2);
#endif


#if AB_EXTRACT_PRINT_ENABLE
    Serial_Printf (DIR_TEXT_PORT, "ABCFG,fs=%d,fft=%d,step=%d,alpha=8-13Hz,beta=13-30Hz,drift_k=9960/10000,notch=%d,detrend=%d,power_scale=1e15,pct_scale=10000,db_scale=100\r\n",
                   (int)SAMPLE_RATE, FFT_SIZE, STEP_SIZE, AB_REALTIME_NOTCH_ENABLE, AB_FFT_DETREND_ENABLE);
    Serial_Printf (DIR_TEXT_PORT, "ABCSV,ch0_alpha_p15,ch1_alpha_p15,ch0_beta_p15,ch1_beta_p15,ch0_alpha_pct,ch1_alpha_pct,ch0_beta_pct,ch1_beta_pct,ch0_alpha_db100,ch1_alpha_db100,ch0_beta_db100,ch1_beta_db100,alpha_lr,beta_lr,ch0_beta_alpha,ch1_beta_alpha,total_lr\r\n");
#endif

    {
        uint8_t ann[1];
        ann[0] = (uint8_t)g_eeg_app_mode;
        Pack_Frame(SERIAL_PORT_DEBUG, CMD_ANNOUNCE, ann, 1);
        Pack_Frame(SERIAL_PORT_WIFI, CMD_ANNOUNCE, ann, 1);
    }

    {
        int sd_ret = DirModelSD_Init();
        Serial_Printf(SERIAL_PORT_DEBUG, "[DIRMOD] init=%d\r\n", sd_ret);
        if (sd_ret == 0) {
            Delay_Ms(200);
            DirModelSD_SendList();
        }
    }

    uint8_t frame_buf[ADS1299_FRAME_BYTE_NUM];
    int32_t ch_data[ADS1299_CHANNEL_NUM];
    static uint8_t fft_active = 0;

#if DUALCORE_IPC_FRAME_SHARE_ENABLE
    static uint32_t ipc_frame_count = 0;
    static uint32_t ipc_last_print_ack = 0;
#endif

    while (1) {
#ifndef HAS_ICM42605

        g_icm42605_ms_tick++;
#endif
#if CMD_MODE_ENABLE
        Retry_Tick();
        Parse_Serial_Data (SERIAL_PORT_DEBUG);
        Parse_Serial_Data (SERIAL_PORT_WIFI);
#endif

        if (ring_buffer_get_frame (frame_buf)) {

            eeg_record_add_frame(frame_buf);

#if DUALCORE_IPC_FRAME_SHARE_ENABLE
            ipc_frame_count++;
            if (ipc_frame_count >= DUALCORE_IPC_FRAME_NOTIFY_EVERY_FRAMES) {
                ipc_frame_count = 0;
                if (g_v5f_active != V5F_ACTIVE_IDLE) {
                    DualCore_IPC_SendFrameFromV3F (frame_buf, ADS1299_FRAME_BYTE_NUM);
                }

                if (g_ipc_diag_enable) {
                    static uint32_t ipc_diag_count = 0;
                    ipc_diag_count++;
                    if ((ipc_diag_count % 50u) == 0u) {
                        uint8_t diag_buf[59];
                        uint32_t l0 = DualCore_IPC_GetAckCount();
                        uint32_t l1 = DualCore_IPC_GetNotifyCount();
                        uint32_t l2 = DualCore_IPC_GetParseOKCount();
                        uint32_t l3 = DualCore_IPC_GetParseBadCount();
                        uint32_t l4 = DualCore_IPC_GetV5FHandlerCount();
                        uint32_t l5 = DualCore_IPC_GetLastV5FWfiWake();
                        uint32_t l6 = (uint32_t)IPC->ENA;
                        uint32_t l7 = (uint32_t)IPC->STS;
                        uint32_t l8 = (uint32_t)IPC->ISR;
                        uint32_t l9 = IPC_LOG_SHARED->ssvep_send_us;
                        uint32_t l10 = IPC_LOG_SHARED->ssvep_late_us;
                        uint32_t l11 = IPC_LOG_SHARED->ssvep_frame_cnt;
                        uint8_t  l12 = IPC_LOG_SHARED->ssvep_mode_active;
                        uint32_t l13 = IPC_LOG_SHARED->ssvep_rp_us;
                        uint32_t l14 = IPC_LOG_SHARED->ssvep_rp_count;
                        diag_buf[0] = DIAG_TYPE_IPCDIAG;
                        memcpy(diag_buf + 1, &l0, 4);
                        memcpy(diag_buf + 5, &l1, 4);
                        memcpy(diag_buf + 9, &l2, 4);
                        memcpy(diag_buf + 13, &l3, 4);
                        memcpy(diag_buf + 17, &l4, 4);
                        memcpy(diag_buf + 21, &l5, 4);
                        diag_buf[25] = (uint8_t)g_v5f_active;
                        memcpy(diag_buf + 26, &l6, 4);
                        memcpy(diag_buf + 30, &l7, 4);
                        memcpy(diag_buf + 34, &l8, 4);
                        memcpy(diag_buf + 38, &l9, 4);
                        memcpy(diag_buf + 42, &l10, 4);
                        memcpy(diag_buf + 46, &l11, 4);
                        diag_buf[50] = l12;
                        memcpy(diag_buf + 51, &l13, 4);
                        memcpy(diag_buf + 55, &l14, 4);
                        Pack_Frame(SERIAL_PORT_DEBUG, CMD_DIAG, diag_buf, sizeof(diag_buf));
                        Pack_Frame(SERIAL_PORT_WIFI, CMD_DIAG, diag_buf, sizeof(diag_buf));
                    }
                }

                uint32_t ipc_ack = DualCore_IPC_GetAckCount();
                if ((ipc_ack - ipc_last_print_ack) >= DUALCORE_IPC_FRAME_PRINT_EVERY_ACKS) {
                    ipc_last_print_ack = ipc_ack;
                }
                (void)ipc_ack;
            }
#endif

            ADS1299_ParseRawFrame (frame_buf, NULL, ch_data);


            float raw_vals[NUM_CHANNELS];
            raw_vals[0] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_OZ], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[1] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_O1], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[2] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_F3], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[3] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_F4], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[4] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_CP3], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[5] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_CP4], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[6] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_C3], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);
            raw_vals[7] = ADS1299_CodeToVolt (ch_data[ADS1299_EEG_CH_C4], ADS1299_VREF_DEFAULT, ADS1299_GAIN_DEFAULT);

            float baseline_vals[NUM_CHANNELS];
            Waveform_RemoveDisplayBaseline_8CH (raw_vals, baseline_vals);

            float drift_vals[NUM_CHANNELS];
            drift_vals[0] = EEG_RemoveRealtimeDrift (raw_vals[0], &AB_Drift_CH0);
            drift_vals[1] = EEG_RemoveRealtimeDrift (raw_vals[1], &AB_Drift_CH1);
            drift_vals[2] = EEG_RemoveRealtimeDrift (raw_vals[2], &AB_Drift_CH2);
            drift_vals[3] = EEG_RemoveRealtimeDrift (raw_vals[3], &AB_Drift_CH3);
            drift_vals[4] = EEG_RemoveRealtimeDrift (raw_vals[4], &AB_Drift_CH4);
            drift_vals[5] = EEG_RemoveRealtimeDrift (raw_vals[5], &AB_Drift_CH5);
            drift_vals[6] = EEG_RemoveRealtimeDrift (raw_vals[6], &AB_Drift_CH6);
            drift_vals[7] = EEG_RemoveRealtimeDrift (raw_vals[7], &AB_Drift_CH7);

#if AB_REALTIME_NOTCH_ENABLE
            float ab_pre_vals[NUM_CHANNELS];
            ab_pre_vals[0] = IIR_SOS_Step (drift_vals[0], &g_notch_coeff, &g_notch_state[0]);
            ab_pre_vals[1] = IIR_SOS_Step (drift_vals[1], &g_notch_coeff, &g_notch_state[1]);
            ab_pre_vals[2] = IIR_SOS_Step (drift_vals[2], &g_notch_coeff, &g_notch_state[2]);
            ab_pre_vals[3] = IIR_SOS_Step (drift_vals[3], &g_notch_coeff, &g_notch_state[3]);
            ab_pre_vals[4] = IIR_SOS_Step (drift_vals[4], &g_notch_coeff, &g_notch_state[4]);
            ab_pre_vals[5] = IIR_SOS_Step (drift_vals[5], &g_notch_coeff, &g_notch_state[5]);
            ab_pre_vals[6] = IIR_SOS_Step (drift_vals[6], &g_notch_coeff, &g_notch_state[6]);
            ab_pre_vals[7] = IIR_SOS_Step (drift_vals[7], &g_notch_coeff, &g_notch_state[7]);
#else
            float ab_pre_vals[NUM_CHANNELS];
            ab_pre_vals[0] = drift_vals[0];
            ab_pre_vals[1] = drift_vals[1];
            ab_pre_vals[2] = drift_vals[2];
            ab_pre_vals[3] = drift_vals[3];
            ab_pre_vals[4] = drift_vals[4];
            ab_pre_vals[5] = drift_vals[5];
            ab_pre_vals[6] = drift_vals[6];
            ab_pre_vals[7] = drift_vals[7];
#endif

            float filt_vals[NUM_CHANNELS];
            filt_vals[0] = DisplayConfig_NeedsBandpass(0u) ?
                           IIR_SOS_Step (ab_pre_vals[0], &g_bandpass_coeff, &g_bandpass_state[0]) : 0.0f;
            filt_vals[1] = DisplayConfig_NeedsBandpass(1u) ?
                           IIR_SOS_Step (ab_pre_vals[1], &g_bandpass_coeff, &g_bandpass_state[1]) : 0.0f;
            filt_vals[2] = IIR_SOS_Step (ab_pre_vals[2], &g_bandpass_coeff, &g_bandpass_state[2]);
            filt_vals[3] = IIR_SOS_Step (ab_pre_vals[3], &g_bandpass_coeff, &g_bandpass_state[3]);
            filt_vals[4] = IIR_SOS_Step (ab_pre_vals[4], &g_bandpass_coeff, &g_bandpass_state[4]);
            filt_vals[5] = IIR_SOS_Step (ab_pre_vals[5], &g_bandpass_coeff, &g_bandpass_state[5]);
            filt_vals[6] = IIR_SOS_Step (ab_pre_vals[6], &g_bandpass_coeff, &g_bandpass_state[6]);
            filt_vals[7] = IIR_SOS_Step (ab_pre_vals[7], &g_bandpass_coeff, &g_bandpass_state[7]);

#if !DIR_1S_TEST_TEXT_ONLY
            {
                uint8_t wt;
                for (wt = 0; wt < 3u; wt++) {
                    int need = 0;
                    for (int wi = 0; wi < DISPLAY_NUM_CH; wi++) {
                        if (g_display_config.wave_type[wi] == wt) {
                            need = 1;
                            break;
                        }
                    }
                    if (!need) continue;
                    float batch[DISPLAY_MAX_CH];
                    for (int ci = 0; ci < DISPLAY_MAX_CH; ci++) {
                        switch (wt) {
                        case WAVE_TYPE_FILT:     batch[ci] = filt_vals[ci]; break;
                        case WAVE_TYPE_BASELINE: batch[ci] = baseline_vals[ci]; break;
                        default:                 batch[ci] = raw_vals[ci]; break;
                        }
                    }
                    Send_WaveformBatch(wt, batch);
                }
            }
#endif


            if (DisplayConfig_NeedsBandpass(0u))
                RingBufFiltered.CH0[RingBufFiltered.WriteIdx] = filt_vals[0];
            if (DisplayConfig_NeedsBandpass(1u))
                RingBufFiltered.CH1[RingBufFiltered.WriteIdx] = filt_vals[1];
            RingBufFiltered.CH2[RingBufFiltered.WriteIdx] = filt_vals[2];
            RingBufFiltered.CH3[RingBufFiltered.WriteIdx] = filt_vals[3];
            RingBufFiltered.CH4[RingBufFiltered.WriteIdx] = filt_vals[4];
            RingBufFiltered.CH5[RingBufFiltered.WriteIdx] = filt_vals[5];
            RingBufFiltered.CH6[RingBufFiltered.WriteIdx] = filt_vals[6];
            RingBufFiltered.CH7[RingBufFiltered.WriteIdx] = filt_vals[7];
            RingBufFiltered.WriteIdx = (RingBufFiltered.WriteIdx + 1) % FFT_SIZE;
            if (csp_filtered_sample_count < 0xFFFFFFFFUL) {
                csp_filtered_sample_count++;
            }

            RingBuf.CH0[RingBuf.WriteIdx] = ab_pre_vals[0];
            RingBuf.CH1[RingBuf.WriteIdx] = ab_pre_vals[1];
            RingBuf.CH2[RingBuf.WriteIdx] = ab_pre_vals[2];
            RingBuf.CH3[RingBuf.WriteIdx] = ab_pre_vals[3];
            RingBuf.CH4[RingBuf.WriteIdx] = ab_pre_vals[4];
            RingBuf.CH5[RingBuf.WriteIdx] = ab_pre_vals[5];
            RingBuf.CH6[RingBuf.WriteIdx] = ab_pre_vals[6];
            RingBuf.CH7[RingBuf.WriteIdx] = ab_pre_vals[7];
            RingBuf.WriteIdx = (RingBuf.WriteIdx + 1) % FFT_SIZE;

            Update_Waveform (baseline_vals[g_display_config.wave_ch[0]]);

            New_Samples_Count++;
            if (New_Samples_Count >= STEP_SIZE && !fft_active) {
                Process_FFT();
                fft_active = 1;
            }

            if (fft_active) {
                if (Process_FFT_Step()) {
                    fft_active = 0;
                    New_Samples_Count = 0;
                }
            }


            /* One CSP protocol item per ADS frame prevents TX-ring overflow. */
            Direction_CSPStreamTask();

            EEG_MI_ResultPoll();
            EEG_SSVEP_ResultPoll();
            EEG_Game_ResultPoll();
            GLXSS_Infer_Poll();

        } else if (g_ssvep_active && DualCore_IPC_GetSsvepSelftest()) {
            static uint32_t s_selftest_last_tick = 0;
            extern uint32_t SystemCoreClock;
            uint32_t cycle;
            __asm__ volatile("rdcycle %0" : "=r"(cycle));
            uint32_t now_tick = cycle / (SystemCoreClock / 1000);
            if ((uint32_t)(now_tick - s_selftest_last_tick) >= 4u) {
                s_selftest_last_tick = now_tick;
                memset(frame_buf, 0, ADS1299_FRAME_BYTE_NUM);
                if (g_v5f_active != V5F_ACTIVE_IDLE) {
                    DualCore_IPC_SendFrameFromV3F(frame_buf, ADS1299_FRAME_BYTE_NUM);
                }
                EEG_SSVEP_ResultPoll();
            }
        }

#ifdef HAS_ICM42605
        ICM42605_Task();

        {
            int32_t roll_deg10, pitch_deg10, yaw_deg10;
            ICM42605_GetAngleDeg10(&roll_deg10, &pitch_deg10, &yaw_deg10);
            IPC_LOG_SHARED->v3f_pitch_deg10 = pitch_deg10;
        }

        {
            static uint32_t posture_last_print_tick = 0;
            static Posture_t s_last_wifi_posture = POSTURE_UNKNOWN;
            const PostureResult_t *pr = Posture_GetResult();
            if (g_posture_diag_enable) {
                uint32_t now_tick = g_icm42605_ms_tick;
                if ((uint32_t)(now_tick - posture_last_print_tick) >= 1000u) {
                    int32_t roll_deg10;
                    int32_t pitch_deg10;
                    int32_t yaw_deg10;
                    posture_last_print_tick = now_tick;
                    {
                        uint8_t diag_buf[22];
                        int32_t gx = (int32_t)(pr->gravity_x * 1000.0f);
                        int32_t gy = (int32_t)(pr->gravity_y * 1000.0f);
                        int32_t gz = (int32_t)(pr->gravity_z * 1000.0f);
                        int32_t conf = (int32_t)(pr->confidence * 100.0f);
                        diag_buf[0] = DIAG_TYPE_POSTURE;
                        diag_buf[1] = (uint8_t)pr->posture;
                        memcpy(diag_buf + 2, &gx, 4);
                        memcpy(diag_buf + 6, &gy, 4);
                        memcpy(diag_buf + 10, &gz, 4);
                        memcpy(diag_buf + 14, &conf, 4);
                        memcpy(diag_buf + 18, &pr->stable_ms, 4);
                        Pack_Frame(SERIAL_PORT_WIFI, CMD_DIAG, diag_buf, sizeof(diag_buf));
                    }

                    /*
                     * WIFI keeps the original POSTURE gravity packet exactly
                     * as before. DEBUG/Python receives only readable angles;
                     * no periodic gx/gy/gz packet is duplicated to DEBUG.
                     * Binary: WIFI=CMD_DIAG/POSTURE, DEBUG=CMD_DIAG/POSTURE_ANGLE.
                     */
                    ICM42605_GetAngleDeg10 (&roll_deg10, &pitch_deg10, &yaw_deg10);
                    {
                        uint8_t diag_buf[13];
                        diag_buf[0] = DIAG_TYPE_POSTURE_ANGLE;
                        memcpy(diag_buf + 1, &roll_deg10, 4);
                        memcpy(diag_buf + 5, &pitch_deg10, 4);
                        memcpy(diag_buf + 9, &yaw_deg10, 4);
                        Pack_Frame(SERIAL_PORT_DEBUG, CMD_DIAG, diag_buf, sizeof(diag_buf));
                    }
                }
            }
            if (pr->posture != POSTURE_UNKNOWN && pr->posture != s_last_wifi_posture) {
                s_last_wifi_posture = pr->posture;
                const PM_Result_t *pm = PM_GetResult();
                uint8_t payload[7];
                payload[0] = Retry_GetSeq();
                payload[1] = EVENT_POSTURE_STATE;
                payload[2] = (uint8_t)pr->posture;
                memcpy(payload + 3, &pm->turn_count, 4);
                Pack_Frame(SERIAL_PORT_DEBUG, CMD_EVENT, payload, sizeof(payload));
                Pack_Frame(SERIAL_PORT_WIFI, CMD_EVENT, payload, sizeof(payload));
                Retry_StoreEx(payload, sizeof(payload), CMD_EVENT, 20);
            }
        }

        if (g_icm42605_pm_event_pending) {
            g_icm42605_pm_event_pending = 0u;
            const PM_Result_t *pm = PM_GetResult();
            switch ((PM_Event_t)g_icm42605_pm_event_type) {
            case PM_EVENT_TURN: {
                uint8_t payload[12];
                payload[0] = Retry_GetSeq();
                payload[1] = EVENT_TURN;
                memcpy(payload + 2, &pm->turn_count, 4);
                payload[6] = (uint8_t)pm->turn_from;
                payload[7] = (uint8_t)pm->turn_to;
                memcpy(payload + 8, &pm->last_turn_tick, 4);
                Pack_Frame(SERIAL_PORT_DEBUG, CMD_EVENT, payload, sizeof(payload));
                Pack_Frame(SERIAL_PORT_WIFI, CMD_EVENT, payload, sizeof(payload));
                Retry_StoreEx(payload, sizeof(payload), CMD_EVENT, 20);
                break;
            }
            case PM_EVENT_FALL: {
                uint8_t payload[15];
                int32_t gyro = (int32_t)(pm->fall_peak_gyro * 100.0f);
                int32_t acc = (int32_t)(pm->fall_peak_acc * 100.0f);
                payload[0] = Retry_GetSeq();
                payload[1] = EVENT_FALL;
                memcpy(payload + 2, &gyro, 4);
                memcpy(payload + 6, &acc, 4);
                payload[10] = (uint8_t)pm->fall_posture;
                memcpy(payload + 11, &pm->fall_tick, 4);
                Pack_Frame(SERIAL_PORT_DEBUG, CMD_EVENT, payload, sizeof(payload));
                Pack_Frame(SERIAL_PORT_WIFI, CMD_EVENT, payload, sizeof(payload));
                Retry_StoreEx(payload, sizeof(payload), CMD_EVENT, 20);
                wav_player_play("fall.wav");
                wav_player_set_loop(1);
                break;
            }
            case PM_EVENT_NO_TURN: {
                uint8_t payload[6];
                uint32_t duration_min = pm->no_turn_duration_ms / 60000u;
                payload[0] = Retry_GetSeq();
                payload[1] = EVENT_NO_TURN;
                memcpy(payload + 2, &duration_min, 4);
                Pack_Frame(SERIAL_PORT_DEBUG, CMD_EVENT, payload, sizeof(payload));
                Pack_Frame(SERIAL_PORT_WIFI, CMD_EVENT, payload, sizeof(payload));
                Retry_StoreEx(payload, sizeof(payload), CMD_EVENT, 20);
                PM_ClearNoTurnAlert();
                break;
            }
            default:
                break;
            }
        }
#endif /* HAS_ICM42605 */

        if (!ring_buffer_has_frame()) {
            wav_player_poll();
            eeg_record_poll();
        }
    }
}
