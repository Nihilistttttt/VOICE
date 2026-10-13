/********************************** (C) COPYRIGHT *******************************
 * File Name          : main_v5f.c
 * Description        : V5F core entry. Compile with V5F_MODE_GLXSS to drive
 *                      AR glasses display (W25Q64 architecture, command-driven).
 *******************************************************************************/

#include "debug.h"
#include "hardware.h"

#if defined(V5F_MODE_GLXSS) || defined(GLXSS_ENABLED)
#include "ipc_log.h"
#include "glxss_me.h"
#include "usb_host_config.h"
#include "ch32h417_usbhs_host.h"
#include <string.h>
#ifdef GLXSS_ENABLED
#include "dualcore_ipc.h"
#include "dualcore_v5f_ssvep.h"
#endif
#include "game_sprites.h"
#endif

#if defined(V5F_MODE_GLXSS) || defined(GLXSS_ENABLED)

#define MODE_IDLE        0
#define MODE_SSVEP       1
#define MODE_ARROW       2
#define MODE_ARROW_TRAIN 3
#define MODE_GAME        4
#define MODE_FOCUS       5

#define LCD_W  640
#define LCD_H  400
#define BYTES_PER_PIX  4

#ifndef USB_PID_NAK
#define USB_PID_NAK  0x0A
#endif

static volatile uint32_t g_tick_raw = 0;
static uint32_t g_ticks_per_ms = 1;

void TIM4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM4_IRQHandler(void)
{
    TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    g_tick_raw++;
}

static uint32_t tick_get_us(void)
{
    return (uint32_t)((uint64_t)g_tick_raw * 1000 / g_ticks_per_ms);
}

static void tick_init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM4, ENABLE);
    TIM_Cmd(TIM4, DISABLE);
    TIM_TimeBaseStructure.TIM_Period        = 10 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler     = SystemCoreClock / 1000000 / 4 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);
    TIM_ClearFlag(TIM4, TIM_FLAG_Update);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
    NVIC_EnableIRQ(TIM4_IRQn);
    TIM_Cmd(TIM4, ENABLE);
    g_tick_raw = 0;
    Delay_Ms(200);
    g_ticks_per_ms = g_tick_raw / 200;
    if (g_ticks_per_ms == 0) g_ticks_per_ms = 1;
    g_tick_raw = 0;
}

static uint16_t g_endp_tog = 0;
static uint8_t g_mode = MODE_IDLE;

static uint8_t find_bulk_out_ep(void)
{
    uint8_t index = DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL;
    for (uint8_t n = 0; n < HostCtl[index].InterfaceNum; n++) {
        for (uint8_t e = 0; e < HostCtl[index].Interface[n].OutEndpNum; e++) {
            if (HostCtl[index].Interface[n].OutEndpType[e] == 0x02)
                return HostCtl[index].Interface[n].OutEndpAddr[e];
        }
    }
    return 0;
}

static void reset_toggle_light(void)
{
    g_endp_tog = 0;
}

static void reset_toggle_full(void)
{
    glxss_usb_clear_halt(GLXSS_EP_DATA_OUT);
    g_endp_tog = 0;
    Delay_Ms(5);
}

/* ==================== SSVEP ==================== */

static __attribute__((aligned(4))) uint8_t s_white_first[512];
static __attribute__((aligned(4))) uint8_t s_white_mid[512];
static __attribute__((aligned(4))) uint8_t s_black_first[512];
static __attribute__((aligned(4))) uint8_t s_black_mid[512];

static void ssvep_precompute(void)
{
    uint8_t hdr[16] = {0x80,0x02,0x00,0x00, 0x90,0x01,0x00,0x00, 0x01,0,0,0,0,0,0,0};
    memcpy(s_white_first, hdr, 16);
    memset(s_white_first + 16, 0xFF, 496);
    memset(s_white_mid, 0xFF, 512);
    memcpy(s_black_first, hdr, 16);
    { uint32_t *p = (uint32_t *)(s_black_first + 16); for (int i = 0; i < 124; i++) p[i] = 0xFF000000; }
    { uint32_t *p = (uint32_t *)s_black_mid; for (int i = 0; i < 128; i++) p[i] = 0xFF000000; }
}

static glxss_err_t send_ssvep_frame(uint8_t visible)
{
    uint8_t out_ep = find_bulk_out_ep();
    if (out_ep == 0) return GLXSS_ERR_BULK_WRITE;
    uint8_t *first = visible ? s_white_first : s_black_first;
    uint8_t *mid   = visible ? s_white_mid   : s_black_mid;
    uint32_t saved_tx_dma = USBHSH->TX_DMA;
    uint16_t tog = g_endp_tog;
    uint16_t tog0 = 0;
    uint16_t tog1 = USBHS_UH_T_TOG_DATA1;
    USBHSH->TX_DMA = (uint32_t)first;
    USBHSH->TX_LEN = 512;
    USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
    USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
    for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
    USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
    if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
    tog ^= USBHS_UH_T_TOG_DATA1;
    USBHSH->TX_DMA = (uint32_t)mid;
    for (uint32_t c = 1; c < 2000; c++) {
        USBHSH->TX_LEN = 512;
        USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
        USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
        for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
        USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
        if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
        tog ^= USBHS_UH_T_TOG_DATA1;
    }
    USBHSH->TX_LEN = 16;
    USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
    USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
    for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
    USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
    g_endp_tog = tog ^ USBHS_UH_T_TOG_DATA1;
    USBHSH->TX_DMA = saved_tx_dma;
    return GLXSS_OK;
}

static uint8_t g_ssvep_freqs[] = {11, 13, 15, 17};
static int g_ssvep_fi = 0;
static uint8_t g_ssvep_toggle = 1;
static uint32_t g_ssvep_count = 0;
static uint32_t g_ssvep_t_start = 0;

static void ssvep_mode_init(void)
{
    g_ssvep_toggle = 1;
    g_ssvep_count = 0;
    g_ssvep_t_start = tick_get_us();
    IPC_Log_Printf_V5F("[V5F] SSVEP %dHz\r\n", g_ssvep_freqs[g_ssvep_fi]);
}

static void ssvep_mode_set_freq(uint32_t freq_idx)
{
    if (freq_idx < 4) g_ssvep_fi = (int)freq_idx;
    ssvep_mode_init();
}

static glxss_err_t ssvep_mode_step(void)
{
    uint8_t freq = g_ssvep_freqs[g_ssvep_fi];
    uint32_t half_us = 500000 / freq;
    uint32_t t_send0 = tick_get_us();
    glxss_err_t err = send_ssvep_frame(g_ssvep_toggle);
    if (err != GLXSS_OK) return err;
    uint32_t send_us = tick_get_us() - t_send0;
    g_ssvep_toggle = !g_ssvep_toggle;
    g_ssvep_count++;
    uint32_t target = g_ssvep_t_start + (uint32_t)g_ssvep_count * half_us;
    uint32_t now = tick_get_us();
    uint32_t late_us = (now >= target) ? (now - target) : 0u;
    {
        volatile IPC_Log_Shared_t *shared = IPC_LOG_SHARED;
        shared->ssvep_send_us = send_us;
        shared->ssvep_late_us = late_us;
        shared->ssvep_frame_cnt = g_ssvep_count;
        shared->ssvep_mode_active = 1u;
    }
    while (1) {
        uint32_t rp_t0 = tick_get_us();
        DualCore_V5F_SSVEP_RunPending();
        uint32_t rp_us = tick_get_us() - rp_t0;
        if (rp_us > 0) {
            IPC_LOG_SHARED->ssvep_rp_us = rp_us;
            IPC_LOG_SHARED->ssvep_rp_count++;
        }
        if (tick_get_us() >= target) break;
    }
    if (tick_get_us() - g_ssvep_t_start >= 2000000) {
        uint32_t elapsed_us = tick_get_us() - g_ssvep_t_start;
        uint32_t meas_hz_x10 = (uint32_t)g_ssvep_count * 5000 / (elapsed_us / 1000);
        IPC_Log_Printf_V5F("[V5F] %lu frm %lums => %lu.%luHz (fixed %dHz)\r\n",
            (unsigned long)g_ssvep_count, (unsigned long)(elapsed_us / 1000),
            (unsigned long)(meas_hz_x10 / 10), (unsigned long)(meas_hz_x10 % 10),
            freq);

        g_ssvep_count = 0;
        g_ssvep_t_start = tick_get_us();
    }
    return GLXSS_OK;
}

/* ==================== TEXT RENDER ==================== */

