/*============================================================================
 * 手势状态机 —— 基于 Zephyr SMF 的实现（纯逻辑, 无硬件依赖）
 *
 * 文件说明:
 *   本文件是手势业务的状态机集合, 由 v1 手写版（commit 7292d58）原地
 *   重写而来: 外部 API、公共镜像字段、对外行为完全不变, 状态编排改由
 *   Zephyr SMF（Common/smf.c, Apache-2.0 移植版）驱动:
 *     - init:  smf_set_initial() 进入初始态（层次机沿 initial 链下探）
 *     - step:  smf_run_state()   每 tick 执行当前状态的 run 动作
 *     - 转移:  处理函数内 smf_set_state()（SMF 自动编排 exit/entry）
 *
 * 行为等价性:
 *   hosttest/test_gesture_sm.c 的 125 条断言未改动一字而全部通过——
 *   这是本重写的等价性证明。唯一已知语义强化: home 序列重入时相内
 *   计数清零（v1 起已如此, 由 entry 动作天然保证）。
 *
 * 设计要点（为什么 s1 不用父状态）:
 *   SMF 规定"子状态发生转移的当拍, 祖先的 run 被跳过"。s1 的吹气迟滞
 *   与中键两相是跨状态每拍逻辑, 放父状态会让"短吸释放当拍即出中键
 *   按下位"晚一拍——故 s1 采用扁平三状态, 每状态 run 内按原始内联顺序
 *   调用共享静态助手（blow_update → 吸进程 → rclick_update）。
 *
 * 模块清单（自上而下）: s1 单口手势 / scroll 双口滚动 / s2 第二口手势 /
 * kb 键盘两相 / home 回主页序列。接口详见 gesture_sm.h。
 *============================================================================*/

#include "gesture_sm.h"

/*============================================================================
 * s1 —— 单口手势
 *   状态: IDLE(无手势) / TIMING(吸住计时) / VOLUME(音量模式)
 *   转移: IDLE→TIMING(吸去抖达标) TIMING→IDLE(松口, 短吸发中键两相)
 *         TIMING→VOLUME(吸住≥500ms) VOLUME→IDLE(松口)
 *   共享逻辑: blow_update(吹气迟滞, 每拍) / rclick_update(中键两相, 每拍)
 *   —— 三个状态的 run 都按 [吹 → 吸进程 → 中键两相] 的原始顺序调用。
 *============================================================================*/

/* 状态表索引（与状态枚举解耦, 仅本文件使用）*/
enum { GS1_IDLE = 0, GS1_TIMING, GS1_VOLUME, GS1_NUM };

/* 前置声明: 状态表需要处理函数指针 */
static void gs1_idle_entry(void *obj);
static enum smf_state_result gs1_idle_run(void *obj);
static void gs1_timing_entry(void *obj);
static enum smf_state_result gs1_timing_run(void *obj);
static void gs1_volume_entry(void *obj);
static enum smf_state_result gs1_volume_run(void *obj);

/* 状态表: ROOT 仅作层次根承载 initial 转移, 无动作 */
static const struct smf_state gs1_states[GS1_NUM] = {
    [GS1_IDLE]   = SMF_CREATE_STATE(gs1_idle_entry,   gs1_idle_run,   NULL, NULL, NULL),
    [GS1_TIMING] = SMF_CREATE_STATE(gs1_timing_entry, gs1_timing_run, NULL, NULL, NULL),
    [GS1_VOLUME] = SMF_CREATE_STATE(gs1_volume_entry, gs1_volume_run, NULL, NULL, NULL),
};

/**
 * @brief 吹气迟滞更新（每拍共享逻辑, 等价于原始内联的吹气块）
 *
 * 按下: 未按 && p1≥250 && 另一口空闲(|p2|<100) → 去抖 2 拍置左键位
 * 释放: 已按 && p1≤150（迟滞下沿）→ 清左键位
 * 其余: 未按且条件不满足 → 去抖计数清零
 *
 * @param sm 状态机
 * @param p1 s1 口压力 Pa
 * @param p2 s2 口压力 Pa（跨口保护窗口用）
 */
