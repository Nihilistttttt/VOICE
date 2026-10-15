#include "gesture_sm.h"

/*============================================================================
 * 手势状态机实现 —— 从 Mode_HidMouse 内联逻辑等价移植
 * 每个状态机的转移条件/阈值/次序与原实现逐行对应, 除注明外无行为变更。
 *============================================================================*/

/*----------------------------------------------------------------------------
 * s1: 吹=左键（按下 ±250 / 释放 150, 另一口空闲窗口 ±100, 去抖 2 tick）
 *     吸=短吸中键点击 / 吸住≥500ms 音量模式
 *---------------------------------------------------------------------------*/
void s1_sm_init(s1_sm_t *sm)
{
    sm->state = S1_IDLE;
    sm->btn = 0;
    sm->blow_n = 0;
    sm->sip_n = 0;
    sm->sip_ms = 0;
    sm->rclick_pend = 0;
}

void s1_sm_step(s1_sm_t *sm, int32_t p1, int32_t p2, s1_out_t *out)
{
    out->btn = sm->btn;
    out->volume_active = (sm->state == S1_SIP_VOLUME);

    /* 吹: 过阈值且另一口空闲 → 去抖置左键; 已按下且回落 → 释放 */
    if (!(sm->btn & 0x01) && p1 >= GS_PRESS_ON && p2 > -GS_IDLE_WIN && p2 < GS_IDLE_WIN) {
        if (++sm->blow_n >= GS_DEBOUNCE) { sm->btn |= 0x01; sm->blow_n = GS_DEBOUNCE; }
    } else if ((sm->btn & 0x01) && p1 <= GS_PRESS_OFF) {
        sm->btn &= (uint8_t)~0x01;
        sm->blow_n = 0;
    } else if (!(sm->btn & 0x01)) {
        sm->blow_n = 0;
    }

    /* 吸: 状态机三态（原实现保留 !(btn&0x02) 死条件, 等价移植）*/
    if (sm->state == S1_IDLE) {
        if (!(sm->btn & 0x02) && p1 <= -GS_PRESS_ON && p2 > -GS_IDLE_WIN && p2 < GS_IDLE_WIN) {
            if (++sm->sip_n >= GS_DEBOUNCE) { sm->state = S1_SIP_TIMING; sm->sip_ms = 0; sm->sip_n = GS_DEBOUNCE; }
        } else {
            sm->sip_n = 0;
        }
    } else if (sm->state == S1_SIP_TIMING) {
        if (p1 >= -GS_PRESS_OFF) {
            if (sm->sip_ms < GS_LONG_MS) sm->rclick_pend = 2;   /* 短吸 = 中键点击 */
            sm->state = S1_IDLE;
            sm->sip_n = 0;
        } else {
            sm->sip_ms += GS_TICK_MS;
            if (sm->sip_ms >= GS_LONG_MS) sm->state = S1_SIP_VOLUME;   /* 吸住 → 音量 */
        }
    } else {   /* S1_SIP_VOLUME */
        if (p1 >= -GS_PRESS_OFF) {   /* 松口退出音量 */
            sm->state = S1_IDLE;
            sm->sip_n = 0;
        }
    }

    /* 中键点击两相: 按下 → (下一 tick)释放 */
    if (sm->rclick_pend == 2) {
        sm->btn |= 0x04;              /* 中键 = EMUI 返回（右键无返回语义）*/
        sm->rclick_pend = 1;
    } else if (sm->rclick_pend == 1) {
        sm->btn &= (uint8_t)~0x04;
        sm->rclick_pend = 0;
    }

    out->btn = sm->btn;
    out->volume_active = (sm->state == S1_SIP_VOLUME);
}

/*----------------------------------------------------------------------------
 * scroll: 双吹=上滚(-1) 双吸=下滚(+1); 先判进入/退出, 后限速输出
 * （进入当拍不输出 —— 与原实现 ++n 次序一致）
 *---------------------------------------------------------------------------*/
void scroll_sm_init(scroll_sm_t *sm)
{
    sm->active = 0;
    sm->dir = 0;
    sm->n = 0;
}

int8_t scroll_sm_step(scroll_sm_t *sm, int32_t p1, int32_t p2)
{
    int8_t wheel = 0;

    if (!sm->active) {
        if (p1 >= GS_SCROLL_ON && p2 >= GS_SCROLL_ON) {
            sm->active = 1; sm->dir = -1; sm->n = 0;       /* 双吹 = 上滚 */
        } else if (p1 <= -GS_SCROLL_ON && p2 <= -GS_SCROLL_ON) {
            sm->active = 1; sm->dir = 1; sm->n = 0;        /* 双吸 = 下滚 */
        }
    } else {
        int32_t a1 = (p1 < 0) ? -p1 : p1;
        int32_t a2 = (p2 < 0) ? -p2 : p2;
        if (a1 < GS_SCROLL_OFF && a2 < GS_SCROLL_OFF) {
            sm->active = 0; sm->dir = 0; sm->n = 0;
        }
    }

    if (sm->active && ++sm->n >= 2) {   /* 每 2 tick（约100ms）一格 */
        sm->n = 0;
        wheel = sm->dir;
    }
    return wheel;
}

/*----------------------------------------------------------------------------
 * s2: 短吹松口=Alt+Tab, 短吸松口=回主页; 按住≥500ms 松口无动作（预留）
 *---------------------------------------------------------------------------*/
