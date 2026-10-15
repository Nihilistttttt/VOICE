#ifndef __GESTURE_SM_H__
#define __GESTURE_SM_H__

#include "smf.h"    /* Zephyr SMF 移植版: 各状态机内嵌 struct smf_ctx */
#include "params.h" /* 参数表: 阈值/灵敏度等可调量的单一真源(g_params) */

/*============================================================================
 * 手势状态机（基于 Zephyr SMF 的实现, 纯逻辑, 无硬件依赖, 可宿主编译单测）
 *
 * 来源与演进:
 *   v1: 从 Mode_HidMouse 内联实现等价抽取的手写状态机（commit 7292d58）
 *   v2: 本版 —— 外部 API/公共字段/行为完全不变, 内部改由 Zephyr SMF
 *       驱动（smf_set_initial 初始化, smf_run_state 每 tick 推进,
 *       smf_set_state 转移）。hosttest 125 断言不改一字全过 = 等价证明。
 *
 * 结构约定:
 *   - 每个状态机结构体以 struct smf_ctx 开头（SMF 约定, 处理函数可直接
 *     把 obj 指针转回机器结构体）
 *   - 状态枚举/计数字段是对外的"镜像字段"（由 entry/run 处理函数维护）,
 *     供调用方查询与宿主测试断言; 真正的状态由 SMF 的 current 持有
 *   - 输出一律经"输出槽"字段返回: run 处理函数写入, step() 读出并清零
 *
 * tick 语义: 一次 step ≈ 主循环一圈（常规节奏 ~50ms）。计时字段沿用
 * "每调用 +50ms" 口径（与 v1 一致）; 时序与循环速率的解耦是 SMF 化
 * 之后的后续课题（时间事件/定时器由调用方演进）。
 *
 * 宿主单测: hosttest/run.sh
 *============================================================================*/

/* ---- 结构性常量（不随参数调整; 阈值/灵敏度类可调量已迁入 params 表）------*/
#define GS_TICK_MS      50      /* 每 tick 名义时长 ms */
#define HOME_X_TICKS    5       /* X 定位分 5 帧发（分批因子, 结构性）*/

/* ---- 鼠标报告按键位（HID 按键字节的标准位定义, 消除裸 magic number）------*/
#define GS_BTN_LEFT     0x01    /* bit0: 左键 */
#define GS_BTN_RIGHT    0x02    /* bit1: 右键（本项目不使用: EMUI 右键无返回语义）*/
#define GS_BTN_MIDDLE   0x04    /* bit2: 中键（EMUI 返回键）*/

/*============================================================================
 * s1 —— 单口手势: 吹=左键点击/吹住=拖拽, 短吸=中键点击, 吸住=音量模式
 *
 * SMF 建模: 扁平三状态（IDLE/TIMING/VOLUME）。
 * ★ 设计注记: 吹气迟滞与中键两相"跨状态每拍都要跑", 若放进父状态 run,
 *   SMF 语义是"子状态转移当拍跳过祖先 run"——中键按下会晚一拍生效,
 *   破坏"释放当拍即出 0x04"的既有行为。故采用每状态 run 内调用共享
 *   静态助手（blow_update/rclick_update, 调用顺序=原始内联顺序）,
 *   SMF 只负责状态编排。层次建模留给未来确有共享需求的场景。
 *============================================================================*/
typedef enum {
    S1_IDLE = 0,        /* 无手势 */
    S1_SIP_TIMING,      /* 吸住计时中 */
    S1_SIP_VOLUME,      /* 音量模式（调用方冻结指针并算 Consumer 位）*/
} s1_state_t;