static void gs1_blow_update(s1_sm_t *sm, int32_t p1, int32_t p2)
{
    if (!(sm->btn & GS_BTN_LEFT) && p1 >= GS_PRESS_ON && p2 > -GS_IDLE_WIN && p2 < GS_IDLE_WIN) {
        if (++sm->blow_n >= GS_DEBOUNCE) {
            sm->btn |= GS_BTN_LEFT;
            sm->blow_n = GS_DEBOUNCE;   /* 钳在阈值, 防溢出且便于断言 */
        }
    } else if ((sm->btn & GS_BTN_LEFT) && p1 <= GS_PRESS_OFF) {
        sm->btn &= (uint8_t)~GS_BTN_LEFT;
        sm->blow_n = 0;
    } else if (!(sm->btn & GS_BTN_LEFT)) {
        sm->blow_n = 0;
    }
}

/**
 * @brief 中键两相更新（每拍共享逻辑, 等价于原始内联的中键块）
 *
 * rclick_pend==2 → 置中键位并进入"待释放"; ==1 → 清中键位完成一次点击。
 * 两相各占一拍, 构成主机侧可识别的一次完整点击。
 *
 * @param sm 状态机
 */
static void gs1_rclick_update(s1_sm_t *sm)
{
    if (sm->rclick_pend == 2) {
        sm->btn |= GS_BTN_MIDDLE;            /* 中键 = EMUI 返回（右键无返回语义）*/
        sm->rclick_pend = 1;
    } else if (sm->rclick_pend == 1) {
        sm->btn &= (uint8_t)~GS_BTN_MIDDLE;
        sm->rclick_pend = 0;
    }
}

/* IDLE 入口: 维护镜像字段 */
static void gs1_idle_entry(void *obj)
{
    s1_sm_t *sm = (s1_sm_t *)obj;
    sm->state = S1_IDLE;
}

/* IDLE 运行: 吹气更新 → 吸 engage 检测 → 中键两相（原始顺序）*/
static enum smf_state_result gs1_idle_run(void *obj)
{
    s1_sm_t *sm = (s1_sm_t *)obj;

    gs1_blow_update(sm, sm->p1_tick, sm->p2_tick);

    /* 吸 engage: 原实现保留的 !(btn&0x02) 条件为死条件（0x02 位无人置位）,
     * 等价移植原样保留 */
    if (!(sm->btn & GS_BTN_RIGHT) && sm->p1_tick <= -GS_PRESS_ON &&
        sm->p2_tick > -GS_IDLE_WIN && sm->p2_tick < GS_IDLE_WIN) {
        if (++sm->sip_n >= GS_DEBOUNCE) {
            sm->sip_n = GS_DEBOUNCE;
            smf_set_state(&sm->ctx, &gs1_states[GS1_TIMING]);   /* entry 清 sip_ms */
        }
    } else {
        sm->sip_n = 0;
    }

    gs1_rclick_update(sm);
    return SMF_EVENT_HANDLED;
}

/* TIMING 入口: 计时清零 + 镜像 */
static void gs1_timing_entry(void *obj)
{
    s1_sm_t *sm = (s1_sm_t *)obj;
    sm->state = S1_SIP_TIMING;
    sm->sip_ms = 0;
}

/* TIMING 运行: 吹气更新 → 吸计时/松口判定 → 中键两相（原始顺序）*/
static enum smf_state_result gs1_timing_run(void *obj)
{
    s1_sm_t *sm = (s1_sm_t *)obj;

    gs1_blow_update(sm, sm->p1_tick, sm->p2_tick);

    if (sm->p1_tick >= -GS_PRESS_OFF) {
        /* 松口: 短吸(计时未达长按阈值)=中键点击; 计时归零回 IDLE */
        if (sm->sip_ms < GS_LONG_MS) {
            sm->rclick_pend = 2;    /* 当拍由本 run 尾部的 rclick_update 按下 */
        }
        sm->sip_n = 0;
        smf_set_state(&sm->ctx, &gs1_states[GS1_IDLE]);
    } else {
        sm->sip_ms += GS_TICK_MS;
        if (sm->sip_ms >= GS_LONG_MS) {
            smf_set_state(&sm->ctx, &gs1_states[GS1_VOLUME]);   /* 吸住 → 音量 */
        }
    }

