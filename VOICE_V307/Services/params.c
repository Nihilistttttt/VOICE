/*============================================================================
 * 参数表实现 —— 单一真源 + 描述符表 + 钳位写入
 *
 * 文件说明:
 *   g_params 静态初始化为出厂默认值（与 gesture_sm/test_modes 既有行为
 *   数值完全一致）, 保证"不调参=行为不变"。SD 持久化在 params_sd.c
 *   （独立文件: 宿主单测不编 FatFs, 固件按 USE_SDCARD 门控）。
 *
 *   新增参数的固定三步:
 *     ① params.h 结构体加字段 + PARAM_ID_ 宏
 *     ② 本文件描述符表加一行(名字/默认/min/max)
 *     ③ 使用方把宏引用改为 g_params.字段
 *============================================================================*/

#include "params.h"
#include <string.h>

/*---------------------------------------------------------------------------
 * 运行时真源: 静态初始化 = 出厂默认值
 * （顺序即 voice_params_t 字段顺序; 描述符表与其逐项对应）
 *---------------------------------------------------------------------------*/
voice_params_t g_params = {
    .joy_deadzone      = 500,
    .joy_step_div      = 128,
    .joy_clamp         = 64,
    .press_on          = 250,
    .press_off         = 150,
    .idle_win          = 100,
    .scroll_on         = 150,
    .scroll_off        = 100,
    .vol_tilt          = 600,
    .vol_rate_ms       = 50,
    .debounce          = 2,
    .home_x_center     = 540,
    .home_y_offset     = 24,
    .home_flood_ticks  = 12,
    .click_hold_ms     = 30,
    .locate_ms         = 25,
    .pair_debounce     = 2,
    .blow_debounce     = 3,
    .s2_short_ms       = 800,
    .cmd_gap_ms        = 500,
    .s1_short_ms       = 800,
    .s1_back_min_ms    = 50,
};

/*---------------------------------------------------------------------------
 * 参数描述符表: id / 键名(SD 文件+上位机显示) / 默认 / 最小 / 最大
 * min/max 是安全边界——钳位保证任何来源(SD/协议)的非法值都不致瘫痪
 * （例: press_on 若被写成 0 会"永远按下", 钳位是最后的防线）
 *---------------------------------------------------------------------------*/
typedef struct {
    uint8_t      id;
    const char  *name;
    int32_t     *ptr;
    int32_t      def;
    int32_t      min;
    int32_t      max;
} param_desc_t;

static param_desc_t s_desc[PARAM_ID_NUM] = {
    [PARAM_ID_JOY_DEADZONE]     = { PARAM_ID_JOY_DEADZONE,     "joy_deadzone",     &g_params.joy_deadzone,     500,    0,   4000 },
    [PARAM_ID_JOY_STEP_DIV]     = { PARAM_ID_JOY_STEP_DIV,     "joy_step_div",     &g_params.joy_step_div,     128,    16,  1024 },
    [PARAM_ID_JOY_CLAMP]        = { PARAM_ID_JOY_CLAMP,        "joy_clamp",        &g_params.joy_clamp,        64,     8,   127  },
    [PARAM_ID_PRESS_ON]         = { PARAM_ID_PRESS_ON,         "press_on",         &g_params.press_on,         250,    50,  480  },
    [PARAM_ID_PRESS_OFF]        = { PARAM_ID_PRESS_OFF,        "press_off",        &g_params.press_off,        150,    20,  400  },
    [PARAM_ID_IDLE_WIN]         = { PARAM_ID_IDLE_WIN,         "idle_win",         &g_params.idle_win,         100,    20,  400  },
    [PARAM_ID_SCROLL_ON]        = { PARAM_ID_SCROLL_ON,        "scroll_on",        &g_params.scroll_on,        150,    50,  480  },
    [PARAM_ID_SCROLL_OFF]       = { PARAM_ID_SCROLL_OFF,       "scroll_off",       &g_params.scroll_off,       100,    20,  400  },
    /* [8] long_ms 已废弃: 槽位留空(ptr=NULL), 各入口跳过 */
    [PARAM_ID_VOL_TILT]         = { PARAM_ID_VOL_TILT,         "vol_tilt",         &g_params.vol_tilt,         600,    100, 8000 },
    [PARAM_ID_DEBOUNCE]         = { PARAM_ID_DEBOUNCE,         "debounce",         &g_params.debounce,         2,      1,   10   },
    [PARAM_ID_HOME_X_CENTER]    = { PARAM_ID_HOME_X_CENTER,    "home_x_center",    &g_params.home_x_center,    540,    100, 4000 },
    [PARAM_ID_HOME_Y_OFFSET]    = { PARAM_ID_HOME_Y_OFFSET,    "home_y_offset",    &g_params.home_y_offset,    24,     0,   200  },
    [PARAM_ID_HOME_FLOOD_TICKS] = { PARAM_ID_HOME_FLOOD_TICKS, "home_flood_ticks", &g_params.home_flood_ticks, 12,     4,   60   },
    [PARAM_ID_CLICK_HOLD_MS]    = { PARAM_ID_CLICK_HOLD_MS,    "click_hold_ms",    &g_params.click_hold_ms,    30,     5,   200  },
    [PARAM_ID_LOCATE_MS]        = { PARAM_ID_LOCATE_MS,        "locate_ms",        &g_params.locate_ms,        25,     5,   100  },
    [PARAM_ID_PAIR_DEBOUNCE]    = { PARAM_ID_PAIR_DEBOUNCE,    "pair_debounce",    &g_params.pair_debounce,    2,      1,   10   },
    [PARAM_ID_BLOW_DEBOUNCE]    = { PARAM_ID_BLOW_DEBOUNCE,    "blow_debounce",    &g_params.blow_debounce,    3,      1,   10   },
    [PARAM_ID_S2_SHORT_MS]      = { PARAM_ID_S2_SHORT_MS,      "s2_short_ms",      &g_params.s2_short_ms,      800,    100, 2000 },
    [PARAM_ID_CMD_GAP_MS]       = { PARAM_ID_CMD_GAP_MS,       "cmd_gap_ms",       &g_params.cmd_gap_ms,       500,    100, 2000 },
    [PARAM_ID_S1_SHORT_MS]      = { PARAM_ID_S1_SHORT_MS,      "s1_short_ms",      &g_params.s1_short_ms,      800,    100, 2000 },
    [PARAM_ID_S1_BACK_MIN_MS]   = { PARAM_ID_S1_BACK_MIN_MS,   "s1_back_min_ms",   &g_params.s1_back_min_ms,   50,     0,   500  },
    [PARAM_ID_VOL_RATE_MS]      = { PARAM_ID_VOL_RATE_MS,      "vol_rate_ms",      &g_params.vol_rate_ms,      50,     20,  1000 },
};