typedef struct {
    struct smf_ctx ctx;     /* SMF 上下文（必须为首成员, 处理函数直接转型）*/
    s1_state_t state;       /* 镜像: 当前吸吮子状态（entry 维护）*/
    uint8_t  btn;           /* 对外按键位: 0x01=左键 0x04=中键 */
    uint8_t  blow_n;        /* 吹去抖计数 */
    uint8_t  sip_n;         /* 吸去抖计数 */
    uint16_t sip_ms;        /* 吸住时长（ms, 按 GS_TICK_MS 步进）*/
    uint8_t  rclick_pend;   /* 中键两相: 0=无 2=按下待发 1=待释放 */
    int32_t  p1_tick;       /* 输入槽: 本 tick s1 口压力（step 写入, run 读取）*/
    int32_t  p2_tick;       /* 输入槽: 本 tick s2 口压力 */
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
 *
 * SMF 建模: IDLE/ACTIVE 两状态。进入/退出当拍都不出格
 * （转移当拍新状态 run 不执行 —— 与 v1 的 ++n 次序行为一致）。
 *============================================================================*/
typedef struct {
    struct smf_ctx ctx;
    uint8_t active;     /* 镜像: 1=滚动中 */
    int8_t  dir;        /* 镜像: +1=向下  -1=向上 */
    uint8_t n;          /* 限速计数 */
    int8_t  wheel_out;  /* 输出槽: 本 tick 滚轮增量 */
    int32_t p1_tick;    /* 输入槽: 本 tick s1 口压力（step 写入, run 读取）*/
    int32_t p2_tick;    /* 输入槽: 本 tick s2 口压力 */
} scroll_sm_t;

void scroll_sm_init(scroll_sm_t *sm);
int8_t scroll_sm_step(scroll_sm_t *sm, int32_t p1, int32_t p2);  /* 返回滚轮增量 0/±1 */

/*============================================================================
 * s2 —— 第二口手势: 短吹=Alt+Tab(多任务), 短吸=回主页; 按住≥500ms 松口无动作
 *
 * SMF 建模: IDLE/HOLD_BLOW/HOLD_SIP 三状态, 命令经输出槽一次性返回。
 *============================================================================*/
typedef enum {
    S2_NONE = 0,
    S2_ALTTAB,          /* 短吹松口 */
    S2_HOME,            /* 短吸松口 */
} s2_cmd_t;

typedef struct {
    struct smf_ctx ctx;
    uint8_t  mode;      /* 镜像: 0=无 1=吹按住 2=吸按住（entry 维护）*/
    uint16_t ms;        /* 按住时长 */
    uint8_t  blow_n;    /* 吹去抖计数 */
    uint8_t  sip_n;     /* 吸去抖计数 */
    s2_cmd_t cmd_out;   /* 输出槽: 一次性命令 */
    int32_t  p2_tick;   /* 输入槽: 本 tick s2 口压力（step 写入, run 读取）*/
} s2_sm_t;

void s2_sm_init(s2_sm_t *sm);
s2_cmd_t s2_sm_step(s2_sm_t *sm, int32_t p2);   /* 返回一次性命令 */

/*============================================================================
 * kb —— 键盘报告两相发送: 按下 → (下一 tick)释放
 *
 * SMF 建模: IDLE/PRESSED 两状态。press() 只登记请求; IDLE run 消费请求
 * 并发按下帧, PRESSED run 发释放帧。跨断连的自愈语义与 v1 相同。
 *============================================================================*/
typedef struct {
    struct smf_ctx ctx;
    uint8_t pend;       /* 镜像: 0=无 2=已发按下待释放（与 v1 数值兼容）*/
    uint8_t mod;        /* 待发修饰键 */
    uint8_t key;        /* 待发键值 */
    uint8_t press_req;  /* 请求槽: 1=有未消费的按下请求 */
    uint8_t out_evt;    /* 输出槽: 0=无 1=按下帧 2=释放帧 */
    uint8_t out_mod;    /* 输出槽: 按下帧修饰键 */
    uint8_t out_key;    /* 输出槽: 按下帧键值 */
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
 *
 * SMF 建模: IDLE/FLOOD/X/Y/DOWN/HOLD 六状态, 各移动相的 run 写输出槽;
 * 调用方按 home_sm_pacing_ms(phase 镜像) 决定发帧节奏
 * （Android 指针加速度约束: 冲角快发/定位 25ms/保持 30ms）。
 * start/abort 经 smf_set_state 实现（可在 run 之外调用, SMF 支持）。
 *============================================================================*/
typedef enum {
    HOME_IDLE = 0,
    HOME_FLOOD,         /* 冲左下角: X=-128 Y=+127 */
    HOME_X,             /* X 右移屏幕中心 */
    HOME_Y,             /* Y 微调进主页键 */
    HOME_DOWN,          /* 左键按下 */
    HOME_HOLD,          /* 点击保持相（下一 tick 释放）*/
} home_phase_t;

/* HOME_FLOOD_TICKS/HOME_X_CENTER_COUNTS/HOME_Y_NAV_OFFSET 已迁入参数表
 * (g_params.home_flood_ticks / home_x_center / home_y_offset);
 * HOME_X_TICKS 为结构性分批因子, 留在本文件头部。 */

typedef struct {
    struct smf_ctx ctx;
    home_phase_t phase;     /* 镜像: 当前相（entry 维护, 调用方查节奏用）*/
    uint8_t n;              /* 相内 tick 计数 */
    uint8_t move;           /* 输出槽: 1=本 tick dx/dy 有效 */
    int8_t  dx, dy;         /* 输出槽: 本 tick 位移 */
    int8_t  left_btn;       /* 输出槽: -1=不接管 0=清左键 1=置左键 */
} home_sm_t;

typedef struct {
    uint8_t move;           /* 1=本 tick dx/dy 有效（移动相）*/
    int8_t  dx, dy;
    int8_t  left_btn;       /* -1=不动 0=清左键 1=置左键 */
} home_out_t;

void home_sm_init(home_sm_t *sm);
void home_sm_start(home_sm_t *sm);
void home_sm_abort(home_sm_t *sm);      /* 断连恢复: 未完成序列作废 */
void home_sm_step(home_sm_t *sm, home_out_t *out);
uint16_t home_sm_pacing_ms(home_phase_t phase); /* 0=尽快(busy-wait) 25 30 */

#endif /* __GESTURE_SM_H__ */