    gs1_rclick_update(sm);
    return SMF_EVENT_HANDLED;
}

/* VOLUME 入口: 镜像 */
static void gs1_volume_entry(void *obj)
{
    s1_sm_t *sm = (s1_sm_t *)obj;
    sm->state = S1_SIP_VOLUME;
}

/* VOLUME 运行: 吹气更新 → 松口退出 → 中键两相（原始顺序）*/
static enum smf_state_result gs1_volume_run(void *obj)
{
    s1_sm_t *sm = (s1_sm_t *)obj;

    gs1_blow_update(sm, sm->p1_tick, sm->p2_tick);

    if (sm->p1_tick >= -GS_PRESS_OFF) {
        sm->sip_n = 0;      /* 松口退出音量, 不发中键（长吸≠返回）*/
        smf_set_state(&sm->ctx, &gs1_states[GS1_IDLE]);
    }

    gs1_rclick_update(sm);
    return SMF_EVENT_HANDLED;
}

/**
 * @brief 初始化 s1 状态机（零化全部字段 → SMF 进入 IDLE）
 *
 * @param sm 状态机
 */
void s1_sm_init(s1_sm_t *sm)
{
    sm->state = S1_IDLE;
    sm->btn = 0;
    sm->blow_n = 0;
    sm->sip_n = 0;
    sm->sip_ms = 0;
    sm->rclick_pend = 0;
    sm->p1_tick = 0;
    sm->p2_tick = 0;
    smf_set_initial(&sm->ctx, &gs1_states[GS1_IDLE]);
}

/**
 * @brief s1 每 tick 推进一步
 *
 * 压力值先存入 tick 槽（SMF 处理函数经 obj 访问), 再跑 SMF;
 * 输出在 run 之后从镜像字段读取——保证转移当拍的按键位也生效。
 *
 * @param sm 状态机
 * @param p1 s1 口压力 Pa
 * @param p2 s2 口压力 Pa
 * @param out 输出: 按键位组合 + 音量模式标志
 */
void s1_sm_step(s1_sm_t *sm, int32_t p1, int32_t p2, s1_out_t *out)
{
    sm->p1_tick = p1;   /* 本 tick 输入存槽, 供各 run 共享 */
    sm->p2_tick = p2;

    (void)smf_run_state(&sm->ctx);

    out->btn = sm->btn;
    out->volume_active = (sm->state == S1_SIP_VOLUME);
}

/*============================================================================
 * scroll —— 双口滚动
 *   状态: IDLE / ACTIVE。IDLE run 检测同向进入(带方向); ACTIVE run
 *   检测双口回落退出, 否则每 2 拍输出一格。
 *   转移当拍新状态 run 不执行 → 进入/退出当拍都不出格（与 v1 一致）。
 *============================================================================*/

enum { GSCR_IDLE = 0, GSCR_ACTIVE, GSCR_NUM };

static void gscr_idle_entry(void *obj);
static enum smf_state_result gscr_idle_run(void *obj);
static void gscr_active_entry(void *obj);
static enum smf_state_result gscr_active_run(void *obj);

static const struct smf_state gscr_states[GSCR_NUM] = {
    [GSCR_IDLE]   = SMF_CREATE_STATE(gscr_idle_entry,   gscr_idle_run,   NULL, NULL, NULL),
    [GSCR_ACTIVE] = SMF_CREATE_STATE(gscr_active_entry, gscr_active_run, NULL, NULL, NULL),
};

/* IDLE 入口: 退出滚动态时 also 走这里 → 方向/计数一并复位 */
static void gscr_idle_entry(void *obj)
{
    scroll_sm_t *sm = (scroll_sm_t *)obj;
    sm->active = 0;
    sm->dir = 0;
    sm->n = 0;
}