void s2_sm_init(s2_sm_t *sm)
{
    sm->mode = 0;
    sm->ms = 0;
    sm->blow_n = 0;
    sm->sip_n = 0;
}

s2_cmd_t s2_sm_step(s2_sm_t *sm, int32_t p2)
{
    s2_cmd_t cmd = S2_NONE;

    if (sm->mode == 0) {
        if (p2 >= GS_PRESS_ON) {
            if (++sm->blow_n >= GS_DEBOUNCE) { sm->mode = 1; sm->ms = 0; sm->blow_n = GS_DEBOUNCE; }
        } else if (p2 <= -GS_PRESS_ON) {
            if (++sm->sip_n >= GS_DEBOUNCE) { sm->mode = 2; sm->ms = 0; sm->sip_n = GS_DEBOUNCE; }
        } else {
            sm->blow_n = 0;
            sm->sip_n = 0;
        }
    } else if (sm->ms < GS_LONG_MS) {                 /* 按住计时 */
        if (sm->mode == 1 && p2 <= GS_PRESS_OFF) {
            cmd = S2_ALTTAB;                          /* 短吹松口 → Alt+Tab */
            sm->mode = 0;
            sm->blow_n = 0;
        } else if (sm->mode == 2 && p2 >= -GS_PRESS_OFF) {
            cmd = S2_HOME;                            /* 短吸松口 → 回主页 */
            sm->mode = 0;
            sm->sip_n = 0;
        } else {
            sm->ms += GS_TICK_MS;
        }
    } else {                                          /* 按住≥500ms: 无动作 */
        if (sm->mode == 1 && p2 <= GS_PRESS_OFF) sm->mode = 0;
        else if (sm->mode == 2 && p2 >= -GS_PRESS_OFF) sm->mode = 0;
    }
    return cmd;
}

/*----------------------------------------------------------------------------
 * kb: 键盘两相发送（按下 → 下一 tick 释放）
 *---------------------------------------------------------------------------*/
void kb_sm_init(kb_sm_t *sm)
{
    sm->pend = 0;
    sm->mod = 0;
    sm->key = 0;
}

void kb_sm_press(kb_sm_t *sm, uint8_t mod, uint8_t key)
{
    sm->mod = mod;
    sm->key = key;
    sm->pend = 1;
}

void kb_sm_step(kb_sm_t *sm, kb_out_t *out)
{
    out->evt = 0;
    out->mod = 0;
    out->key = 0;

    if (sm->pend == 1) {
        out->evt = 1;
        out->mod = sm->mod;
        out->key = sm->key;
        sm->pend = 2;
    } else if (sm->pend == 2) {
        out->evt = 2;                                 /* 释放帧: 调用方发全 0 */
        sm->pend = 0;
    }
}

/*----------------------------------------------------------------------------
 * home: 回主页序列（CLICKNAV）
 *   FLOOD ×12 → X ×5 → Y ×1 → DOWN ×1 → HOLD ×1(释放) → IDLE
 *---------------------------------------------------------------------------*/
void home_sm_init(home_sm_t *sm)
{
    sm->phase = HOME_IDLE;
    sm->n = 0;
}

void home_sm_start(home_sm_t *sm)
{
    sm->phase = HOME_FLOOD;
    sm->n = 0;
}

void home_sm_abort(home_sm_t *sm)   /* 审计 F3: 断连时未完成序列作废 */
{
    sm->phase = HOME_IDLE;
    sm->n = 0;
}

void home_sm_step(home_sm_t *sm, home_out_t *out)
{
    out->move = 0;
    out->dx = 0;
    out->dy = 0;
    out->left_btn = -1;

    switch (sm->phase) {
    case HOME_FLOOD:   /* 冲左下角: X 左满幅 + Y 下满幅（边界钳位）*/
        out->move = 1;
        out->dx = -128;
        out->dy = 127;
        if (++sm->n >= HOME_FLOOD_TICKS) { sm->phase = HOME_X; sm->n = 0; }
        break;
    case HOME_X:       /* X 右移到屏幕中心（分 5 帧发, 防指针加速度放大）*/
        out->move = 1;
        out->dx = (int8_t)(HOME_X_CENTER_COUNTS / 5);
        if (++sm->n >= HOME_X_TICKS) { sm->phase = HOME_Y; sm->n = 0; }
        break;
    case HOME_Y:       /* Y 微调: 进入主页键中心 */
        out->move = 1;
        out->dy = (int8_t)(-HOME_Y_NAV_OFFSET);
        sm->phase = HOME_DOWN;
        break;
    case HOME_DOWN:    /* 左键按下 */
        out->left_btn = 1;
        sm->phase = HOME_HOLD;
        break;
    case HOME_HOLD:    /* 左键释放 → 主页键点击 = 回主页 */
        out->left_btn = 0;
        sm->phase = HOME_IDLE;
        break;
    default:
        sm->phase = HOME_IDLE;
        break;
    }
}

uint16_t home_sm_pacing_ms(home_phase_t phase)
{
    switch (phase) {
    case HOME_FLOOD:  return 0;    /* 尽快: 调用方 busy-wait 端点空闲 */
    case HOME_X:
    case HOME_Y:
    case HOME_DOWN:   return 25;   /* 定位/点击段: 25ms/帧（计数≈距离标定节奏）*/
    case HOME_HOLD:   return 30;   /* 点击保持: 太短怕 EMUI 不识别成点击 */
    default:          return 50;
    }
}
