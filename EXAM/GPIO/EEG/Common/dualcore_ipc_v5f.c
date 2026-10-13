#include "dualcore_ipc.h"
#include "dualcore_ipc_shared.h"
#include "dualcore_v5f_dsp.h"
#include "dualcore_v5f_ssvep.h"
#include <string.h>
#include <math.h>

#if defined(Core_V5F) && DUALCORE_IPC_RUNTIME_ENABLE

static volatile uint32_t g_ipc_v5f_recv_count      = 0;
static volatile uint32_t g_ipc_v5f_last_seq        = 0;
static volatile uint32_t g_ipc_v5f_last_checksum   = 0;
volatile uint32_t g_ipc_v5f_window_count    = 0;
volatile uint16_t g_ipc_v5f_window_samples  = 0;
static volatile uint8_t  g_ipc_v5f_ready           = 0;
static volatile uint8_t  g_ipc_v5f_pending_flag    = 0;
static volatile uint32_t g_ipc_v5f_pending_share_addr = 0;
static volatile uint8_t  g_ipc_v5f_last_frame[DUALCORE_IPC_FRAME_LEN];
static volatile uint32_t g_ipc_v5f_handler_count = 0;
static volatile uint8_t  g_ipc_v5f_ack_ready = 0;
static volatile uint32_t g_ipc_v5f_ack_data = 0;

volatile uint32_t g_ipc_v5f_wfi_wake_count = 0;

#ifndef V5F_MODE_GLXSS
void IPC_CH0_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
#endif

static void DualCore_V5F_SendAckNow(void)
{
    if (!g_ipc_v5f_ack_ready) {
        return;
    }

    IPC_WriteMSG(IPC_MSG0, g_ipc_v5f_ack_data);
    DUALCORE_FENCE();
    IPC_ITConfig(IPC_CH0, IPC_CH_Sta_Bit0, DISABLE);
    IPC_ITConfig(IPC_CH0, IPC_CH_Sta_Bit0, ENABLE);
    IPC_SetFlagStatus(IPC_CH0, IPC_CH_Sta_Bit0);
    g_ipc_v5f_ack_ready = 0u;
}

static float s_v5f_selftest_phase = 0.0f;

static int32_t DualCore_FloatVoltToMicroVoltX1000(float volt)
{
    float v = volt * 1000000000.0f;
    if (v >= 0.0f) {
        v += 0.5f;
    } else {
        v -= 0.5f;
    }
    return (int32_t)v;
}