/* IDLE 运行: 双吹→上滚 / 双吸→下滚（进入当拍不出格）*/
static enum smf_state_result gscr_idle_run(void *obj)
{
    scroll_sm_t *sm = (scroll_sm_t *)obj;

    if (sm->p1_tick >= GS_SCROLL_ON && sm->p2_tick >= GS_SCROLL_ON) {
        sm->dir = -1;                               /* 双吹 = 上滚 */
        smf_set_state(&sm->ctx, &gscr_states[GSCR_ACTIVE]);
    } else if (sm->p1_tick <= -GS_SCROLL_ON && sm->p2_tick <= -GS_SCROLL_ON) {
        sm->dir = 1;                                /* 双吸 = 下滚 */
        smf_set_state(&sm->ctx, &gscr_states[GSCR_ACTIVE]);
    }
    return SMF_EVENT_HANDLED;
}

/* ACTIVE 入口: 限速计数置 1 —— 对齐 v1 的"进入当拍即计入一拍"语义:
 * v1 在进入当拍的滚轮限速块会立刻 ++n(=1), 而 SMF 转移当拍新状态 run
 * 不执行, 故由 entry 补上这一拍, 否则出格节奏整体晚一拍。 */
static void gscr_active_entry(void *obj)
{
    scroll_sm_t *sm = (scroll_sm_t *)obj;
    sm->active = 1;
    sm->n = 1;
}

/* ACTIVE 运行: 双口 |p|<100 → 退出; 否则每 2 拍输出一格 */
static enum smf_state_result gscr_active_run(void *obj)
{
    scroll_sm_t *sm = (scroll_sm_t *)obj;
    int32_t a1 = (sm->p1_tick < 0) ? -sm->p1_tick : sm->p1_tick;
    int32_t a2 = (sm->p2_tick < 0) ? -sm->p2_tick : sm->p2_tick;

    if (a1 < GS_SCROLL_OFF && a2 < GS_SCROLL_OFF) {
        smf_set_state(&sm->ctx, &gscr_states[GSCR_IDLE]);   /* 退出当拍不出格 */
        return SMF_EVENT_HANDLED;
    }

    if (++sm->n >= 2) {
        sm->n = 0;
        sm->wheel_out = sm->dir;    /* 每 2 tick（约100ms）一格 */
    }
    return SMF_EVENT_HANDLED;
}

/**
 * @brief 初始化 scroll 状态机
 *
 * @param sm 状态机
 */
void scroll_sm_init(scroll_sm_t *sm)
{
    sm->active = 0;
    sm->dir = 0;
    sm->n = 0;
    sm->wheel_out = 0;
    sm->p1_tick = 0;
    sm->p2_tick = 0;
    smf_set_initial(&sm->ctx, &gscr_states[GSCR_IDLE]);
}

/**
 * @brief scroll 每 tick 推进一步
 *
 * @param sm 状态机
 * @param p1 s1 口压力 Pa
 * @param p2 s2 口压力 Pa
 * @return 本 tick 滚轮增量（0 或 ±1）
 */
int8_t scroll_sm_step(scroll_sm_t *sm, int32_t p1, int32_t p2)
{
    sm->p1_tick = p1;
    sm->p2_tick = p2;
    sm->wheel_out = 0;      /* 默认无输出, run 覆写 */

    (void)smf_run_state(&sm->ctx);

    return sm->wheel_out;
}

/*============================================================================
 * s2 —— 第二口手势
 *   状态: IDLE / HOLD_BLOW / HOLD_SIP。松口时若按住时长 <500ms 发对应
 *   一次性命令（Alt+Tab / 回主页）, 否则静默返回。
 *============================================================================*/

enum { GS2_IDLE = 0, GS2_HOLD_BLOW, GS2_HOLD_SIP, GS2_NUM };

static void gs2_idle_entry(void *obj);
static enum smf_state_result gs2_idle_run(void *obj);
static void gs2_hold_blow_entry(void *obj);
static enum smf_state_result gs2_hold_blow_run(void *obj);
static void gs2_hold_sip_entry(void *obj);
static enum smf_state_result gs2_hold_sip_run(void *obj);