static const uint8_t s_font5x7[26*7] = {
    0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,
    0x1F,0x11,0x11,0x1E,0x11,0x11,0x1F,
    0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,
    0x1E,0x11,0x11,0x11,0x11,0x11,0x1E,
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,
    0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,
    0x0E,0x11,0x10,0x17,0x11,0x11,0x0E,
    0x11,0x11,0x11,0x1F,0x11,0x11,0x11,
    0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,
    0x07,0x02,0x02,0x02,0x02,0x12,0x0C,
    0x11,0x12,0x14,0x18,0x14,0x12,0x11,
    0x10,0x10,0x10,0x10,0x10,0x10,0x1F,
    0x11,0x1B,0x15,0x15,0x11,0x11,0x11,
    0x11,0x11,0x19,0x15,0x13,0x11,0x11,
    0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,
    0x1F,0x11,0x11,0x1E,0x10,0x10,0x10,
    0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,
    0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,
    0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E,
    0x1F,0x04,0x04,0x04,0x04,0x04,0x04,
    0x11,0x11,0x11,0x11,0x11,0x11,0x0E,
    0x11,0x11,0x11,0x11,0x11,0x0A,0x04,
    0x11,0x11,0x11,0x15,0x15,0x1B,0x11,
    0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,
    0x11,0x11,0x0A,0x04,0x04,0x04,0x04,
    0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,
};

static const uint8_t s_font5x7_digit[10*7] = {
    0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,
    0x04,0x0C,0x04,0x04,0x04,0x04,0x1F,
    0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,
    0x1F,0x02,0x04,0x02,0x01,0x11,0x0E,
    0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,
    0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,
    0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,
    0x1F,0x01,0x02,0x04,0x08,0x08,0x08,
    0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,
    0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,
};

static int text_pixel(int32_t x, int32_t y,
                       const char *str, int len,
                       int scale, int cx, int cy)
{
    int cw = 5, ch = 7, gap = 1;
    int step = cw + gap;
    int tw = len * step - gap;
    int x0 = cx - (tw * scale) / 2;
    int y0 = cy - (ch * scale) / 2;
    int dx = (int)x - x0;
    int dy = (int)y - y0;
    if (dx < 0 || dy < 0 || dy >= ch * scale) return 0;
    int ci = dx / (step * scale);
    if (ci >= len) return 0;
    int lx = dx - ci * step * scale;
    if (lx >= cw * scale) return 0;
    int row = dy / scale;
    int col = lx / scale;
    if (row >= ch || col >= cw) return 0;
    int idx = str[ci] - 'A';
    if (idx >= 0 && idx < 26)
        return (s_font5x7[idx * 7 + row] & (0x10 >> col)) ? 1 : 0;
    if (str[ci] == '-')
        return (row == 3 && col >= 0 && col < 5) ? 1 : 0;
    int didx = str[ci] - '0';
    if (didx < 0 || didx >= 10) return 0;
    return (s_font5x7_digit[didx * 7 + row] & (0x10 >> col)) ? 1 : 0;
}

/* 16x16 Chinese glyphs (from msyh.ttc):
 * 0=红,1=黄,2=绿,3=蓝,4=白,5=黑, 6=等,7=待,8=指,9=令,10=放,11=松 */
static const uint8_t s_font_cn[12][32] = {
    { 0x00,0x00, 0x18,0x00, 0x18,0x00, 0x33,0xFE, 0x20,0x30, 0x6C,0x30, 0xC8,0x30, 0xF8,0x30, 0x30,0x30, 0x20,0x30, 0x7C,0x30, 0x78,0x30, 0x00,0x30, 0x3C,0x30, 0xF3,0xFF, 0x03,0xFF }, /* 红 */
    { 0x00,0x00, 0x04,0x20, 0x7F,0xFE, 0x7F,0xFE, 0x04,0x30, 0xFF,0xFF, 0x01,0x80, 0x1F,0xF8, 0x3F,0xFC, 0x31,0x8C, 0x3F,0xFC, 0x31,0x8C, 0x3F,0xFC, 0x3F,0xFC, 0x0C,0x30, 0x7C,0x1E }, /* 黄 */
    { 0x00,0x00, 0x10,0x00, 0x33,0xFC, 0x20,0x04, 0x69,0xFC, 0x49,0xFC, 0xF8,0x04, 0x77,0xFF, 0x32,0x20, 0x63,0x32, 0x79,0x36, 0x70,0xF8, 0x01,0xE8, 0x3F,0x26, 0xF2,0x23, 0x01,0xE0 }, /* 绿 */
    { 0x00,0x00, 0x04,0x20, 0x7F,0xFE, 0xFF,0xFF, 0x04,0x20, 0x26,0x40, 0x26,0xFE, 0x26,0xC0, 0x24,0x98, 0x27,0x8C, 0x00,0x00, 0x3F,0xFC, 0x32,0x6C, 0x32,0x44, 0x32,0x6C, 0xFF,0xFF }, /* 蓝 */
    { 0x00,0x00, 0x03,0x00, 0x03,0x00, 0x3F,0xFC, 0x7F,0xFC, 0x60,0x04, 0x60,0x04, 0x60,0x04, 0x60,0x04, 0x7F,0xFC, 0x60,0x04, 0x60,0x04, 0x60,0x04, 0x60,0x04, 0x7F,0xFC, 0x7F,0xFC }, /* 白 */
    { 0x00,0x00, 0x00,0x00, 0x3F,0xFC, 0x29,0x94, 0x2D,0xB4, 0x25,0xA4, 0x21,0xA4, 0x3F,0xFC, 0x01,0x80, 0x3F,0xFC, 0x3F,0xFC, 0x01,0x80, 0xFF,0xFF, 0x24,0xCC, 0x64,0x44, 0x46,0x66 }, /* 黑 */
    { 0x00,0x00, 0x10,0x20, 0x3F,0x7E, 0x7F,0xFE, 0xC8,0x90, 0x85,0x98, 0x7F,0xFE, 0x01,0x80, 0x01,0x80, 0xFF,0xFF, 0x00,0x10, 0x7F,0xFE, 0x08,0x30, 0x0C,0x10, 0x06,0x30, 0x01,0xF0 }, /* 等 */
    { 0x00,0x00, 0x08,0x20, 0x18,0x20, 0x33,0xFE, 0x63,0xFE, 0xC0,0x20, 0x18,0x60, 0x17,0xFF, 0x30,0x08, 0x70,0x0C, 0xF7,0xFF, 0x30,0x08, 0x33,0x08, 0x31,0x88, 0x30,0x08, 0x30,0x78 }, /* 待 */
    { 0x00,0x00, 0x32,0x00, 0x33,0x0E, 0x33,0xFC, 0xFF,0xC3, 0x7B,0x03, 0x33,0xFE, 0x30,0x00, 0x38,0x00, 0x7B,0xFE, 0xF3,0x06, 0x33,0x06, 0x33,0xFE, 0x33,0x06, 0x33,0xFE, 0xF3,0xFE }, /* 指 */
    { 0x00,0x00, 0x00,0x80, 0x01,0x80, 0x03,0xC0, 0x06,0x60, 0x1C,0x38, 0x33,0x0C, 0xE1,0x87, 0x00,0x80, 0x3F,0xF8, 0x3F,0xF8, 0x00,0x10, 0x0C,0x70, 0x06,0xC0, 0x03,0xC0, 0x00,0xC0 }, /* 令 */
    { 0x00,0x00, 0x18,0x20, 0x18,0x60, 0x08,0x60, 0xFF,0x7F, 0x30,0xFE, 0x30,0xC4, 0x31,0xC4, 0x3E,0x44, 0x32,0x6C, 0x22,0x68, 0x26,0x38, 0x26,0x30, 0x66,0x38, 0x46,0x6C, 0xDD,0xC7 }, /* 放 */
    { 0x00,0x00, 0x10,0x00, 0x10,0xD8, 0x10,0xD8, 0x7C,0x88, 0xFD,0x8C, 0x31,0x06, 0x33,0x23, 0x3E,0x63, 0x74,0x40, 0xD0,0xC0, 0x90,0xC8, 0x10,0x8C, 0x11,0x84, 0x13,0xFE, 0x13,0xF6 }, /* 松 */
};

static int text_pixel_cn(int32_t x, int32_t y, int idx, int scale, int cx, int cy)
{
    int cw = 16, ch = 16;
    int x0 = cx - (cw * scale) / 2;
    int y0 = cy - (ch * scale) / 2;
    int dx = (int)x - x0;
    int dy = (int)y - y0;
    if (dx < 0 || dy < 0 || dy >= ch * scale) return 0;
    int row = dy / scale;
    int col = dx / scale;
    if (row >= ch || col >= cw) return 0;
    uint8_t bits = s_font_cn[idx][row * 2 + (col >> 3)];
    return (bits & (0x80 >> (col & 7))) ? 1 : 0;
}

/* Multi-glyph string: idxs[] holds N glyph indices, horizontally centered at (cx,cy). */
static int text_pixel_cn_str(int32_t x, int32_t y, const uint8_t *idxs, int n, int scale, int cx, int cy)
{
    int cw = 16, ch = 16;
    int total_w = cw * scale * n;
    int x0 = cx - total_w / 2;
    int y0 = cy - (ch * scale) / 2;
    int dx = (int)x - x0;
    int dy = (int)y - y0;
    if (dx < 0 || dy < 0 || dy >= ch * scale) return 0;
    int gi = dx / (cw * scale);
    if (gi < 0 || gi >= n) return 0;
    int local_x = dx - gi * cw * scale;
    int row = dy / scale;
    int col = local_x / scale;
    if (row >= ch || col >= cw) return 0;
    uint8_t bits = s_font_cn[idxs[gi]][row * 2 + (col >> 3)];
    return (bits & (0x80 >> (col & 7))) ? 1 : 0;
}