static void DualCore_V5F_ProcessPreprocess(volatile DualCore_IPC_FrameSlot_t *slot,
                                           int32_t ch_data[DUALCORE_ADS1299_CHANNEL_NUM])
{
    uint8_t c;
    uint8_t i;

    if (slot->control_reserved[0] & DUALCORE_IPC_CTRL_RESET_DSP) {
        DualCore_V5F_DSP_Reset();
        slot->control_reserved[0] &= ~DUALCORE_IPC_CTRL_RESET_DSP;
    }
    if (slot->control_reserved[0] & DUALCORE_IPC_CTRL_RESET_SSVEP) {
        DualCore_V5F_SSVEP_Reset();
        s_v5f_selftest_phase = 0.0f;
        slot->control_reserved[0] &= ~DUALCORE_IPC_CTRL_RESET_SSVEP;
    }

    uint8_t ssvep_selftest_idx = slot->control_reserved[1] & 0x03u;

    g_ipc_v5f_window_samples++;
    g_v5f_step_count++;
    if (g_ipc_v5f_window_samples >= DUALCORE_V5F_WINDOW_SIZE) {
        g_ipc_v5f_window_samples = 0;
        g_ipc_v5f_window_count++;
    }

    if (slot->model_select <= DUALCORE_V5F_MODEL_SELECT_CSP) {
        g_ipc_v5f_model_select = slot->model_select;
    } else {
        g_ipc_v5f_model_select = DUALCORE_V5F_MODEL_SELECT_AUTO;
    }

    slot->v5f_sample_count = g_ipc_v5f_recv_count;
    slot->v5f_window_count = g_ipc_v5f_window_count;

    for (c = 0; c < DUALCORE_ADS1299_ACTIVE_CH_NUM; c++) {
        uint8_t src_ch = g_v5f_ch_map[c];
        int32_t uv_x1000 = DualCore_ADS1299_CodeToMicroVoltX1000(ch_data[src_ch]);
        float volt = (float)uv_x1000 / 1000000000.0f;
        float drift = DualCore_RemoveRealtimeDrift(volt, &g_v5f_drift[c]);
        float pre = DualCore_IIR_SOS_Step(drift, &g_v5f_notch_coeff, &g_v5f_notch_state[c]);
        float filt = DualCore_IIR_SOS_Step(pre, &g_v5f_bandpass_coeff, &g_v5f_bandpass_state[c]);
        float csp_filt = DualCore_IIR_SOS_Step(pre,
                                               &g_v5f_csp_bandpass_coeff,
                                               &g_v5f_csp_bandpass_state[c]);

        slot->v5f_uv_x1000[c] = uv_x1000;
        slot->v5f_pre_x1000[c] = DualCore_FloatVoltToMicroVoltX1000(pre);
        slot->v5f_filt_x1000[c] = DualCore_FloatVoltToMicroVoltX1000(filt);

        g_v5f_ring[c][g_v5f_ring_write_idx] = filt;
        g_v5f_csp_ring[c][g_v5f_ring_write_idx] = csp_filt;
    }

    g_v5f_ring_write_idx = (uint16_t)((g_v5f_ring_write_idx + 1u) & (DUALCORE_V5F_FFT_SIZE - 1u));
    if (g_v5f_ring_valid_count < DUALCORE_V5F_FFT_SIZE) {
        g_v5f_ring_valid_count++;
    }

    if ((g_v5f_ring_valid_count >= DUALCORE_V5F_FFT_SIZE) &&
        (g_v5f_step_count >= DUALCORE_V5F_STEP_SIZE)) {
        g_v5f_step_count = 0;
        DualCore_V5F_ComputeFFTFeature();
    }

    slot->v5f_feature_valid = g_ipc_v5f_feature_valid;
    slot->v5f_fft_count = g_ipc_v5f_fft_count;
    for (i = 0; i < DUALCORE_V5F_FEATURE_DIM; i++) {
        slot->v5f_feature_q[i] = g_ipc_v5f_feature_q[i];
    }
    slot->v5f_infer_valid = g_ipc_v5f_infer_valid;
    slot->v5f_pred = g_ipc_v5f_pred;
    slot->v5f_model_trained = g_ipc_v5f_model_trained;
    slot->v5f_model_used = g_ipc_v5f_model_used;
    slot->v5f_score_left = g_ipc_v5f_score_left;
    slot->v5f_score_right = g_ipc_v5f_score_right;
    slot->v5f_confidence = g_ipc_v5f_confidence;
    slot->v5f_infer_count = g_ipc_v5f_infer_count;
    slot->control_reserved[1] = 0u;
    slot->control_reserved[2] = (uint8_t)(g_ipc_v5f_wfi_wake_count & 0xFFu);

    {
        int32_t o1_uv = DualCore_ADS1299_CodeToMicroVoltX1000(ch_data[4]);
        int32_t oz_uv = DualCore_ADS1299_CodeToMicroVoltX1000(ch_data[3]);
        float o1_mv = (float)o1_uv / 1000000.0f;
        float oz_mv = (float)oz_uv / 1000000.0f;
        slot->ssvep_o1_uv_x1000 = o1_uv;
        slot->ssvep_oz_uv_x1000 = oz_uv;
        if (slot->control_reserved[0] & DUALCORE_IPC_CTRL_SSVEP_ENABLE) {
            if (slot->control_reserved[0] & DUALCORE_IPC_CTRL_SSVEP_SELFTEST) {
                static const float s_selftest_fs = 250.0f;
                static const float s_selftest_pi = 3.14159265358979323846f;
                static const float s_selftest_freqs[4] = {11.0f, 13.0f, 15.0f, 17.0f};
                uint8_t freq_idx = ssvep_selftest_idx;
                float freq = s_selftest_freqs[freq_idx];
                float phase = s_v5f_selftest_phase;
                o1_mv = 0.020f * sinf(2.0f * s_selftest_pi * freq * phase / s_selftest_fs)
                       + 0.010f * sinf(2.0f * s_selftest_pi * 2.0f * freq * phase / s_selftest_fs + 0.25f)
                       + 0.004f * sinf(2.0f * s_selftest_pi * 9.0f * phase / s_selftest_fs);
                oz_mv = 0.015f * sinf(2.0f * s_selftest_pi * freq * phase / s_selftest_fs + 0.4f)
                       + 0.008f * sinf(2.0f * s_selftest_pi * 2.0f * freq * phase / s_selftest_fs + 0.6f)
                       + 0.003f * sinf(2.0f * s_selftest_pi * 9.0f * phase / s_selftest_fs + 0.2f);
                s_v5f_selftest_phase += 1.0f;
                if (s_v5f_selftest_phase >= s_selftest_fs) s_v5f_selftest_phase -= s_selftest_fs;
                slot->ssvep_o1_uv_x1000 = (int32_t)(o1_mv * 1000000.0f);
                slot->ssvep_oz_uv_x1000 = (int32_t)(oz_mv * 1000000.0f);
            }
            DualCore_V5F_SSVEP_PushSample(o1_mv, oz_mv);
        } else {
            g_ipc_ssvep_valid = 0;
        }
    }

    slot->ssvep_valid = g_ipc_ssvep_valid;
    slot->ssvep_raw_index = g_ipc_ssvep_raw_index;

    slot->ssvep_ratio_q10000 = g_ipc_ssvep_ratio_q10000;
    slot->ssvep_best_score_q10000 = g_ipc_ssvep_best_score_q10000;
    slot->ssvep_margin_q10000 = g_ipc_ssvep_margin_q10000;
    {
        uint8_t t;
        for (t = 0; t < 4; t++) {
            slot->ssvep_scores_q10000[t] = g_ipc_ssvep_scores_q10000[t];
        }
    }
    slot->ssvep_sequence = g_ipc_ssvep_sequence;
}

