#ifndef __PARAMS_H__
#define __PARAMS_H__

#include <stdint.h>

/*============================================================================
 * 参数表 —— 全系统可调参数的单一真源
 *
 * 设计:
 *   - g_params 是运行时唯一真源: gesture_sm 状态机/摇杆缩放/回主页节奏
 *     全部改读此结构体, 不再使用编译期宏
 *   - 参数描述符表(本文件 .c 内)定义每项的 默认值/最小值/最大值/协议 id,
 *     三处共用: SD 持久化(键名=参数名)、0xFB 协议下行(id 直通)、
 *     恢复出厂(遍历写默认)
 *   - g_params 以静态初始化器直接带默认值 —— 宿主单测不调用任何初始化
 *     函数即可拿到与固件一致的默认行为
 *   - "能放就放"原则: 调不出来的参数保持默认值不影响功能, 故每项都
 *     有安全钳位区间, 非法值一律钳回区间内
 *
 * 持久化: SD 卡 /voice_config.ini (键名=参数名), 见 params_sd.c
 * 协议:   0xFB + id + vH + vL → Params_SetById (AOA 实时调参预留)
 *============================================================================*/

/* ---- 参数运行时结构（字段顺序与 .c 描述符表一致, 新增参数两处同步）----*/
typedef struct {
    /* 摇杆 */
    int32_t joy_deadzone;       /* 死区计数           默认 500 */
    int32_t joy_step_div;       /* 步长分母(灵敏度)   默认 128 */
    int32_t joy_clamp;          /* 单帧位移钳位       默认 64  */
    /* 压差手势 */
    int32_t press_on;           /* 按下阈值 Pa        默认 250 */
    int32_t press_off;          /* 释放阈值 Pa        默认 150 */
    int32_t idle_win;           /* 另一口空闲窗口     默认 100 */
    int32_t scroll_on;          /* 滚动进入阈值       默认 150 */
    int32_t scroll_off;         /* 滚动退出阈值       默认 100 */
    int32_t vol_tilt;           /* 音量对角倾斜合计阈值 默认 600 */
    int32_t vol_rate_ms;        /* 音量调节间隔ms     默认 50 */
    int32_t debounce;           /* 按下去抖 tick      默认 2   */
    /* 回主页(CLICKNAV) */
    int32_t home_x_center;      /* X 定位计数         默认 540 */
    int32_t home_y_offset;      /* Y 偏移计数         默认 24  */
    int32_t home_flood_ticks;   /* 冲角 tick 数       默认 12  */
    int32_t click_hold_ms;      /* 点击保持 ms        默认 30  */
    int32_t locate_ms;          /* 定位段节奏 ms      默认 25  */
    /* 双口仲裁(s2) */
    int32_t pair_debounce;      /* 双口同向确认拍数   默认 2   */
    int32_t blow_debounce;      /* 吹确认拍数(s1左键/s2音量) 默认 3 */
    int32_t s2_short_ms;        /* s2短吸多任务窗口ms 默认 800 */
    /* 命令仲裁 */
    int32_t cmd_gap_ms;         /* 异种命令最小间隔ms 默认 500 */
    int32_t s1_short_ms;        /* s1短/长吸分界ms    默认 800;
                                    短于此且≥最小窗口松口=中键返回,
                                    吸够此时长=回主页 */
    int32_t s1_back_min_ms;     /* s1返回最小吸时ms   默认 50;
                                    吸住至少此时长松口才算返回(更短=毛刺吞掉) */
} voice_params_t;

/* 运行时唯一真源(静态初始化为默认值) */
extern voice_params_t g_params;

/**
 * @brief 按 id 设置参数（0xFB 协议下行直通入口）
 *
 * @param id  参数 id（见 PARAM_ID_* 宏）
 * @param val 目标值（自动钳位到该参数的 [min,max] 区间）
 * @return 1=id 有效已写入; 0=id 不存在
 */
uint8_t Params_SetById(uint8_t id, int32_t val);

/**
 * @brief 按 id 读当前参数值（WiFi 全量推送/诊断用）
 *
 * @param id 参数 id（见 PARAM_ID_* 宏）
 * @return 当前值; id 不存在返回 0
 */
int32_t Params_GetValue(uint8_t id);

/**
 * @brief 按键名设置参数（SD 配置文件解析入口）
 *
 * @param name 参数名（与描述符表精确匹配; 未知名静默忽略——前向兼容）
 * @param val  目标值（自动钳位）
 * @return 1=写入成功; 0=名字不存在
 */
uint8_t Params_SetByName(const char *name, int32_t val);

/**
 * @brief 恢复出厂默认值（遍历描述符表写默认; 不触碰 SD, 落盘用 Params_Save）
 */
void Params_FactoryReset(void);

/**
 * @brief 查询参数个数（供上位机遍历）
 */
uint8_t Params_Count(void);

/**
 * @brief 遍历描述符表（上位机/调试打印用）
 *
 * @param idx  [0, Params_Count())
 * @param name 输出: 参数名(静态字符串)
 * @return 当前值
 */
int32_t Params_GetByIndex(uint8_t idx, const char **name);

/* ---- 参数 id（0xFB 协议下行直通; 新增参数顺延, 上位机同步）------------*/
#define PARAM_ID_JOY_DEADZONE     0
#define PARAM_ID_JOY_STEP_DIV     1
#define PARAM_ID_JOY_CLAMP        2
#define PARAM_ID_PRESS_ON         3
#define PARAM_ID_PRESS_OFF        4
#define PARAM_ID_IDLE_WIN         5
#define PARAM_ID_SCROLL_ON        6
#define PARAM_ID_SCROLL_OFF       7
/* id 8 已废弃(原 long_ms: S2 吹即音量后无用途)。槽位留空防 id 漂移,
 * 描述符 ptr=NULL 由各入口跳过; id 不复用 */
#define PARAM_ID_VOL_TILT         9
#define PARAM_ID_DEBOUNCE         10
#define PARAM_ID_HOME_X_CENTER    11
#define PARAM_ID_HOME_Y_OFFSET    12
#define PARAM_ID_HOME_FLOOD_TICKS 13
#define PARAM_ID_CLICK_HOLD_MS    14
#define PARAM_ID_LOCATE_MS        15
#define PARAM_ID_PAIR_DEBOUNCE    16
#define PARAM_ID_BLOW_DEBOUNCE    17
#define PARAM_ID_S2_SHORT_MS      18
#define PARAM_ID_CMD_GAP_MS       19
#define PARAM_ID_S1_SHORT_MS      20
#define PARAM_ID_S1_BACK_MIN_MS   21
#define PARAM_ID_VOL_RATE_MS      22
#define PARAM_ID_NUM              23

#endif /* __PARAMS_H__ */