/* ==================== ARROW ==================== */

static __attribute__((aligned(4))) uint8_t s_pkt[2048];
static uint8_t g_arrow_tgt_dir = 0;
static uint8_t g_arrow_res_dir = 0;
static uint8_t g_arrow_tgt_color = 0;
static uint8_t g_arrow_res_color = 0;
static uint8_t g_arrow_tgt_vis = 1;
static uint8_t g_arrow_res_vis = 0;
static uint8_t g_arrow_dash = 0;
static uint8_t g_arrow_dirty = 1;

/* 推理边界显示相位: TARGET显示目标(白)→RESULT显示结果(变色)→GAP消失(转向缓冲) */
#define ARROW_PHASE_TARGET 0
#define ARROW_PHASE_RESULT 1
#define ARROW_PHASE_GAP    2
#define ARROW_RESULT_MS    1500   /* 最终投票结果显示时长 */
#define ARROW_GAP_MS       200    /* 消失期(转向缓冲), 不挤压目标显示 */
static uint8_t  g_arrow_phase = ARROW_PHASE_TARGET;
static uint32_t g_arrow_phase_start = 0;

static int is_in_arrow(int32_t px, int32_t py, int32_t cx, int32_t cy, int32_t sz, uint8_t dir)
{
    /* dir: 0=LEFT, 1=RIGHT (matches IPC_PRED_LEFT/RIGHT and DIR_LABEL_*) */
    int32_t dx = px - cx;
    int32_t dy = py - cy;
    int32_t half = sz / 2;
    if (dir == 1) {
        if (dx < -half || dx > half) return 0;
        int32_t edge = half - dx;
        if (edge < 0) edge = 0;
        return (dy >= -edge && dy <= edge);
    } else {
        if (dx < -half || dx > half) return 0;
        int32_t edge = half + dx;
        if (edge < 0) edge = 0;
        return (dy >= -edge && dy <= edge);
    }
}

static int is_in_dash(int32_t px, int32_t py, int32_t cx, int32_t cy, int32_t sz)
{
    int32_t dx = px - cx;
    int32_t dy = py - cy;
    int32_t hw = sz * 3 / 4;
    int32_t hh = sz / 6;
    int32_t gap = sz / 4;
    if (dy >= -hh && dy <= hh) {
        if ((dx >= -hw && dx <= -gap) || (dx >= gap && dx <= hw)) return 1;
    }
    return 0;
}

static int pitch_overlay_pixel(int32_t x, int32_t y, uint8_t *r, uint8_t *g, uint8_t *b);

static void fill_arrow_pkt(uint32_t offset, uint16_t len)
{
    uint8_t hdr[16] = {0x80,0x02,0x00,0x00, 0x90,0x01,0x00,0x00, 0x01,0,0,0,0,0,0,0};
    const int32_t sz = 120, tgt_cx = 160, res_cx = 480, cy = 200;
    uint8_t tgt_r = 0xFF, tgt_g = 0xFF, tgt_b = 0xFF;
    if (g_arrow_tgt_color == 1) { tgt_r = 0x00; tgt_g = 0xFF; tgt_b = 0x00; }
    else if (g_arrow_tgt_color == 2) { tgt_r = 0xFF; tgt_g = 0x00; tgt_b = 0x00; }
    uint8_t res_r = 0xFF, res_g = 0xFF, res_b = 0xFF;
    if (g_arrow_res_color == 1) { res_r = 0x00; res_g = 0xFF; res_b = 0x00; }
    else if (g_arrow_res_color == 2) { res_r = 0xFF; res_g = 0x00; res_b = 0x00; }
    for (uint16_t i = 0; i < len; i++) {
        uint32_t pos = offset + i;
        if (pos < 16) { s_pkt[i] = hdr[pos]; continue; }
        uint32_t pb = pos - 16;
        uint32_t pi = pb / 4;
        uint8_t ch = pb % 4;
        uint32_t x = pi % 640;
        uint32_t y = pi / 640;
        uint8_t r = 0, g = 0, b = 0;
        if (g_arrow_tgt_vis && !g_arrow_dash && is_in_arrow((int32_t)x, (int32_t)y, tgt_cx, cy, sz, g_arrow_tgt_dir))
            { r = tgt_r; g = tgt_g; b = tgt_b; }
        if (g_arrow_tgt_vis && g_arrow_dash && is_in_dash((int32_t)x, (int32_t)y, 320, cy, 200))
            { r = tgt_r; g = tgt_g; b = tgt_b; }
        if (g_arrow_res_vis && is_in_arrow((int32_t)x, (int32_t)y, res_cx, cy, sz, g_arrow_res_dir))
            { r = res_r; g = res_g; b = res_b; }
        { uint8_t pr,pg,pb; if (pitch_overlay_pixel((int32_t)x,(int32_t)y,&pr,&pg,&pb)) { r=pr; g=pg; b=pb; } }
        if (ch == 0) s_pkt[i] = r;
        else if (ch == 1) s_pkt[i] = g;
        else if (ch == 2) s_pkt[i] = b;
        else s_pkt[i] = 0xFF;
    }
}

static glxss_err_t send_arrow_frame(void)
{
    uint8_t out_ep = find_bulk_out_ep();
    if (out_ep == 0) return GLXSS_ERR_BULK_WRITE;
    uint32_t saved_tx_dma = USBHSH->TX_DMA;
    uint16_t tog = g_endp_tog;
    uint16_t tog0 = 0;
    uint16_t tog1 = USBHS_UH_T_TOG_DATA1;
    uint32_t frame_total = 1024016;
    uint32_t pos = 0;
    while (pos < frame_total) {
        uint16_t chunk = 512;
        if (pos + chunk > frame_total) chunk = (uint16_t)(frame_total - pos);
        fill_arrow_pkt(pos, chunk);
        USBHSH->TX_DMA = (uint32_t)s_pkt;
        USBHSH->TX_LEN = chunk;
        USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
        USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
        for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
        USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
        if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
        tog ^= USBHS_UH_T_TOG_DATA1;
        pos += chunk;
    }
    g_endp_tog = tog;
    USBHSH->TX_DMA = saved_tx_dma;
    return GLXSS_OK;
}

/* 测试模式: 左三下右三下 + 推理结果对比 */
static uint32_t g_mi_infer_count = 0;
static uint8_t g_mi_sim_pred = IPC_PRED_UNKNOWN;
static int32_t g_mi_sim_sl = 5000;
static int32_t g_mi_sim_sr = 5000;
static uint32_t g_mi_sim_last_us = 0;
static uint8_t g_mi_infer_active = 0;

static uint8_t g_test_show_result = 0;
static uint8_t g_test_new_result = 0;
static uint8_t g_test_hide_target = 0;

static uint32_t g_test_target_start = 0;

static void arrow_mode_init(void)
{
    g_arrow_tgt_dir = 0;
    g_arrow_tgt_color = 0;   /* white */
    g_arrow_tgt_vis = 1;
    g_arrow_res_dir = 0;
    g_arrow_res_color = 0;
    g_arrow_res_vis = 0;
    g_arrow_dash = 0;
    g_arrow_dirty = 1;
    g_arrow_phase = ARROW_PHASE_TARGET;
    g_arrow_phase_start = tick_get_us();
    g_test_show_result = 0;
    g_test_new_result = 0;
    g_test_hide_target = 0;
    g_test_target_start = tick_get_us();
}

static glxss_err_t arrow_mode_step(void)
{
    uint32_t now = tick_get_us();

    if (g_test_new_result) {
        g_test_new_result = 0;
        g_arrow_res_dir = (g_mi_sim_sr >= g_mi_sim_sl) ? 1u : 0u;
        /* 一致=绿色(1), 不一致=红色(2); 目标+结果同色反馈 */
        uint8_t ok = (g_arrow_res_dir == g_arrow_tgt_dir) ? 1u : 0u;
        g_arrow_tgt_color = ok ? 1u : 2u;
        g_arrow_res_color = ok ? 1u : 2u;
        g_arrow_tgt_vis = 1;
        g_arrow_res_vis = 1;
        if (g_arrow_phase != ARROW_PHASE_RESULT) {
            /* 首次进入结果相位才重置计时; 后续推理只刷新内容不延长显示 */
            g_arrow_phase = ARROW_PHASE_RESULT;
            g_arrow_phase_start = tick_get_us();
        }
        g_arrow_dirty = 1;
    }

    if (g_arrow_phase == ARROW_PHASE_RESULT) {
        if (now - g_arrow_phase_start >= ARROW_RESULT_MS) {
            g_arrow_phase = ARROW_PHASE_GAP;
            g_arrow_phase_start = now;
            g_arrow_tgt_vis = 0;
            g_arrow_res_vis = 0;
            g_arrow_dirty = 1;
        }
    } else if (g_arrow_phase == ARROW_PHASE_GAP) {
        if (now - g_arrow_phase_start >= ARROW_GAP_MS) {
            g_arrow_phase = ARROW_PHASE_TARGET;
            g_arrow_phase_start = now;
            /* 消失结束立即恢复显示目标(白色), 避免等待命令时空白;
               IPC_CMD_ARROW 到达时再更新方向 */
            g_arrow_tgt_color = 0;
            g_arrow_tgt_vis = 1;
            g_arrow_res_vis = 0;
            g_arrow_dirty = 1;
        }
    }

    if (!g_arrow_dirty) {
        return GLXSS_OK;
    }
    g_arrow_dirty = 0;
    return send_arrow_frame();
}


