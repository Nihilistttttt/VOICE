#ifndef __GESTURE_SM_H__
#define __GESTURE_SM_H__

#include <stdint.h>

/*============================================================================
 * 手势状态机（纯逻辑, 无硬件依赖, 可宿主编译单测）
 *
 * 从 User/test_modes.c 的 Mode_HidMouse 内联实现等价抽取（行为不变）。
 * 设计依据 state-machine-design Pattern A: 显式状态 + 每 tick 一步,
 * 输入经参数传入、输出经 out 结构返回, 时序策略（延时/发帧节奏）留在调用方。
 *
 * tick 语义: 一次 step ≈ 主循环一圈（常规节奏 ~50ms）。计时字段沿用原实现
 * "每调用 +50ms" 的口径 —— 序列快发期间计时同样加速, 与抽取前行为一致;
 * 时序与循环速率的彻底解耦留给 Zephyr SMF 路线。
 *
 * 宿主单测: hosttest/run.sh
 *============================================================================*/

/* ---- 阈值/常量（与原内联实现数值一致）-----------------------------------*/
#define GS_PRESS_ON     250     /* 按下阈值 Pa（迟滞上沿）*/
#define GS_PRESS_OFF    150     /* 释放阈值 Pa（迟滞下沿）*/
#define GS_IDLE_WIN     100     /* 另一口空闲窗口: |p2| < 100 */
#define GS_SCROLL_ON    150     /* 双口滚动进入阈值 */
#define GS_SCROLL_OFF   100     /* 双口滚动退出阈值（绝对值）*/
#define GS_LONG_MS      500     /* 长按判定 ms */
#define GS_TICK_MS      50      /* 每 tick 名义时长 ms */
#define GS_VOL_TILT     600     /* 音量倾斜阈值（摇杆原始计数偏移）*/
#define GS_DEBOUNCE     2       /* 按下去抖 tick 数 */

/*============================================================================
 * s1 —— 单口手势: 吹=左键点击/吹住=拖拽, 短吸=中键点击, 吸住=音量模式
 *============================================================================*/
typedef enum {
    S1_IDLE = 0,        /* 无手势 */
    S1_SIP_TIMING,      /* 吸住计时中 */
    S1_SIP_VOLUME,      /* 音量模式（指针冻结, 倾斜=音量）*/
} s1_state_t;

typedef struct {
    s1_state_t state;
    uint8_t  btn;           /* 对外按键位: 0x01=左键 0x04=中键 */
    uint8_t  blow_n;        /* 吹去抖计数 */
    uint8_t  sip_n;         /* 吸去抖计数 */
    uint16_t sip_ms;        /* 吸住时长（ms, 按 GS_TICK_MS 步进）*/
    uint8_t  rclick_pend;   /* 中键两相: 0=无 2=按下待发 1=待释放 */
} s1_sm_t;

typedef struct {
    uint8_t btn;            /* 当前按键位组合 */
    uint8_t volume_active;  /* 1=音量模式（调用方冻结指针并算 cons 位）*/
} s1_out_t;

void s1_sm_init(s1_sm_t *sm);
void s1_sm_step(s1_sm_t *sm, int32_t p1, int32_t p2, s1_out_t *out);

/*============================================================================
 * scroll —— 双口滚动: 双吹=上滚 双吸=下滚, 进入 |p|≥150 / 退出 |p|<100,
 * 每 2 tick（约 100ms）输出一格
 *============================================================================*/
typedef struct {
    uint8_t active;
    int8_t  dir;        /* +1=向下  -1=向上 */
    uint8_t n;          /* 限速计数 */
} scroll_sm_t;

void scroll_sm_init(scroll_sm_t *sm);
int8_t scroll_sm_step(scroll_sm_t *sm, int32_t p1, int32_t p2);  /* 返回滚轮增量 0/±1 */

/*============================================================================
 * s2 —— 第二口手势: 短吹=Alt+Tab(多任务), 短吸=回主页; 按住≥500ms 无动作
 *============================================================================*/
typedef enum {
    S2_NONE = 0,
    S2_ALTTAB,          /* 短吹松口 */
    S2_HOME,            /* 短吸松口 */
} s2_cmd_t;

typedef struct {
    uint8_t  mode;      /* 0=无 1=吹按住 2=吸按住 */
    uint16_t ms;
    uint8_t  blow_n;
    uint8_t  sip_n;
} s2_sm_t;

void s2_sm_init(s2_sm_t *sm);
s2_cmd_t s2_sm_step(s2_sm_t *sm, int32_t p2);   /* 返回一次性命令 */

/*============================================================================
 * kb —— 键盘报告两相发送: 按下 → (下一 tick)释放
 *============================================================================*/
typedef struct {
    uint8_t pend;       /* 0=无 1=按下待发 2=释放待发 */
    uint8_t mod;        /* 修饰键 */
    uint8_t key;        /* 键值 */
} kb_sm_t;

typedef struct {
    uint8_t evt;        /* 0=无 1=发按下帧(mod,key) 2=发释放帧(全0) */
    uint8_t mod;
    uint8_t key;
} kb_out_t;

void kb_sm_init(kb_sm_t *sm);
void kb_sm_press(kb_sm_t *sm, uint8_t mod, uint8_t key);
void kb_sm_step(kb_sm_t *sm, kb_out_t *out);

/*============================================================================
 * home —— 回主页序列（HOME_MODE_CLICKNAV, 旧键盘三连保留在调用方宏分支）
 *   冲左下角(边界钳位, 满幅×12) → X 右移半屏 → Y 微调 → 左键按下 → 释放
 * 调用方按 home_sm_pacing_ms(当前相) 决定发帧节奏（Android 指针加速度约束）。
 *============================================================================*/
typedef enum {
    HOME_IDLE = 0,
    HOME_FLOOD,         /* 冲左下角: X=-128 Y=+127 */
    HOME_X,             /* X 右移屏幕中心 */
    HOME_Y,             /* Y 微调进主页键 */
    HOME_DOWN,          /* 左键按下 */
    HOME_HOLD,          /* 点击保持相（下一 tick 释放）*/
} home_phase_t;

#define HOME_FLOOD_TICKS        12      /* 满幅冲角 tick 数（最坏起点兜底）*/
#define HOME_X_TICKS            5       /* X 右移分 5 帧发 */
#define HOME_X_CENTER_COUNTS    540     /* 屏宽一半(P20=1080px)，实测调整 */
#define HOME_Y_NAV_OFFSET       24      /* 主页键中心距屏幕底边 */

typedef struct {
    home_phase_t phase;
    uint8_t n;
} home_sm_t;

typedef struct {
    uint8_t move;           /* 1=本 tick dx/dy 有效（移动相）*/
    int8_t  dx, dy;
    int8_t  left_btn;       /* -1=不接管 0=清左键 1=置左键 */
} home_out_t;

void home_sm_init(home_sm_t *sm);
void home_sm_start(home_sm_t *sm);
void home_sm_abort(home_sm_t *sm);      /* 断连恢复: 未完成序列作废 */
void home_sm_step(home_sm_t *sm, home_out_t *out);
uint16_t home_sm_pacing_ms(home_phase_t phase); /* 0=尽快(busy-wait) 25 30 */

#endif /* __GESTURE_SM_H__ */