/**
 * @brief 按 id 设置参数（值自动钳位到 [min,max] 安全区间）
 *
 * @param id  参数 id
 * @param val 目标值
 * @return 1=写入成功; 0=id 越界
 */
uint8_t Params_SetById(uint8_t id, int32_t val)
{
    if (id >= PARAM_ID_NUM || s_desc[id].ptr == 0) {
        return 0;
    }

    param_desc_t *d = &s_desc[id];

    if (val < d->min) { val = d->min; }
    if (val > d->max) { val = d->max; }
    *d->ptr = val;
    return 1;
}

int32_t Params_GetValue(uint8_t id)
{
    if (id >= PARAM_ID_NUM || s_desc[id].ptr == 0) {
        return 0;
    }
    return *s_desc[id].ptr;
}

/**
 * @brief 恢复出厂默认值（只写运行时真源; SD 落盘由调用方另行触发）
 */
void Params_FactoryReset(void)
{
    uint8_t i;

    for (i = 0; i < PARAM_ID_NUM; i++) {
        if (s_desc[i].ptr == 0) { continue; }   /* 废弃空槽 */
        *s_desc[i].ptr = s_desc[i].def;
    }
}

/**
 * @brief 按键名设置参数（SD 配置文件解析入口; 未知名静默忽略）
 *
 * @param name 参数名（精确匹配）
 * @param val  目标值（自动钳位）
 * @return 1=写入成功; 0=名字不存在
 */
uint8_t Params_SetByName(const char *name, int32_t val)
{
    uint8_t i;

    for (i = 0; i < PARAM_ID_NUM; i++) {
        param_desc_t *d = &s_desc[i];

        if ((d->ptr == 0) || (name == 0)) { continue; }   /* 废弃空槽/空名 */
        if (strcmp(d->name, name) == 0) {
            if (val < d->min) { val = d->min; }
            if (val > d->max) { val = d->max; }
            *d->ptr = val;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 参数个数
 */
uint8_t Params_Count(void)
{
    return PARAM_ID_NUM;
}

/**
 * @brief 按索引遍历（调试打印/上位机枚举用）
 *
 * @param idx  索引 [0, Params_Count())
 * @param name 输出参数名（静态字符串, 无需释放）
 * @return 当前值; idx 越界返回 0 且 name 置空
 */
int32_t Params_GetByIndex(uint8_t idx, const char **name)
{
    if (idx >= PARAM_ID_NUM || s_desc[idx].ptr == 0) {
        if (name != 0) { *name = 0; }
        return 0;
    }

    if (name != 0) { *name = s_desc[idx].name; }
    return *s_desc[idx].ptr;
}