/* ==================== ARROW_TRAIN ==================== */

static uint8_t g_train_dir = 0;
static uint16_t g_train_progress = 0;
static uint8_t g_train_arrow_vis = 0;
static uint32_t g_train_phase_start = 0;
static uint8_t g_train_phase = 0;
static uint8_t g_train_active = 0;

static void fill_train_pkt(uint32_t offset, uint16_t len)
{
    uint8_t hdr[16] = {0x80,0x02,0x00,0x00, 0x90,0x01,0x00,0x00, 0x01,0,0,0,0,0,0,0};
    const int32_t sz = 160, cx = 320, cy = 160;
    const int32_t bar_y = 320, bar_h = 30, bar_x0 = 80, bar_x1 = 560, bar_w = bar_x1 - bar_x0;
    for (uint16_t i = 0; i < len; i++) {
        uint32_t pos = offset + i;
        if (pos < 16) { s_pkt[i] = hdr[pos]; continue; }
        uint32_t pb = pos - 16;
        uint32_t pi = pb / 4;
        uint8_t ch = pb % 4;
        uint32_t x = pi % 640;
        uint32_t y = pi / 640;
        uint8_t r = 0, g = 0, b = 0;
        if (g_train_arrow_vis && is_in_arrow((int32_t)x, (int32_t)y, cx, cy, sz, g_train_dir))
            { r = 0xFF; g = 0xFF; b = 0xFF; }
        if (!g_train_arrow_vis && g_train_active) {
            static const uint8_t relax_str[2] = {10, 11}; /* 放松 */
            if (text_pixel_cn_str((int32_t)x, (int32_t)y, relax_str, 2, 8, 320, 160))
                { r = 0xFF; g = 0xFF; b = 0xFF; }
        }
        if ((int32_t)y >= bar_y && (int32_t)y < bar_y + bar_h &&
            (int32_t)x >= bar_x0 && (int32_t)x < bar_x1) {
            int32_t fill_x = bar_x0 + (int32_t)g_train_progress * bar_w / 1000;
            if ((int32_t)x < fill_x) { r = 0x00; g = 0x80; b = 0xFF; }
            else { r = 0x40; g = 0x40; b = 0x40; }
        }
        { uint8_t pr,pg,pb; if (pitch_overlay_pixel((int32_t)x,(int32_t)y,&pr,&pg,&pb)) { r=pr; g=pg; b=pb; } }
        if (ch == 0) s_pkt[i] = r;
        else if (ch == 1) s_pkt[i] = g;
        else if (ch == 2) s_pkt[i] = b;
        else s_pkt[i] = 0xFF;
    }
}

static glxss_err_t send_train_frame(void)
{
    uint8_t out_ep = find_bulk_out_ep();
    if (out_ep == 0) return GLXSS_ERR_BULK_WRITE;
    uint32_t saved_tx_dma = USBHSH->TX_DMA;
    uint16_t tog = g_endp_tog;
    uint16_t tog0 = 0;
    uint16_t tog1 = USBHS_UH_T_TOG_DATA1;
    uint32_t frame_total = 1024016;
    uint32_t pos = 0;
    while (pos < frame_total) {
        uint16_t chunk = 512;
        if (pos + chunk > frame_total) chunk = (uint16_t)(frame_total - pos);
        fill_train_pkt(pos, chunk);
        USBHSH->TX_DMA = (uint32_t)s_pkt;
        USBHSH->TX_LEN = chunk;
        USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
        USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
        for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
        USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
        if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
        tog ^= USBHS_UH_T_TOG_DATA1;
        pos += chunk;
    }
    g_endp_tog = tog;
    USBHSH->TX_DMA = saved_tx_dma;
    return GLXSS_OK;
}

static char    s_pitch_buf[8];
static int     s_pitch_len = 0;
static uint32_t s_pitch_last_us = 0;

static int pitch_overlay_pixel(int32_t x, int32_t y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    static uint8_t s_pitch_green = 1;
    uint32_t now = tick_get_us();
    if (now - s_pitch_last_us > 100000u) {
        s_pitch_last_us = now;
        int32_t pitch_deg10 = IPC_LOG_SHARED->v3f_pitch_deg10;
        int deg = (int)(pitch_deg10 / 10);
        s_pitch_green = (deg >= 0 && deg < 180) ? 1 : 0;
        if (deg < 0) deg = 0;
        if (deg > 359) deg = 359;
        char d[8]; int dn = 0;
        if (deg == 0) { d[dn++] = '0'; }
        else { while (deg > 0 && dn < 6) { d[dn++] = (char)('0' + deg % 10); deg /= 10; } }
        for (int i = 0; i < dn; i++) s_pitch_buf[i] = d[dn-1-i];
        s_pitch_len = dn;
    }
    if (text_pixel(x, y, s_pitch_buf, s_pitch_len, 3, 40, 16)) {
        if (s_pitch_green) { *r = 100; *g = 255; *b = 100; }
        else               { *r = 255; *g = 100; *b = 100; }
        return 1;
    }
    return 0;
}

static void fill_idle_pkt(uint32_t offset, uint16_t len)
{
    uint8_t hdr[16] = {0x80,0x02,0x00,0x00, 0x90,0x01,0x00,0x00, 0x01,0,0,0,0,0,0,0};
    for (uint16_t i = 0; i < len; i++) {
        uint32_t pos = offset + i;
        if (pos < 16) { s_pkt[i] = hdr[pos]; continue; }
        uint32_t pb = pos - 16;
        uint32_t pi = pb / 4;
        uint8_t ch = pb % 4;
        uint32_t x = pi % 640;
        uint32_t y = pi / 640;
        uint8_t r = 0, g = 0, b = 0;
        {
            static const uint8_t wait_str[4] = {6, 7, 8, 9}; /* 等待指令 */
            if (text_pixel_cn_str((int32_t)x, (int32_t)y, wait_str, 4, 8, 320, 200))
                { r = 0xFF; g = 0xFF; b = 0xFF; }
        }
        { uint8_t pr,pg,pb; if (pitch_overlay_pixel((int32_t)x,(int32_t)y,&pr,&pg,&pb)) { r=pr; g=pg; b=pb; } }
        if (ch == 0) s_pkt[i] = r;
        else if (ch == 1) s_pkt[i] = g;
        else if (ch == 2) s_pkt[i] = b;
        else s_pkt[i] = 0xFF;
    }
}

static glxss_err_t send_idle_frame(void)
{
    uint8_t out_ep = find_bulk_out_ep();
    if (out_ep == 0) return GLXSS_ERR_BULK_WRITE;
    uint32_t saved_tx_dma = USBHSH->TX_DMA;
    uint16_t tog = g_endp_tog;
    uint16_t tog0 = 0;
    uint16_t tog1 = USBHS_UH_T_TOG_DATA1;
    uint32_t frame_total = 1024016;
    uint32_t pos = 0;
    while (pos < frame_total) {
        uint16_t chunk = 512;
        if (pos + chunk > frame_total) chunk = (uint16_t)(frame_total - pos);
        fill_idle_pkt(pos, chunk);
        USBHSH->TX_DMA = (uint32_t)s_pkt;
        USBHSH->TX_LEN = chunk;
        USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
        USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
        for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
        USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
        if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
        tog ^= USBHS_UH_T_TOG_DATA1;
        pos += chunk;
    }
    g_endp_tog = tog;
    USBHSH->TX_DMA = saved_tx_dma;
    return GLXSS_OK;
}

static glxss_err_t idle_mode_step(void)
{
    return send_idle_frame();
}

static void train_mode_init(void)
{
    g_train_active = 1;
    g_train_arrow_vis = 0;
    g_train_dir = 0;
    g_train_progress = 0;
    g_train_phase = 0;
    g_train_phase_start = tick_get_us();
    IPC_Log_Printf_V5F("[V5F] TRAIN init (command-driven)\r\n");
}

static glxss_err_t train_mode_step(void)
{
    uint32_t elapsed = tick_get_us() - g_train_phase_start;
    if (g_train_arrow_vis) {
        g_train_progress = (uint16_t)(elapsed / 15000);
        if (g_train_progress > 1000) g_train_progress = 1000;
    } else {
        g_train_progress = (uint16_t)(elapsed / 5000);
        if (g_train_progress > 1000) g_train_progress = 1000;
    }
    return send_train_frame();
}

/* ==================== GAME ==================== */

#define GAME_DURATION_MS       60000
#define GAME_BASE_SPEED_MPS    4.0f
#define GAME_MAX_BOOST_MPS     8.0f
#define GAME_COIN_VALUE        10
#define GAME_LANE_COOLDOWN_US  400000
#define GAME_FRAME_INTERVAL_US 50000
#define GAME_MAX_COINS         16
#define GAME_COIN_HIT_RADIUS_M 2.0f
#define GAME_VIEW_RANGE_M      40.0f
#define GAME_COIN_SPACING_MIN  4.0f
#define GAME_COIN_SPACING_MAX  10.0f