static const struct smf_state gs2_states[GS2_NUM] = {
    [GS2_IDLE]      = SMF_CREATE_STATE(gs2_idle_entry,      gs2_idle_run,      NULL, NULL, NULL),
    [GS2_HOLD_BLOW] = SMF_CREATE_STATE(gs2_hold_blow_entry, gs2_hold_blow_run, NULL, NULL, NULL),
    [GS2_HOLD_SIP]  = SMF_CREATE_STATE(gs2_hold_sip_entry,  gs2_hold_sip_run,  NULL, NULL, NULL),
};

/* IDLE 入口: 镜像归零 */
static void gs2_idle_entry(void *obj)
{
    s2_sm_t *sm = (s2_sm_t *)obj;
    sm->mode = 0;
}

/* IDLE 运行: 双向去抖检测（窗口内计数清零, 达标进入对应按住态）*/
static enum smf_state_result gs2_idle_run(void *obj)
{
    s2_sm_t *sm = (s2_sm_t *)obj;

    if (sm->p2_tick >= GS_PRESS_ON) {
        if (++sm->blow_n >= GS_DEBOUNCE) {
            sm->blow_n = GS_DEBOUNCE;
            smf_set_state(&sm->ctx, &gs2_states[GS2_HOLD_BLOW]);
        }
    } else if (sm->p2_tick <= -GS_PRESS_ON) {
        if (++sm->sip_n >= GS_DEBOUNCE) {
            sm->sip_n = GS_DEBOUNCE;
            smf_set_state(&sm->ctx, &gs2_states[GS2_HOLD_SIP]);
        }
    } else {
        sm->blow_n = 0;
        sm->sip_n = 0;
    }
    return SMF_EVENT_HANDLED;
}

/* 吹按住入口: 计时清零 + 镜像 */
static void gs2_hold_blow_entry(void *obj)
{
    s2_sm_t *sm = (s2_sm_t *)obj;
    sm->mode = 1;
    sm->ms = 0;
}

/* 吹按住运行: 松口→短吹发 Alt+Tab（长按不发）, 否则计时 */
static enum smf_state_result gs2_hold_blow_run(void *obj)
{
    s2_sm_t *sm = (s2_sm_t *)obj;

    if (sm->p2_tick <= GS_PRESS_OFF) {
        if (sm->ms < GS_LONG_MS) {
            sm->cmd_out = S2_ALTTAB;    /* 短吹松口 → 多任务切换 */
        }
        smf_set_state(&sm->ctx, &gs2_states[GS2_IDLE]);
    } else {
        sm->ms += GS_TICK_MS;
    }
    return SMF_EVENT_HANDLED;
}

/* 吸按住入口: 计时清零 + 镜像 */
static void gs2_hold_sip_entry(void *obj)
{
    s2_sm_t *sm = (s2_sm_t *)obj;
    sm->mode = 2;
    sm->ms = 0;
}

/* 吸按住运行: 松口→短吸发回主页（长按不发）, 否则计时 */
static enum smf_state_result gs2_hold_sip_run(void *obj)
{
    s2_sm_t *sm = (s2_sm_t *)obj;

    if (sm->p2_tick >= -GS_PRESS_OFF) {
        if (sm->ms < GS_LONG_MS) {
            sm->cmd_out = S2_HOME;      /* 短吸松口 → 回主页 */
        }
        smf_set_state(&sm->ctx, &gs2_states[GS2_IDLE]);
    } else {
        sm->ms += GS_TICK_MS;
    }
    return SMF_EVENT_HANDLED;
}

/**
 * @brief 初始化 s2 状态机
 *
 * @param sm 状态机
 */
void s2_sm_init(s2_sm_t *sm)
{
    sm->mode = 0;
    sm->ms = 0;
    sm->blow_n = 0;
    sm->sip_n = 0;
    sm->cmd_out = S2_NONE;
    sm->p2_tick = 0;
    smf_set_initial(&sm->ctx, &gs2_states[GS2_IDLE]);
}