void DualCore_V5F_MainLoopProcess(void)
{
    uint32_t share_addr;
    uint16_t checksum = 0;
    uint32_t seq = 0;
    uint32_t latest_seq = 0;
    uint32_t start_seq = 0;
    uint32_t latest_idx = 0;
    uint32_t status = 0;
    int32_t ch_data[DUALCORE_ADS1299_CHANNEL_NUM];
    uint16_t i;
    uint16_t c;
    uint8_t processed_any = 0u;

    if (!g_ipc_v5f_pending_flag) {
        return;
    }

    g_ipc_v5f_pending_flag = 0u;
    share_addr = g_ipc_v5f_pending_share_addr;

    if (share_addr < 0x20110000u || share_addr > 0x2017FFFFu) {
        share_addr = 0u;
    }

    if (g_ipc_v5f_ready && share_addr != 0u) {
        volatile DualCore_IPC_FrameSlot_t *latest_slot = (volatile DualCore_IPC_FrameSlot_t *)share_addr;
        volatile DualCore_IPC_FrameSlot_t *base_slot;

        DUALCORE_FENCE();

        latest_seq = latest_slot->seq;
        latest_idx = latest_seq % DUALCORE_IPC_FRAME_SLOT_NUM;
        base_slot = latest_slot - latest_idx;

        {
            volatile DualCore_IPC_DirWeight_t *weight_slot =
                (volatile DualCore_IPC_DirWeight_t *)(base_slot + DUALCORE_IPC_FRAME_SLOT_NUM);
            DUALCORE_FENCE();
            if (weight_slot->update_flag) {
                DualCore_V5F_UpdateDirWeight(weight_slot);
                DUALCORE_FENCE();
                weight_slot->update_flag = 0u;
            }
        }

        start_seq = g_ipc_v5f_last_seq + 1u;
        if (latest_seq >= DUALCORE_IPC_FRAME_SLOT_NUM) {
            uint32_t oldest_valid_seq = latest_seq - DUALCORE_IPC_FRAME_SLOT_NUM + 1u;
            if (start_seq < oldest_valid_seq) {
                start_seq = oldest_valid_seq;
            }
        }

        for (seq = start_seq; seq <= latest_seq; seq++) {
            volatile DualCore_IPC_FrameSlot_t *slot = &base_slot[seq % DUALCORE_IPC_FRAME_SLOT_NUM];

            DUALCORE_FENCE();
            if (slot->seq != seq) {
                continue;
            }

            for (i = 0; i < DUALCORE_IPC_FRAME_LEN; i++) {
                g_ipc_v5f_last_frame[i] = slot->frame[i];
            }

            checksum = DualCore_IPC_Checksum16(g_ipc_v5f_last_frame, DUALCORE_IPC_FRAME_LEN);
            DualCore_ADS1299_ParseRawFrame(g_ipc_v5f_last_frame, &status, ch_data);

            g_ipc_v5f_recv_count++;

            slot->v5f_status = status;
            for (c = 0; c < DUALCORE_ADS1299_ACTIVE_CH_NUM; c++) {
                slot->v5f_ch_code[c] = ch_data[g_v5f_ch_map[c]];
            }

            DualCore_V5F_ProcessPreprocess(slot, ch_data);

            DUALCORE_FENCE();
            slot->v5f_parse_valid = 1u;

            g_ipc_v5f_last_seq = seq;
            g_ipc_v5f_last_checksum = checksum;
            processed_any = 1u;
        }

        if (!processed_any) {
            seq = latest_seq;
            checksum = 0xDDDDu;
        } else {
            seq = g_ipc_v5f_last_seq;
            checksum = (uint16_t)g_ipc_v5f_last_checksum;
        }
    } else {
        checksum = 0xEEEEu;
        seq = 0u;
    }

    g_ipc_v5f_ack_data = (((uint32_t)checksum) << 16) | (seq & 0x0000FFFFu);
    g_ipc_v5f_ack_ready = 1u;
    DualCore_V5F_SendAckNow();
}

