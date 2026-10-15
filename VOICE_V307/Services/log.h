#ifndef __LOG_H__
#define __LOG_H__

#include <stdint.h>

/*============================================================================
 * 分级日志模块（自研精简版）
 *
 * 借鉴与取舍（详见 docs/VOICE_调试模块调研_20261015.html）:
 *   借鉴 Zephyr logging:   调用点只格式化入队, 慢输出延后; 丢弃计数;
 *                          panic 路径强制刷新(Log_FlushNow)
 *   借鉴 RT-Thread ulog:   级别+标签+运行时过滤; 后端可插拔思想
 *                          (简化为固定双后端); 裸机 = 主循环当 idle 钩子
 *   借鉴 EasyLogger:       缓冲输出模式(RAM 攒批+手动 flush); 单代文件
 *                          轮转; 降级链(后端失效不拖垮业务)
 *   借鉴 Segger RTT:       缓冲满即丢不阻塞(Skip 模式哲学)
 *   舍弃: 后端注册表/多线程异步/多代轮转/颜色/关键词过滤/Flash 插件
 *         ——双后端(串口立即+SD攒批)已覆盖本项目全部需求
 *
 * 两条输出路径:
 *   串口后端: 每条立即 printf(2M 波特率, 阻塞可接受)
 *   SD 后端:  行写入 RAM 缓冲, Log_Service() 限频刷盘(≥5s 或缓冲≥80%),
 *             单次 f_write ≤512B 轮询数 ms 且罕见 —— 不干扰 HID 热循环
 * 降级链:   SD 不可用/写失败 → 后端自动摘除, 串口永远在线
 * 并发保护: MODE=2(U盘导出SD) 进入时调 Log_SD_Suspend() —— FatFs 禁写,
 *           日志转 RAM 环形覆盖; MODE=2 期间不调用 Log_Service()
 * 宿主单测: 本模块不进 hosttest(纯固件设施), 由真机验收
 *============================================================================*/

/* ---- 级别定义 ------------------------------------------------------------*/
#define LOG_LVL_ERROR   0       /* 错误: 功能受损 */
#define LOG_LVL_WARN    1       /* 警告: 自愈/降级发生 */
#define LOG_LVL_INFO    2       /* 信息: 关键流程节点 */
#define LOG_LVL_DEBUG   3       /* 调试: 全量细节 */

/* 编译期下限: 发布版可改为 LOG_LVL_WARN 把 DEBUG/INFO 调用整体裁剪掉
 * (低于下限的宏展开为空, 配合 gc-sections 不占 ROM) */
#ifndef LOG_LVL_COMPILE
#define LOG_LVL_COMPILE LOG_LVL_DEBUG
#endif

/* ---- 输出宏（tag=模块标签, 如 "hid"/"params"/"sd"）-----------------------*/
#if LOG_LVL_COMPILE >= LOG_LVL_ERROR
#define LOGE(tag, ...)  Log_Write(LOG_LVL_ERROR, (tag), __VA_ARGS__)
#else
#define LOGE(tag, ...)  do { } while (0)
#endif
#if LOG_LVL_COMPILE >= LOG_LVL_WARN
#define LOGW(tag, ...)  Log_Write(LOG_LVL_WARN, (tag), __VA_ARGS__)
#else
#define LOGW(tag, ...)  do { } while (0)
#endif
#if LOG_LVL_COMPILE >= LOG_LVL_INFO
#define LOGI(tag, ...)  Log_Write(LOG_LVL_INFO, (tag), __VA_ARGS__)
#else
#define LOGI(tag, ...)  do { } while (0)
#endif
#if LOG_LVL_COMPILE >= LOG_LVL_DEBUG
#define LOGD(tag, ...)  Log_Write(LOG_LVL_DEBUG, (tag), __VA_ARGS__)
#else
#define LOGD(tag, ...)  do { } while (0)
#endif

/**
 * @brief 初始化日志模块（运行时级别默认 DEBUG; SD 后端此时还未挂载）
 */
void Log_Init(void);

/**
 * @brief 写一条日志（级别过滤在函数内完成; 串口立即输出, SD 进缓冲）
 *
 * @param level 级别 LOG_LVL_*
 * @param tag   模块标签字符串(静态存储)
 * @param fmt   printf 风格格式串
 */
void Log_Write(uint8_t level, const char *tag, const char *fmt, ...);

/**
 * @brief 设置运行时级别（串口命令 log=N 直通; N 越界钳位到 0~3）
 */
void Log_SetLevel(int32_t lvl);

/**
 * @brief 查询当前运行时级别
 */
int32_t Log_GetLevel(void);

/**
 * @brief 附加 SD 后端（在 params_sd 挂载成功之后调用一次:
 *        打开/轮转日志文件 + 写 BOOT 头(含 BKP 启动计数) + 冲刷积压行）
 *        USE_SDCARD=0 时为空操作
 */
void Log_SD_Attach(void);

/**
 * @brief 挂起 SD 后端（MODE=2 U盘模式进入时调用: SD 已导出给 USB,
 *        FatFs 禁写; 挂起期间日志仅串口输出, RAM 环形覆盖不落盘）
 */
void Log_SD_Suspend(void);

/**
 * @brief 强制把 RAM 缓冲刷入 SD（致命错误路径/进入低功耗前调用）
 */
void Log_FlushNow(void);

/**
 * @brief 日志服务（主循环每圈调用一次）: 按限频策略把 RAM 缓冲刷入 SD
 *        MODE=2 期间不调用（SD 已导出）; USE_SDCARD=0 时空操作
 */
void Log_Service(void);

#endif /* __LOG_H__ */