/**
 * @brief s2 每 tick 推进一步
 *
 * @param sm 状态机
 * @param p2 s2 口压力 Pa
 * @return 一次性命令（NONE/ALTTAB/HOME）, 每命令仅返回一拍
 */
s2_cmd_t s2_sm_step(s2_sm_t *sm, int32_t p2)
{
    sm->p2_tick = p2;
    sm->cmd_out = S2_NONE;  /* 默认无命令, run 覆写 */

    (void)smf_run_state(&sm->ctx);

    return sm->cmd_out;
}

/*============================================================================
 * kb —— 键盘报告两相发送
 *   状态: IDLE / PRESSED。press() 只登记请求（可在 run 外调用）;
 *   IDLE run 消费请求→发按下帧→转 PRESSED; PRESSED run 发释放帧→IDLE。
 *   节奏: 按下帧与请求同拍, 释放帧晚一拍（与 v1 两相一致）。
 *============================================================================*/

enum { GKB_IDLE = 0, GKB_PRESSED, GKB_NUM };

static void gkb_idle_entry(void *obj);
static enum smf_state_result gkb_idle_run(void *obj);
static enum smf_state_result gkb_pressed_run(void *obj);

static const struct smf_state gkb_states[GKB_NUM] = {
    [GKB_IDLE]    = SMF_CREATE_STATE(gkb_idle_entry,    gkb_idle_run,    NULL, NULL, NULL),
    [GKB_PRESSED] = SMF_CREATE_STATE(NULL,              gkb_pressed_run, NULL, NULL, NULL),
};

/* IDLE 入口: 相位镜像归零 */
static void gkb_idle_entry(void *obj)
{
    kb_sm_t *sm = (kb_sm_t *)obj;
    sm->pend = 0;
}

/* IDLE 运行: 消费按下请求 → 填输出槽 + 置两相镜像 → 转 PRESSED */
static enum smf_state_result gkb_idle_run(void *obj)
{
    kb_sm_t *sm = (kb_sm_t *)obj;

    if (sm->press_req) {
        sm->press_req = 0;
        sm->out_evt = 1;            /* 1=按下帧 */
        sm->out_mod = sm->mod;
        sm->out_key = sm->key;
        sm->pend = 2;               /* 与 v1 相同: 按下发出后进入"待释放"数值 */
        smf_set_state(&sm->ctx, &gkb_states[GKB_PRESSED]);
    }
    return SMF_EVENT_HANDLED;
}

/* PRESSED 运行: 发释放帧 → 回 IDLE */
static enum smf_state_result gkb_pressed_run(void *obj)
{
    kb_sm_t *sm = (kb_sm_t *)obj;

    sm->out_evt = 2;                /* 2=释放帧（调用方发全 0 报告）*/
    sm->pend = 0;
    smf_set_state(&sm->ctx, &gkb_states[GKB_IDLE]);
    return SMF_EVENT_HANDLED;
}

/**
 * @brief 初始化 kb 状态机
 *
 * @param sm 状态机
 */
void kb_sm_init(kb_sm_t *sm)
{
    sm->pend = 0;
    sm->mod = 0;
    sm->key = 0;
    sm->press_req = 0;
    sm->out_evt = 0;
    sm->out_mod = 0;
    sm->out_key = 0;
    smf_set_initial(&sm->ctx, &gkb_states[GKB_IDLE]);
}

/**
 * @brief 登记一次按键请求（按下帧在随后最近一次 step 中发出）
 *
 * 重复调用覆盖前次请求（与 v1 的 pend 覆写语义一致）。
 * 可在 run 之外任意时机调用。
 *
 * @param sm  状态机
 * @param mod 修饰键字节
 * @param key 键值字节
 */
void kb_sm_press(kb_sm_t *sm, uint8_t mod, uint8_t key)
{
    sm->mod = mod;
    sm->key = key;
    sm->press_req = 1;
}

/**
 * @brief kb 每 tick 推进一步
 *
 * @param sm  状态机
 * @param out 输出: evt=1 发按下帧 / evt=2 发释放帧 / evt=0 无动作
 */