void DualCore_IPC_Init_V5F(void)
{
    uint16_t i;

    g_ipc_v5f_recv_count = 0;
    g_ipc_v5f_last_seq = 0;
    g_ipc_v5f_last_checksum = 0;
    g_ipc_v5f_ready = 0;
    g_ipc_v5f_window_count = 0;
    g_ipc_v5f_window_samples = 0;
    g_ipc_v5f_pending_flag = 0;
    g_ipc_v5f_pending_share_addr = 0;
    g_ipc_v5f_ack_ready = 0u;
    g_ipc_v5f_ack_data = 0u;

    for (i = 0; i < DUALCORE_IPC_FRAME_LEN; i++) {
        g_ipc_v5f_last_frame[i] = 0;
    }

    DualCore_V5F_DSP_Reset();
    DualCore_V5F_SSVEP_Init();

    IPC_ClearFlagStatus(IPC_CH0, IPC_CH_Sta_Bit1);
    NVIC_ClearPendingIRQ(IPC_CH0_IRQn);

    NVIC_SetPriority(IPC_CH0_IRQn, (2 << 5) | (0 << 4));
    NVIC_EnableIRQ(IPC_CH0_IRQn);

    g_ipc_v5f_ready = 1;
}

#ifndef V5F_MODE_GLXSS
void IPC_CH0_Handler(void)
{
    if (IPC_GetITStatus(IPC_CH0, IPC_CH_Sta_Bit1) != RESET) {
        g_ipc_v5f_handler_count++;
        g_ipc_v5f_wfi_wake_count++;
        IPC_WriteMSG(IPC_MSG2, g_ipc_v5f_handler_count);

        uint32_t addr = IPC_ReadMSG(IPC_MSG0);
        if (addr >= 0x20110000u && addr <= 0x2017FFFFu) {
            g_ipc_v5f_pending_share_addr = addr;
            g_ipc_v5f_pending_flag = 1u;
        }
    }

    IPC_ITConfig(IPC_CH0, IPC_CH_Sta_Bit1, DISABLE);

    DualCore_V5F_SendAckNow();
}
#endif

#endif