static uint8_t  g_game_lane = 1;
static float    g_game_world_y = 0;
static uint32_t g_game_coins = 0;
static uint32_t g_game_start_us = 0;
static uint8_t  g_game_active = 0;
static uint8_t  g_game_over = 0;
static uint32_t g_game_last_frame_us = 0;
static uint32_t g_game_last_lane_us = 0;
static uint8_t  g_game_last_pred = 2;
static float    g_coin_y[GAME_MAX_COINS];
static uint8_t  g_coin_lane[GAME_MAX_COINS];
static uint8_t  g_coin_active[GAME_MAX_COINS];
static float    g_next_coin_y = 5;
static uint8_t  g_game_dirty = 1;
static uint32_t g_game_over_seq = 0;
static uint8_t  g_walk_frame = 0;
static uint32_t g_walk_timer_us = 0;

static int int_to_str(int val, char *buf);

static int32_t  s_prep_coin_sx[GAME_MAX_COINS];
static int32_t  s_prep_coin_sy[GAME_MAX_COINS];
static int      s_prep_coin_n = 0;
static int      s_prep_coin_min_sy = 999;
static int      s_prep_coin_max_sy = -999;
static int32_t  s_prep_player_sx = 0;
static int      s_prep_bar_w = 0;
static int      s_prep_mile_sy[8];
static int      s_prep_mile_n = 0;
static char     s_prep_buf_score[12]; static int s_prep_slen = 0;
static char     s_prep_buf_time[12];  static int s_prep_tlen = 0;
static char     s_prep_buf_dist[12];  static int s_prep_dlen = 0;
static char     s_prep_buf_coins[12]; static int s_prep_clen = 0;

static void game_frame_prepare(void)
{
    const int32_t player_sy = 360;
    const float mpp = 10.0f;

    s_prep_coin_n = 0;
    s_prep_coin_min_sy = 999;
    s_prep_coin_max_sy = -999;
    for (int c = 0; c < GAME_MAX_COINS; c++) {
        if (!g_coin_active[c]) continue;
        int32_t sy = player_sy - (int32_t)((g_coin_y[c] - g_game_world_y) * mpp);
        if (sy < -40 || sy > 400) continue;
        s_prep_coin_sx[s_prep_coin_n] = (int32_t)g_coin_lane[c] * 213 + 106;
        s_prep_coin_sy[s_prep_coin_n] = sy;
        if (sy < s_prep_coin_min_sy) s_prep_coin_min_sy = sy;
        if (sy > s_prep_coin_max_sy) s_prep_coin_max_sy = sy;
        s_prep_coin_n++;
    }

    s_prep_player_sx = (int32_t)g_game_lane * 213 + 106;

    uint32_t elapsed_us = tick_get_us() - g_game_start_us;
    int timeLeft_s = (int)(GAME_DURATION_MS / 1000) - (int)(elapsed_us / 1000000);
    if (timeLeft_s < 0) timeLeft_s = 0;
    int score = (int)g_game_world_y + (int)g_game_coins * GAME_COIN_VALUE;
    int dist = (int)g_game_world_y;
    s_prep_slen = int_to_str(score, s_prep_buf_score);
    s_prep_tlen = int_to_str(timeLeft_s, s_prep_buf_time);
    s_prep_dlen = int_to_str(dist, s_prep_buf_dist);
    s_prep_clen = int_to_str((int)g_game_coins, s_prep_buf_coins);
    s_prep_bar_w = timeLeft_s * 640 / (GAME_DURATION_MS / 1000);

    s_prep_mile_n = 0;
    {
        int startM = ((int)g_game_world_y / 10) * 10 - 10;
        int endM = (int)g_game_world_y + 45;
        for (int m = startM; m <= endM && s_prep_mile_n < 8; m += 10) {
            if (m < 0) continue;
            s_prep_mile_sy[s_prep_mile_n++] = player_sy - (int32_t)((m - g_game_world_y) * mpp);
        }
    }
}

