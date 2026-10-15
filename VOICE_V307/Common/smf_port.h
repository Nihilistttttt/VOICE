#ifndef __SMF_PORT_H__
#define __SMF_PORT_H__

/*============================================================================
 * Zephyr SMF 裸机移植层（VOICE 项目专用）
 *
 * 来源: Zephyr upstream main 分支 lib/smf/smf.c + include/zephyr/smf.h
 *       (Copyright 2021 The Chromium OS Authors, Apache-2.0)
 *
 * 移植差异（与上游逐条对应, 便于后续版本同步时 diff 审计）:
 *   1. 头文件依赖: <zephyr/sys/util.h> / <zephyr/kernel.h>
 *      → <stdint.h> <stdbool.h> <stddef.h>（SMF 只用到整型/布尔/NULL）
 *   2. 配置宏: Kconfig 的 CONFIG_SMF_* 在无 Kconfig 的裸机工程里
 *      由本头文件显式定义（0/1 都写出, grep 一次见全貌）:
 *        ANCESTOR_SUPPORT    = 1  层次状态机（s1 父状态共享吹气/中键逻辑）
 *        INITIAL_TRANSITION  = 1  父状态初始转移（依赖 ANCESTOR）
 *        INSTRUMENTATION     = 0  观测钩子（暂不需要, 关掉省 footprint）
 *   3. 日志: <zephyr/logging/log.h> 的 LOG_ERR → SMF_LOG_ERR shim
 *      （默认静默——SMF 仅在"NULL 转移/exit 内转移"两种编程错误时打日志,
 *        正常运行永不触发; 接 printf 会拖累热路径, 故默认空实现）
 *   4. SMF_CREATE_STATE: 上游用 IF_ENABLED(CONFIG_*, ...) 按配置裁剪
 *      初始化器成员; 本移植配置固定, 直接写死展开（免去移植 util.h
 *      的 COND_CODE 宏机器）, 展开结果与上游配置等价时逐字节相同。
 *
 * 本文件之外的两个移植件: smf.h（API 头, 原注释保留）与 smf.c
 * （实现, 仅 include 与 LOG 两处差异）。
 *============================================================================*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*---- 配置宏（对应 Zephyr Kconfig, 见文件头第 2 条差异）--------------------*/
#define CONFIG_SMF_ANCESTOR_SUPPORT    1   /* 层次状态机支持 */
#define CONFIG_SMF_INITIAL_TRANSITION  1   /* 父状态初始转移（依赖 ANCESTOR）*/
/* CONFIG_SMF_INSTRUMENTATION 故意不定义 —— 上游 #ifdef 闭, 省钩子代码 */

/*---- 日志 shim（对应差异第 3 条）------------------------------------------*/
/* 上游 smf.c 仅在 smf_set_state() 收到 NULL 或 exit 动作内调用转移时
 * 打 LOG_ERR —— 均为使用者编程错误, 正常路径不触发。裸机下默认静默;
 * 调试期可改为 printf 前缀 "[SMF] "。 */
#define SMF_LOG_ERR(...)   do { } while (false)

#endif /* __SMF_PORT_H__ */
