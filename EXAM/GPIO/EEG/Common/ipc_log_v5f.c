#include "ipc_log.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(Core_V5F)

static char s_v5f_fmt_buf[128];
static volatile uint32_t s_v5f_last_cmd_seq = 0;

void IPC_Log_Init_V5F(void)
{
    volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
    shared->write_pos = 0;
    shared->read_pos = 0;
    shared->v5f_status = IPC_LOG_STATUS_IDLE;
    s_v5f_last_cmd_seq = shared->cmd_seq;

#ifndef GLXSS_ENABLED
    IPC_ClearFlagStatus(IPC_CH0, IPC_CH_Sta_Bit1);
    NVIC_ClearPendingIRQ(IPC_CH0_IRQn);
    NVIC_SetPriority(IPC_CH0_IRQn, (2 << 5) | (0 << 4));
    NVIC_EnableIRQ(IPC_CH0_IRQn);
#endif
}

void IPC_Log_SetStatus_V5F(uint32_t status)
{
    IPC_LOG_SHARED->v5f_status = status;
}

void IPC_Log_Printf_V5F(const char *fmt, ...)
{
    volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(s_v5f_fmt_buf, sizeof(s_v5f_fmt_buf), fmt, args);
    va_end(args);

    if (len <= 0) return;
    if ((uint32_t)len >= sizeof(s_v5f_fmt_buf))
        len = sizeof(s_v5f_fmt_buf) - 1;

    uint32_t wp = shared->write_pos;
    uint32_t free_space;
    uint32_t rp = shared->read_pos;

    if (wp >= rp) {
        free_space = IPC_LOG_BUF_SIZE - (wp - rp) - 1;
    } else {
        free_space = rp - wp - 1;
    }

    if ((uint32_t)len > free_space)
        len = (int)free_space;

    uint32_t i;
    for (i = 0; i < (uint32_t)len; i++) {
        shared->buf[(wp + i) % IPC_LOG_BUF_SIZE] = (uint8_t)s_v5f_fmt_buf[i];
    }

    __asm volatile ("fence iorw, iorw" ::: "memory");
    shared->write_pos = (wp + (uint32_t)len) % IPC_LOG_BUF_SIZE;

#ifndef GLXSS_ENABLED
    IPC_ITConfig(IPC_CH0, IPC_CH_Sta_Bit1, ENABLE);
#endif
}

uint32_t IPC_Cmd_Recv_V5F(uint32_t *param)
{
    volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
    uint32_t seq = shared->cmd_seq;

    if (seq == s_v5f_last_cmd_seq) {
        return IPC_CMD_NONE;
    }

    __asm volatile ("fence iorw, iorw" ::: "memory");

    uint32_t cmd = shared->cmd;
    uint32_t p = shared->cmd_param;
    s_v5f_last_cmd_seq = seq;
    shared->cmd_ack = seq;

    if (param) *param = p;
    return cmd;
}

uint32_t IPC_Ctrl_GetFlags_V5F(void)
{
    __asm volatile ("fence iorw, iorw" ::: "memory");
    return IPC_LOG_SHARED->ctrl_flags;
}

void IPC_Ctrl_SetFlags_V5F(uint32_t flags)
{
    IPC_LOG_SHARED->ctrl_flags = flags;
    __asm volatile ("fence iorw, iorw" ::: "memory");
}

void IPC_SetMIResult_V5F(uint8_t pred, int32_t sl, int32_t sr, int32_t conf)
{
    volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
    shared->v5f_pred = pred;
    shared->v5f_score_left = sl;
    shared->v5f_score_right = sr;
    shared->v5f_confidence = conf;
    shared->v5f_infer_count++;
    __asm volatile ("fence iorw, iorw" ::: "memory");
}

void IPC_SetSSVEPResult_V5F(int8_t idx, const int32_t scores_q10000[4])
{
    volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
    shared->ssvep_raw_index = idx;
    for (int i = 0; i < 4; i++)
        shared->ssvep_scores_q10000[i] = scores_q10000[i];
    __asm volatile ("fence iorw, iorw" ::: "memory");
}

void IPC_SetFeature_V5F(const int32_t feature_q[IPC_FEATURE_DIM])
{
    volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
    for (int i = 0; i < IPC_FEATURE_DIM; i++)
        shared->feature_q[i] = feature_q[i];
    shared->feature_seq++;
    shared->feature_valid = 1;
    __asm volatile ("fence iorw, iorw" ::: "memory");
}

void IPC_IncSample_V5F(uint32_t n)
{
    IPC_LOG_SHARED->sample_count += n;
}

#ifdef V5F_MODE_GLXSS
void IPC_CH0_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void IPC_CH0_Handler(void)
{
    if (IPC_GetITStatus(IPC_CH0, IPC_CH_Sta_Bit1) != RESET) {
        IPC_ITConfig(IPC_CH0, IPC_CH_Sta_Bit1, DISABLE);
    }
}
#endif

#endif