static int int_to_str(int val, char *buf)
{
    if (val < 0) val = 0;
    if (val == 0) { buf[0] = '0'; return 1; }
    char tmp[12];
    int n = 0;
    while (val > 0 && n < 11) { tmp[n++] = (char)('0' + (val % 10)); val /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

static void fill_game_pkt(uint32_t offset, uint16_t len)
{
    uint8_t hdr[16] = {0x80,0x02,0x00,0x00, 0x90,0x01,0x00,0x00, 0x01,0,0,0,0,0,0,0};
    const int32_t player_sy = 360;

    for (uint16_t i = 0; i < len; i++) {
        uint32_t pos = offset + i;
        if (pos < 16) { s_pkt[i] = hdr[pos]; continue; }
        uint32_t pb = pos - 16;
        uint32_t pi = pb / 4;
        uint8_t ch = pb % 4;
        uint32_t x = pi % 640;
        uint32_t y = pi / 640;
        uint8_t r = 12, g = 14, b = 20;

        uint32_t lane = (x >= 426) ? 2 : (x >= 213 ? 1 : 0);
        if (lane == g_game_lane) { r += 8; g += 12; b += 20; }

        if (x == 213 || x == 426) { r = 50; g = 50; b = 60; }

        if (x < 40) {
            for (int m = 0; m < s_prep_mile_n; m++) {
                if ((int)y == s_prep_mile_sy[m]) { r = 70; g = 70; b = 80; break; }
            }
        }

        if (s_prep_coin_n > 0) {
            int32_t yi = (int32_t)y;
            if (yi >= s_prep_coin_min_sy - SPRITE_COIN_H/2 && yi <= s_prep_coin_max_sy + SPRITE_COIN_H/2) {
                int32_t xi = (int32_t)x;
                for (int c = 0; c < s_prep_coin_n; c++) {
                    int32_t dx = xi - s_prep_coin_sx[c];
                    int32_t dy = yi - s_prep_coin_sy[c];
                    if (dx >= -SPRITE_COIN_W/2 && dx < SPRITE_COIN_W/2 && dy >= -SPRITE_COIN_H/2 && dy < SPRITE_COIN_H/2) {
                        int32_t csx = dx + SPRITE_COIN_W/2;
                        int32_t csy = dy + SPRITE_COIN_H/2;
                        uint8_t palIdx = sprite_coin_data[csy * SPRITE_COIN_W + csx];
                        if (palIdx != 0) {
                            uint16_t c565 = sprite_coin_palette[palIdx];
                            r = (c565 >> 11) << 3; g = ((c565 >> 5) & 0x3F) << 2; b = (c565 & 0x1F) << 3;
                            break;
                        }
                    }
                }
            }
        }

        {
            int32_t spx = (int32_t)x - (s_prep_player_sx - SPRITE_CHAR_W/2);
            int32_t spy = (int32_t)y - (player_sy - SPRITE_CHAR_H);
            if (spx >= 0 && spx < SPRITE_CHAR_W && spy >= 0 && spy < SPRITE_CHAR_H) {
                uint8_t palIdx = sprite_char_frames[g_walk_frame][spy * SPRITE_CHAR_W + spx];
                if (palIdx != 0) {
                    uint16_t c565 = sprite_char_palette[palIdx];
                    r = (c565 >> 11) << 3; g = ((c565 >> 5) & 0x3F) << 2; b = (c565 & 0x1F) << 3;
                }
            }
        }

        if (y < 6) {
            if ((int)x < s_prep_bar_w) { r = 255; g = 80; b = 80; }
            else { r = 40; g = 20; b = 20; }
        }

        if (x >= 540 && y >= 15 && y <= 130) {
            if (text_pixel((int32_t)x, (int32_t)y, s_prep_buf_time,  s_prep_tlen, 2, 580, 24))  { r=255; g=255; b=255; }
            else if (text_pixel((int32_t)x, (int32_t)y, s_prep_buf_score, s_prep_slen, 2, 580, 56))  { r=255; g=200; b=0; }
            else if (text_pixel((int32_t)x, (int32_t)y, s_prep_buf_dist,  s_prep_dlen, 2, 580, 88))  { r=100; g=255; b=100; }
            else if (text_pixel((int32_t)x, (int32_t)y, s_prep_buf_coins, s_prep_clen, 2, 580, 120)) { r=255; g=180; b=0; }
        }

        if (g_game_over && x >= 260 && x <= 390 && y >= 160 && y <= 250) {
            if (text_pixel((int32_t)x, (int32_t)y, "GAME", 4, 5, 320, 180)) { r=255; g=100; b=100; }
            else if (text_pixel((int32_t)x, (int32_t)y, "OVER", 4, 5, 320, 230)) { r=255; g=100; b=100; }
        }
        { uint8_t pr,pg,pb; if (pitch_overlay_pixel((int32_t)x,(int32_t)y,&pr,&pg,&pb)) { r=pr; g=pg; b=pb; } }

        if (ch == 0) s_pkt[i] = r;
        else if (ch == 1) s_pkt[i] = g;
        else if (ch == 2) s_pkt[i] = b;
        else s_pkt[i] = 0xFF;
    }
}

static glxss_err_t send_game_frame(void)
{
    uint8_t out_ep = find_bulk_out_ep();
    if (out_ep == 0) return GLXSS_ERR_BULK_WRITE;
    game_frame_prepare();
    uint32_t saved_tx_dma = USBHSH->TX_DMA;
    uint16_t tog = g_endp_tog;
    uint16_t tog0 = 0;
    uint16_t tog1 = USBHS_UH_T_TOG_DATA1;
    uint32_t frame_total = 1024016;
    uint32_t pos = 0;
    while (pos < frame_total) {
        uint16_t fill_chunk = 2048;
        if (pos + fill_chunk > frame_total) fill_chunk = (uint16_t)(frame_total - pos);
        fill_game_pkt(pos, fill_chunk);
        uint32_t send_off = 0;
        while (send_off < fill_chunk) {
            uint16_t chunk = 512;
            if (send_off + chunk > fill_chunk) chunk = (uint16_t)(fill_chunk - send_off);
            USBHSH->TX_DMA = (uint32_t)s_pkt + send_off;
            USBHSH->TX_LEN = chunk;
            USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
            USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
            for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
            USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
            if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
            tog ^= USBHS_UH_T_TOG_DATA1;
            send_off += chunk;
        }
        pos += fill_chunk;
    }
    g_endp_tog = tog;
    USBHSH->TX_DMA = saved_tx_dma;
    return GLXSS_OK;
}

static void game_mode_init(void)
{
    g_game_lane = 1;
    g_game_world_y = 0;
    g_game_coins = 0;
    g_game_start_us = tick_get_us();
    g_game_active = 1;
    g_game_over = 0;
    g_game_last_frame_us = 0;
    g_game_last_lane_us = 0;
    g_game_last_pred = 2;
    g_next_coin_y = 5;
    for (int c = 0; c < GAME_MAX_COINS; c++) g_coin_active[c] = 0;
    g_game_dirty = 1;
    g_walk_frame = 0;
    g_walk_timer_us = 0;
    IPC_Log_Printf_V5F("[V5F] GAME init\r\n");
}

static glxss_err_t game_mode_step(void)
{
    uint32_t now = tick_get_us();

    if (g_game_active) {
        uint32_t elapsed = now - g_game_start_us;
        if (elapsed >= (uint32_t)GAME_DURATION_MS * 1000) {
            g_game_active = 0;
            g_game_over = 1;
            g_game_dirty = 1;
            g_game_over_seq++;
            IPC_LOG_SHARED->game_distance = (int32_t)g_game_world_y;
            IPC_LOG_SHARED->game_coins = (int32_t)g_game_coins;
            IPC_LOG_SHARED->game_score = (int32_t)g_game_world_y + (int32_t)g_game_coins * GAME_COIN_VALUE;
            IPC_LOG_SHARED->game_over_seq = g_game_over_seq;
            IPC_Log_Printf_V5F("[V5F] GAME OVER dist=%d coins=%lu score=%d\r\n",
                (int)g_game_world_y, (unsigned long)g_game_coins,
                (int)g_game_world_y + (int)g_game_coins * GAME_COIN_VALUE);
        } else {
            int32_t attn_q100 = IPC_LOG_SHARED->v3f_attn_q100;
            float attn = (float)attn_q100 / 10000.0f;
            if (attn < 0) attn = 0;
            if (attn > 1) attn = 1;
            float speed = GAME_BASE_SPEED_MPS + attn * GAME_MAX_BOOST_MPS;

            float dt = 0.05f;
            if (g_game_last_frame_us > 0) {
                uint32_t diff = now - g_game_last_frame_us;
                if (diff > 0 && diff < 200000) dt = (float)diff / 1000000.0f;
            }
            g_game_world_y += speed * dt;

            g_walk_timer_us += (uint32_t)(dt * 1000000.0f);
            if (g_walk_timer_us >= 150000) {
                g_walk_timer_us = 0;
                g_walk_frame = (g_walk_frame + 1) & 3;
            }

            if (now - g_game_last_lane_us > GAME_LANE_COOLDOWN_US) {
                uint8_t pred = IPC_LOG_SHARED->v5f_pred;
                if (pred != g_game_last_pred && pred != IPC_PRED_UNKNOWN) {
                    g_game_last_pred = pred;
                    g_game_last_lane_us = now;
                    if (pred == IPC_PRED_LEFT && g_game_lane > 0) g_game_lane--;
                    else if (pred == IPC_PRED_RIGHT && g_game_lane < 2) g_game_lane++;
                }
            }

            while (g_next_coin_y < g_game_world_y + GAME_VIEW_RANGE_M) {
                for (int c = 0; c < GAME_MAX_COINS; c++) {
                    if (!g_coin_active[c]) {
                        g_coin_y[c] = g_next_coin_y;
                        g_coin_lane[c] = (uint8_t)((uint32_t)(g_next_coin_y * 37.0f) % 3);
                        g_coin_active[c] = 1;
                        break;
                    }
                }
                g_next_coin_y += GAME_COIN_SPACING_MIN +
                    ((float)((uint32_t)(g_next_coin_y * 91.0f) % 601) / 100.0f);
            }

            for (int c = 0; c < GAME_MAX_COINS; c++) {
                if (!g_coin_active[c]) continue;
                float dy = g_coin_y[c] - g_game_world_y;
                if (dy < 0) dy = -dy;
                if (dy < GAME_COIN_HIT_RADIUS_M && g_coin_lane[c] == g_game_lane) {
                    g_coin_active[c] = 0;
                    g_game_coins++;
                } else if (g_coin_y[c] < g_game_world_y - 5) {
                    g_coin_active[c] = 0;
                }
            }

            if (now - g_game_last_frame_us < GAME_FRAME_INTERVAL_US) return GLXSS_OK;
            g_game_last_frame_us = now;
            return send_game_frame();
        }
    }

    if (!g_game_dirty) return GLXSS_OK;
    g_game_dirty = 0;
    return send_game_frame();
}

/* ==================== FOCUS ANALYSIS + STROOP TRAIN ==================== */

#define STROOP_COLORS   6              /* 红黄绿蓝白黑 */
#define STROOP_PERIOD_US  1000000U     /* 1s switch */

/* One color-word shown at a time; word and color picked randomly,
 * color always differs from the word meaning (Stroop conflict).
 * Index into s_font_cn glyphs (0=红,1=黄,2=绿,3=蓝,4=白,5=黑). */
static uint8_t  g_stroop_word = 0;
static uint8_t  g_stroop_color = 1;
static uint32_t g_stroop_last_us = 0;
static uint32_t g_stroop_rng = 0xACE1u;

static uint32_t stroop_xorshift32(void)
{
    uint32_t x = g_stroop_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_stroop_rng = x;
    return x;
}

static void stroop_pick(void)
{
    uint32_t tick = tick_get_us();
    g_stroop_rng ^= (tick >> 8) ^ (tick << 16);
    stroop_xorshift32();
    g_stroop_word = (uint8_t)(stroop_xorshift32() % STROOP_COLORS);
    uint8_t c;
    do {
        c = (uint8_t)(stroop_xorshift32() % STROOP_COLORS);
    } while (c == g_stroop_word);
    g_stroop_color = c;
}

static void fill_focus_pkt(uint32_t offset, uint16_t len)
{
    uint8_t hdr[16] = {0x80,0x02,0x00,0x00, 0x90,0x01,0x00,0x00, 0x01,0,0,0,0,0,0,0};

    /* color table: 0=红,1=黄,2=绿,3=蓝,4=白,5=黑(深灰,黑背景上可辨) */
    static const uint8_t cr[6] = {255,255,  0,  0,255, 90};
    static const uint8_t cg[6] = {  0,255,255,  0,255, 90};
    static const uint8_t cb[6] = {  0,  0,  0,255,255, 90};

    for (uint16_t i = 0; i < len; i++) {
        uint32_t pos = offset + i;
        if (pos < 16) { s_pkt[i] = hdr[pos]; continue; }
        uint32_t pb = pos - 16;
        uint32_t pi = pb / 4;
        uint8_t ch = pb % 4;
        uint32_t x = pi % 640;
        uint32_t y = pi / 640;
        uint8_t r = 10, g = 12, b = 18;

        /* Single large centered glyph */
        if (text_pixel_cn((int32_t)x, (int32_t)y, g_stroop_word, 8, 320, 200)) {
            r = cr[g_stroop_color];
            g = cg[g_stroop_color];
            b = cb[g_stroop_color];
        }
        { uint8_t pr,pg,pb; if (pitch_overlay_pixel((int32_t)x,(int32_t)y,&pr,&pg,&pb)) { r=pr; g=pg; b=pb; } }

        if (ch == 0) s_pkt[i] = r;
        else if (ch == 1) s_pkt[i] = g;
        else if (ch == 2) s_pkt[i] = b;
        else s_pkt[i] = 0xFF;
    }
}

static glxss_err_t send_focus_frame(void)
{
    uint8_t out_ep = find_bulk_out_ep();
    if (out_ep == 0) return GLXSS_ERR_BULK_WRITE;
    uint32_t saved_tx_dma = USBHSH->TX_DMA;
    uint16_t tog = g_endp_tog;
    uint16_t tog0 = 0;
    uint16_t tog1 = USBHS_UH_T_TOG_DATA1;
    uint32_t frame_total = 1024016;
    uint32_t pos = 0;
    while (pos < frame_total) {
        uint16_t fill_chunk = 2048;
        if (pos + fill_chunk > frame_total) fill_chunk = (uint16_t)(frame_total - pos);
        fill_focus_pkt(pos, fill_chunk);
        uint32_t send_off = 0;
        while (send_off < fill_chunk) {
            uint16_t chunk = 512;
            if (send_off + chunk > fill_chunk) chunk = (uint16_t)(fill_chunk - send_off);
            USBHSH->TX_DMA = (uint32_t)s_pkt + send_off;
            USBHSH->TX_LEN = chunk;
            USBHSH->CONTROL = USBHS_UH_HOST_ACTION | USB_PID_OUT | (out_ep << 4) | (tog ? tog1 : tog0);
            USBHSH->INT_FLAG = USBHS_UHIF_TRANSFER;
            for (uint16_t i = 20000; (i != 0) && ((USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER) == 0); i--);
            USBHSH->CONTROL &= ~USBHS_UH_T_TOKEN_MASK;
            if (!(USBHSH->INT_FLAG & USBHS_UHIF_TRANSFER)) { USBHSH->TX_DMA = saved_tx_dma; return GLXSS_ERR_BULK_WRITE; }
            tog ^= USBHS_UH_T_TOG_DATA1;
            send_off += chunk;
        }
        pos += fill_chunk;
    }
    g_endp_tog = tog;
    USBHSH->TX_DMA = saved_tx_dma;
    return GLXSS_OK;
}

static void focus_mode_init(void)
{
    g_stroop_word = 0;
    g_stroop_color = 1;
    g_stroop_last_us = tick_get_us();
    stroop_pick();
    IPC_LOG_SHARED->ssvep_mode_active = 0u;

    IPC_Log_Printf_V5F("[V5F] FOCUS mode init\r\n");
}

static glxss_err_t focus_mode_step(void)
{
    uint32_t now = tick_get_us();
    if (now - g_stroop_last_us >= STROOP_PERIOD_US) {
        g_stroop_last_us = now;
        stroop_pick();
    }


    return send_focus_frame();
}

/* ==================== MI SIM ==================== */

static void __attribute__((unused)) mi_sim_step(void)
{
    if (!g_mi_infer_active) return;
    uint32_t now = tick_get_us();
    if (now - g_mi_sim_last_us < 2000000) return;
    g_mi_sim_last_us = now;
    g_mi_infer_count++;
    uint32_t t = g_mi_infer_count;
    int32_t bias = (int32_t)((t * 137) % 10001) - 5000;
    g_mi_sim_sr = 5000 + bias;
    g_mi_sim_sl = 10000 - g_mi_sim_sr;
    int32_t conf = g_mi_sim_sr - g_mi_sim_sl;
    if (conf < 0) conf = -conf;
    if (conf < 300) g_mi_sim_pred = IPC_PRED_UNKNOWN;
    else g_mi_sim_pred = (g_mi_sim_sr >= g_mi_sim_sl) ? IPC_PRED_RIGHT : IPC_PRED_LEFT;
    IPC_SetMIResult_V5F(g_mi_sim_pred, g_mi_sim_sl, g_mi_sim_sr, conf);
    if (g_mode == MODE_ARROW && g_mi_sim_pred != IPC_PRED_UNKNOWN)
        g_test_new_result = 1;
}

/* ==================== SSVEP SIM ==================== */

static uint32_t g_ssvep_sim_timer = 0;
static int8_t g_ssvep_sim_idx = -1;

static void __attribute__((unused)) ssvep_sim_step(void)
{
    uint32_t ctrl = IPC_Ctrl_GetFlags_V5F();
    if (!(ctrl & IPC_CTRL_SSVEP_ENABLE)) return;
    g_ssvep_sim_timer++;
    if (g_ssvep_sim_timer >= 500) {
        g_ssvep_sim_timer = 0;
        if (ctrl & IPC_CTRL_SSVEP_SELFTEST) {
            g_ssvep_sim_idx = (int8_t)((g_mi_infer_count + 1) % 4);
        } else {
            g_ssvep_sim_idx = (g_ssvep_sim_idx + 1) % 4;
        }
        int32_t scores[4] = {0, 0, 0, 0};
        scores[g_ssvep_sim_idx] = 8000;
        IPC_SetSSVEPResult_V5F(g_ssvep_sim_idx, scores);
    }
}

/* ==================== COLLECT SIM ==================== */

static uint32_t g_collect_last_us = 0;
static uint32_t g_collect_seed = 1;
static uint8_t g_collect_mode = 0;

static uint32_t lcg_next(void)
{
    g_collect_seed = g_collect_seed * 1664525u + 1013904223u;
    return g_collect_seed;
}

static void __attribute__((unused)) collect_sim_step(void)
{
    if (g_mode != MODE_ARROW_TRAIN || g_collect_mode == 0) return;
    uint32_t now = tick_get_us();
    if (now - g_collect_last_us < 2000000) return;
    g_collect_last_us = now;
    IPC_IncSample_V5F(250);
    {
        int32_t feats[IPC_FEATURE_DIM];
        for (int i = 0; i < IPC_FEATURE_DIM; i++) {
            int32_t base = (i < 14) ? 1000 : 0;
            int32_t noise = (int32_t)(lcg_next() % 2001) - 1000;
            feats[i] = base + noise;
        }
        feats[0] += (g_train_dir == 0) ? 3000 : -3000;
        feats[7] += (g_train_dir == 0) ? -2000 : 2000;
        IPC_SetFeature_V5F(feats);
        IPC_Log_Printf_V5F("[V5F] FEAT seq=%lu\r\n", (unsigned long)IPC_LOG_SHARED->feature_seq);
    }
}

static void collect_mode_enter(uint32_t mode)
{
    g_collect_mode = (uint8_t)mode;
    g_collect_last_us = 0;
    g_collect_seed = 1;
    IPC_Log_Printf_V5F("[V5F] COLLECT mode=%lu (sim features)\r\n", (unsigned long)mode);
}

/* ==================== MODE SWITCH ==================== */

static uint32_t g_last_disp_us = 0;

static void switch_mode(uint8_t new_mode)
{
    if (new_mode == g_mode) return;
    IPC_Log_Printf_V5F("[V5F] switch %d->%d, reset toggle\r\n", g_mode, new_mode);
    reset_toggle_light();
    g_mode = new_mode;
    g_last_disp_us = 0;
    IPC_LOG_SHARED->ssvep_mode_active = (new_mode == MODE_SSVEP) ? 1u : 0u;
    switch (g_mode) {
    case MODE_SSVEP:       ssvep_mode_init(); break;
    case MODE_ARROW:       arrow_mode_init(); break;
    case MODE_ARROW_TRAIN: train_mode_init(); break;
    case MODE_GAME:        game_mode_init(); break;
    case MODE_FOCUS:       focus_mode_init(); break;
    }
}

static void handle_ipc_cmd(void)
{
    uint32_t param;
    uint32_t cmd = IPC_Cmd_Recv_V5F(&param);
    if (cmd == IPC_CMD_NONE) return;
    IPC_Log_Printf_V5F("[V5F] cmd=%lu param=%lu\r\n", (unsigned long)cmd, (unsigned long)param);
    uint32_t ctrl = IPC_Ctrl_GetFlags_V5F();
    if (ctrl & IPC_CTRL_RESET_DSP) {
        IPC_Log_Printf_V5F("[V5F] RESET DSP\r\n");
        g_mi_infer_count = 0;
        g_mi_sim_pred = IPC_PRED_UNKNOWN;
        g_mi_infer_active = 0;
        g_mi_sim_last_us = 0;
        g_ssvep_sim_timer = 0;
        g_ssvep_sim_idx = -1;
        g_collect_mode = 0;
        g_collect_last_us = 0;
        IPC_Ctrl_SetFlags_V5F(ctrl & ~IPC_CTRL_RESET_DSP);
    }
    switch (cmd) {
    case IPC_CMD_SSVEP:
        switch_mode(MODE_SSVEP);
        if (param < 4) ssvep_mode_set_freq(param);
        break;
    case IPC_CMD_ARROW:
        if (g_mode != MODE_ARROW) switch_mode(MODE_ARROW);
        g_arrow_tgt_dir = (uint8_t)(param & 1u);
        g_arrow_tgt_color = 0;   /* white: 采集期间目标箭头为白色 */
        if (g_arrow_phase == ARROW_PHASE_RESULT || g_arrow_phase == ARROW_PHASE_GAP) {
            /* 结果/消失相位中: 只更新方向, 等相位自然结束再显示新目标 */
            g_arrow_dirty = 1;
        } else {
            g_arrow_tgt_vis = 1;
            g_arrow_res_vis = 0;
            g_arrow_phase = ARROW_PHASE_TARGET;
            g_arrow_phase_start = tick_get_us();
            g_arrow_dirty = 1;
        }
        g_last_disp_us = 0;   /* 立即刷新显示 */
        break;
    case IPC_CMD_ARROW_TRAIN:
        if (g_mode != MODE_ARROW_TRAIN) switch_mode(MODE_ARROW_TRAIN);
        g_train_active = 1;
        if (param <= 1) {
            g_train_dir = (uint8_t)param;
            g_train_arrow_vis = 1;
            g_train_progress = 0;
            g_train_phase_start = tick_get_us();
            IPC_Log_Printf_V5F("[V5F] TRAIN arrow dir=%s\r\n", param ? "RIGHT" : "LEFT");
        } else {
            g_train_arrow_vis = 0;
            g_train_progress = 0;
            g_train_phase_start = tick_get_us();
            IPC_Log_Printf_V5F("[V5F] TRAIN rest\r\n");
        }
        break;
    case IPC_CMD_MI_INFER:
        IPC_Log_Printf_V5F("[V5F] MI_INFER mode\r\n");
        g_mi_infer_active = 1;
        switch_mode(MODE_ARROW);
        break;
    case IPC_CMD_MI_RESULT:
        /* V3F 最终投票结果命令: 显示结果箭头并变色, 1.5s后消失(200ms) */
        if (g_mode != MODE_ARROW) switch_mode(MODE_ARROW);
        g_mi_sim_pred = (uint8_t)(param & 1u);
        g_mi_sim_sl = 5000;
        g_mi_sim_sr = 5000;
        if (g_mi_sim_pred == IPC_PRED_RIGHT) { g_mi_sim_sl = 3000; g_mi_sim_sr = 7000; }
        else if (g_mi_sim_pred == IPC_PRED_LEFT) { g_mi_sim_sl = 7000; g_mi_sim_sr = 3000; }
        g_test_new_result = 1;
        g_last_disp_us = 0;   /* 立即刷新显示 */
        IPC_Log_Printf_V5F("[V5F] RESULT %s\r\n", g_mi_sim_pred ? "RIGHT" : "LEFT");
        break;
    case IPC_CMD_COLLECT:
        IPC_Log_Printf_V5F("[V5F] COLLECT cmd param=%lu\r\n", (unsigned long)param);
        collect_mode_enter(param ? param : 1);
        switch_mode(MODE_ARROW_TRAIN);
        break;
    case IPC_CMD_SSVEP_CTRL:
        IPC_Log_Printf_V5F("[V5F] SSVEP_CTRL flags=0x%02X\r\n", (unsigned)ctrl);
        switch_mode(MODE_SSVEP);
        break;
    case IPC_CMD_GAME:
        IPC_Log_Printf_V5F("[V5F] GAME param=%lu\r\n", (unsigned long)param);
        if (param) {
            switch_mode(MODE_GAME);
        } else {
            g_game_active = 0;
        }
        break;
    case IPC_CMD_FOCUS:
        IPC_Log_Printf_V5F("[V5F] FOCUS param=%lu\r\n", (unsigned long)param);
        if (param) {
            switch_mode(MODE_FOCUS);
        } else {
            switch_mode(MODE_IDLE);
        }
        break;

    case IPC_CMD_RESET:
        IPC_Log_Printf_V5F("[V5F] RESET -> IDLE\r\n");
        g_mi_infer_active = 0;
        g_collect_mode = 0;
        switch_mode(MODE_IDLE);
        break;
    default: break;
    }
}

#endif /* V5F_MODE_GLXSS || GLXSS_ENABLED */

/* ==================== MAIN ==================== */

int main(void)
{
    SystemAndCoreClockUpdate();
    Delay_Init();
    HSEM_FastTake(HSEM_ID0);
    HSEM_ReleaseOneSem(HSEM_ID0, 0);

#if defined(V5F_MODE_GLXSS)
    IPC_Log_Init_V5F();
    IPC_Log_Printf_V5F("[V5F] boot GLXSS mode, SystemCoreClk:%d\r\n", SystemCoreClock);
    Delay_Ms(500);
    Hardware();
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_USB_INIT);
    IPC_Log_Printf_V5F("[V5F] USBHS Host init...\r\n");
    glxss_usb_host_init();
    IPC_Log_Printf_V5F("[V5F] USBHS Host init done, PORT=0x%08lX\r\n", (unsigned long)USBHSH->PORT_STATUS);
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_USB_WAIT);
    IPC_Log_Printf_V5F("[V5F] Waiting for display device...\r\n");
    glxss_err_t err = glxss_usb_wait_device(GLXSS_DEVICE_VID, GLXSS_DEVICE_PID, 30000);
    if (err != GLXSS_OK) {
        IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_ERROR);
        IPC_Log_Printf_V5F("[V5F] Device wait failed: %d\r\n", err);
        while (1) {}
    }
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_USB_READY);
    IPC_Log_Printf_V5F("[V5F] Device found! Clearing halt...\r\n");
    glxss_usb_clear_halt(GLXSS_EP_DATA_OUT);
    g_endp_tog = 0;
    tick_init();
    IPC_Log_Printf_V5F("[V5F] TIM4 calibrated: %lu ticks/ms\r\n", (unsigned long)g_ticks_per_ms);
    ssvep_precompute();
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_IDLE);
    IPC_Log_Printf_V5F("[V5F] IDLE - waiting for V3F commands...\r\n");

    for (;;) {
        handle_ipc_cmd();
        mi_sim_step();
        ssvep_sim_step();
        collect_sim_step();
        err = GLXSS_OK;
        switch (g_mode) {
        case MODE_IDLE:        err = idle_mode_step(); break;
        case MODE_SSVEP:       err = ssvep_mode_step(); break;
        case MODE_ARROW:       err = arrow_mode_step(); break;
        case MODE_ARROW_TRAIN: err = train_mode_step(); break;
        case MODE_GAME:        err = game_mode_step(); break;
        case MODE_FOCUS:       err = focus_mode_step(); break;
        }
        if (err != GLXSS_OK) {
            IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_ERROR);
            IPC_Log_Printf_V5F("[V5F] FAIL err=%d\r\n", err);
            break;
        }
    }
    while (1) {}