void kb_sm_step(kb_sm_t *sm, kb_out_t *out)
{
    sm->out_evt = 0;
    sm->out_mod = 0;
    sm->out_key = 0;

    (void)smf_run_state(&sm->ctx);

    out->evt = sm->out_evt;
    out->mod = sm->out_mod;
    out->key = sm->out_key;
}

/*============================================================================
 * home —— 回主页序列（CLICKNAV）
 *   状态: IDLE / FLOOD(×12) / X(×5) / Y(×1) / DOWN(×1) / HOLD(×1)
 *   各移动相 run 写输出槽; DOWN/HOLD 构成一次左键点击。
 *   start/abort 在 run 之外经 smf_set_state 调用（SMF 支持, 内部标志
 *   会在下一次 run_state 开头复位）。
 *============================================================================*/

enum { GHOME_IDLE = 0, GHOME_FLOOD, GHOME_X, GHOME_Y, GHOME_DOWN, GHOME_HOLD, GHOME_NUM };

static void ghome_idle_entry(void *obj);
static void ghome_flood_entry(void *obj);
static enum smf_state_result ghome_flood_run(void *obj);
static void ghome_x_entry(void *obj);
static enum smf_state_result ghome_x_run(void *obj);
static void ghome_y_entry(void *obj);
static enum smf_state_result ghome_y_run(void *obj);
static void ghome_down_entry(void *obj);
static enum smf_state_result ghome_down_run(void *obj);
static void ghome_hold_entry(void *obj);
static enum smf_state_result ghome_hold_run(void *obj);

static const struct smf_state ghome_states[GHOME_NUM] = {
    [GHOME_IDLE]  = SMF_CREATE_STATE(ghome_idle_entry,  NULL,            NULL, NULL, NULL),
    [GHOME_FLOOD] = SMF_CREATE_STATE(ghome_flood_entry, ghome_flood_run, NULL, NULL, NULL),
    [GHOME_X]     = SMF_CREATE_STATE(ghome_x_entry,     ghome_x_run,     NULL, NULL, NULL),
    [GHOME_Y]     = SMF_CREATE_STATE(ghome_y_entry,     ghome_y_run,     NULL, NULL, NULL),
    [GHOME_DOWN]  = SMF_CREATE_STATE(ghome_down_entry,  ghome_down_run,  NULL, NULL, NULL),
    [GHOME_HOLD]  = SMF_CREATE_STATE(ghome_hold_entry,  ghome_hold_run,  NULL, NULL, NULL),
};

/* IDLE 入口: 相位镜像 + 计数复位 */
static void ghome_idle_entry(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;
    sm->phase = HOME_IDLE;
    sm->n = 0;
}

/* FLOOD 入口: 相位镜像 + 相内计数清零（重入即从头重放）*/
static void ghome_flood_entry(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;
    sm->phase = HOME_FLOOD;
    sm->n = 0;
}

/* FLOOD 运行: 满幅冲左下角（边界钳位保证必达）, 计满转 X */
static enum smf_state_result ghome_flood_run(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;

    sm->move = 1;
    sm->dx = -128;
    sm->dy = 127;
    if (++sm->n >= HOME_FLOOD_TICKS) {
        smf_set_state(&sm->ctx, &ghome_states[GHOME_X]);
    }
    return SMF_EVENT_HANDLED;
}

/* X 入口: 相位镜像 + 相内计数清零 */
static void ghome_x_entry(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;
    sm->phase = HOME_X;
    sm->n = 0;
}

/* X 运行: 右移半屏（分 5 帧防指针加速度放大）, 计满转 Y */
static enum smf_state_result ghome_x_run(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;

    sm->move = 1;
    sm->dx = (int8_t)(HOME_X_CENTER_COUNTS / 5);
    sm->dy = 0;
    if (++sm->n >= HOME_X_TICKS) {
        smf_set_state(&sm->ctx, &ghome_states[GHOME_Y]);
    }
    return SMF_EVENT_HANDLED;
}

/* Y 入口: 相位镜像（单拍相, 无计数）*/
static void ghome_y_entry(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;
    sm->phase = HOME_Y;
}