#elif defined(GLXSS_ENABLED)
    DualCore_IPC_Init_V5F();
    IPC_Log_Init_V5F();
    IPC_Log_Printf_V5F("[V5F] boot EEG+GLXSS merged mode, SystemCoreClk:%d\r\n", SystemCoreClock);
    Delay_Ms(500);
    Hardware();
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_USB_INIT);
    IPC_Log_Printf_V5F("[V5F] USBHS Host init...\r\n");
    glxss_usb_host_init();
    IPC_Log_Printf_V5F("[V5F] USBHS Host init done, PORT=0x%08lX\r\n", (unsigned long)USBHSH->PORT_STATUS);
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_USB_WAIT);
    IPC_Log_Printf_V5F("[V5F] Waiting for display device...\r\n");
    glxss_err_t err = glxss_usb_wait_device(GLXSS_DEVICE_VID, GLXSS_DEVICE_PID, 30000);
    if (err != GLXSS_OK) {
        IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_ERROR);
        IPC_Log_Printf_V5F("[V5F] Device wait failed: %d, EEG-only mode\r\n", err);
    } else {
        IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_USB_READY);
        IPC_Log_Printf_V5F("[V5F] Device found! Clearing halt...\r\n");
        glxss_usb_clear_halt(GLXSS_EP_DATA_OUT);
        g_endp_tog = 0;
        tick_init();
        IPC_Log_Printf_V5F("[V5F] TIM4 calibrated: %lu ticks/ms\r\n", (unsigned long)g_ticks_per_ms);
        ssvep_precompute();
    }
    IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_IDLE);
    IPC_Log_Printf_V5F("[V5F] EEG+GLXSS main loop\r\n");

    for (;;) {
        DualCore_V5F_MainLoopProcess();
        handle_ipc_cmd();

        err = GLXSS_OK;
        if (g_mode == MODE_SSVEP) {
            err = ssvep_mode_step();
        } else {
            uint32_t now = tick_get_us();
            if (g_mode == MODE_ARROW) {
                /* 等头模式: 每次循环都检查相位转换(RESULT/GAP 400ms需及时切换),
                   脏标志保证无变化时不发送1MB帧 */
                err = arrow_mode_step();
            } else if (g_mode == MODE_GAME) {
                err = game_mode_step();
            } else if (g_mode == MODE_FOCUS) {
                err = focus_mode_step();
            } else if (now - g_last_disp_us >= 1000000) {
                g_last_disp_us = now;
                switch (g_mode) {
                case MODE_IDLE:        err = idle_mode_step(); break;
                case MODE_ARROW_TRAIN: err = train_mode_step(); break;
                default: break;
                }
            }
            DualCore_V5F_SSVEP_RunPending();
        }

        if (err != GLXSS_OK) {
            IPC_Log_SetStatus_V5F(IPC_LOG_STATUS_ERROR);
            IPC_Log_Printf_V5F("[V5F] FAIL err=%d, USB recovery\r\n", err);
            reset_toggle_full();
            Delay_Ms(10);
            continue;
        }

    }

    while (1) {}

#else
    DualCore_IPC_Init_V5F();

    while (1)
    {
        DualCore_V5F_MainLoopProcess();
        DualCore_V5F_SSVEP_RunPending();
        __WFI();
    }
#endif
}