/* Y 运行: 上移进主页键中心, 单拍后转 DOWN */
static enum smf_state_result ghome_y_run(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;

    sm->move = 1;
    sm->dx = 0;
    sm->dy = (int8_t)(-HOME_Y_NAV_OFFSET);
    smf_set_state(&sm->ctx, &ghome_states[GHOME_DOWN]);
    return SMF_EVENT_HANDLED;
}

/* DOWN 入口: 相位镜像 */
static void ghome_down_entry(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;
    sm->phase = HOME_DOWN;
}

/* DOWN 运行: 左键按下（镜像由调用方套用到报告）, 转 HOLD */
static enum smf_state_result ghome_down_run(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;

    sm->move = 0;
    sm->left_btn = 1;
    smf_set_state(&sm->ctx, &ghome_states[GHOME_HOLD]);
    return SMF_EVENT_HANDLED;
}

/* HOLD 入口: 相位镜像 */
static void ghome_hold_entry(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;
    sm->phase = HOME_HOLD;
}

/* HOLD 运行: 左键释放 = 完成点击, 回 IDLE */
static enum smf_state_result ghome_hold_run(void *obj)
{
    home_sm_t *sm = (home_sm_t *)obj;

    sm->left_btn = 0;
    smf_set_state(&sm->ctx, &ghome_states[GHOME_IDLE]);
    return SMF_EVENT_HANDLED;
}

/**
 * @brief 初始化 home 状态机（进入 IDLE）
 *
 * @param sm 状态机
 */
void home_sm_init(home_sm_t *sm)
{
    sm->phase = HOME_IDLE;
    sm->n = 0;
    sm->move = 0;
    sm->dx = 0;
    sm->dy = 0;
    sm->left_btn = -1;
    smf_set_initial(&sm->ctx, &ghome_states[GHOME_IDLE]);
}

/**
 * @brief 启动回主页序列（任何状态下调用都从头重放, 相内计数由 entry 清零）
 *
 * @param sm 状态机
 */
void home_sm_start(home_sm_t *sm)
{
    smf_set_state(&sm->ctx, &ghome_states[GHOME_FLOOD]);
}

/**
 * @brief 中止未完成序列（审计 F3 断连恢复; 已在 IDLE 时为无害空操作）
 *
 * @param sm 状态机
 */
void home_sm_abort(home_sm_t *sm)
{
    if (sm->phase != HOME_IDLE) {
        smf_set_state(&sm->ctx, &ghome_states[GHOME_IDLE]);
    }
}

/**
 * @brief home 每 tick 推进一步（仅序列进行中有意义）
 *
 * @param sm  状态机
 * @param out 输出: move/dx/dy=本拍位移, left_btn=-1 不接管 / 0 清 / 1 置
 */
void home_sm_step(home_sm_t *sm, home_out_t *out)
{
    /* 默认无输出: 移动相 run 覆写 move/dx/dy, 点击相 run 覆写 left_btn */
    sm->move = 0;
    sm->dx = 0;
    sm->dy = 0;
    sm->left_btn = -1;

    (void)smf_run_state(&sm->ctx);

    out->move = sm->move;
    out->dx = sm->dx;
    out->dy = sm->dy;
    out->left_btn = sm->left_btn;
}

/**
 * @brief 查询某相的发送节奏（调用方在 step 之前按当前相延时）
 *
 * 节奏依据 Android 指针加速度的实测约束:
 *   冲角段快发（钳位兜底, 怎么飞都出不了屏）; 定位/按下段 25ms/帧
 *   （"计数≈距离"的标定节奏, 快发会被加速度放大导致飞过头）;
 *   点击保持 30ms（太短怕 EMUI 不识别成点击）。
 *
 * @param phase 目标相
 * @return 建议节奏 ms（0=尽快, 由调用方 busy-wait 端点空闲）
 */
uint16_t home_sm_pacing_ms(home_phase_t phase)
{
    switch (phase) {
    case HOME_FLOOD:  return 0;
    case HOME_X:
    case HOME_Y:
    case HOME_DOWN:   return 25;
    case HOME_HOLD:   return 30;
    default:          return 50;
    }
}
