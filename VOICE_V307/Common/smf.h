/*
 * Copyright 2021 The Chromium OS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================================================================
 * Zephyr SMF 裸机移植版 API 头（VOICE 项目）
 * 来源: zephyrproject-rtos/zephyr main 分支 include/zephyr/smf.h
 *
 * 移植差异（详见 Common/smf_port.h 文件头）:
 *   1. 依赖: <zephyr/sys/util.h> / <zephyr/kernel.h> → smf_port.h(标准 C 头)
 *   2. 配置固化: ANCESTOR + INITIAL_TRANSITION 开, INSTRUMENTATION 关
 *   3. SMF_CREATE_STATE 宏: 上游用 IF_ENABLED 按配置裁剪成员, 本移植
 *      配置固定 → 展开写死（免去移植 util.h 的 COND_CODE 宏机器）
 *   4. 注释: 上游 Doxygen 注释译为中文（代码与上游逐行一致, 英文原文
 *      可对照上游同路径文件回溯）
 *============================================================================*/

/**
 * @file
 *
 * @brief 状态机框架（State Machine Framework）头文件
 */

#ifndef ZEPHYR_INCLUDE_SMF_H_
#define ZEPHYR_INCLUDE_SMF_H_

#include "smf_port.h"   /* 移植层: 替代上游 <zephyr/sys/util.h> 与 <zephyr/kernel.h> */

/**
 * @brief 状态机框架 API
 * @defgroup smf 状态机框架 API
 * @version 0.2.0
 * @ingroup os_services
 * @{
 */

/**
 * @brief 创建一个带初始转移的层次化状态（表项构造宏）
 *
 * @param _entry   状态 entry 动作（进入时执行一次）, 可为 NULL
 * @param _run     状态 run 动作（每 tick 执行）, 可为 NULL
 * @param _exit    状态 exit 动作（离开时执行一次）, 可为 NULL
 * @param _parent  父状态表项指针（无层次传 NULL）
 * @param _initial 初始转移子状态指针（无初始转移传 NULL）
 */
/* clang-format off */
#define SMF_CREATE_STATE(_entry, _run, _exit, _parent, _initial)           \
{                                                                          \
	.entry   = (_entry),                                               \
	.run     = (_run),                                                 \
	.exit    = (_exit),                                                \
	.parent  = (_parent),                                              \
	.initial = (_initial),                                             \
}
/* clang-format on */

/**
 * @brief 把用户自定义对象指针转换为状态机上下文指针
 *
 * @param o 指向用户对象的指针（其首成员须为 struct smf_ctx）
 */
#define SMF_CTX(o) ((struct smf_ctx *)o)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 状态动作函数的返回值枚举
 */
enum smf_state_result {
	SMF_EVENT_HANDLED,      /**< 本状态已处理, 不再向父状态传播 */
	SMF_EVENT_PROPAGATE,    /**< 向父状态传播（父 run 会继续执行）*/
};

/**
 * @brief 正在执行的动作类型（供观测钩子使用）
 */
enum smf_action_type {
	SMF_ACTION_ENTRY,       /**< entry 动作 */
	SMF_ACTION_RUN,         /**< run 动作 */
	SMF_ACTION_EXIT,        /**< exit 动作 */
};

/** 经观测钩子上报的错误码 */
#define SMF_ERR_NULL_TRANSITION    1 /**< smf_set_state 收到 NULL 目标状态 */
#define SMF_ERR_TRANSITION_IN_EXIT 2 /**< 在 exit 动作内调用了 smf_set_state */

#if defined(CONFIG_SMF_INSTRUMENTATION) || defined(__DOXYGEN__)
/* 钩子函数类型前置声明 */
struct smf_ctx;
struct smf_state;

/**
 * @brief 转移观测钩子: 当前状态指针已更新、新状态 entry 尚未执行时调用
 *
 * @param ctx    状态机上下文
 * @param source 转移源状态
 * @param dest   转移目标状态（更新后的 current）
 */
typedef void (*smf_transition_hook)(struct smf_ctx *ctx, const struct smf_state *source,
				    const struct smf_state *dest);

/**
 * @brief 动作观测钩子: 每个动作（entry/run/exit）执行前调用
 *
 * @param ctx         状态机上下文
 * @param state       即将执行动作的状态
 * @param action_type 动作类型（entry/run/exit）
 */
typedef void (*smf_action_hook)(struct smf_ctx *ctx, const struct smf_state *state,
				enum smf_action_type action_type);

/**
 * @brief 错误观测钩子: 检测到非法操作时调用
 *
 * @param ctx        状态机上下文
 * @param error_code SMF_ERR_* 错误码
 */
typedef void (*smf_error_hook)(struct smf_ctx *ctx, int error_code);

/**
 * @brief 可选观测钩子集合（本移植默认关闭 INSTRUMENTATION）
 *
 * 任一成员可为 NULL 表示跳过该通知。
 */
struct smf_hooks {
	smf_transition_hook on_transition;  /**< 转移钩子 */
	smf_action_hook on_action;          /**< 动作钩子 */
	smf_error_hook on_error;            /**< 错误钩子 */
};
#endif /* CONFIG_SMF_INSTRUMENTATION */

/**
 * @brief 状态 entry / exit 动作的函数指针类型
 *
 * @param obj 指向用户对象的指针（即 smf_set_initial 传入的 ctx 所在容器）
 */
typedef void (*state_method)(void *obj);

/**
 * @brief 状态 run 动作的函数指针类型
 *
 * @param obj 指向用户对象的指针
 * @return 事件是否向父状态传播（未启用 ANCESTOR 时忽略）
 */
typedef enum smf_state_result (*state_execution)(void *obj);

/** 通用状态表项: 一组动作函数指针 + 层次关系 */
struct smf_state {
	/** 可选: 进入本状态时执行一次 */
	const state_method entry;

	/** 可选: 状态机主循环期间反复执行 */
	const state_execution run;

	/** 可选: 离开本状态时执行一次 */
	const state_method exit;
#if CONFIG_SMF_ANCESTOR_SUPPORT /* 移植注记: 上游为 #ifdef, 恒定义时二者等价 */
	/**
	 * 可选父状态: 为多个子状态提供共享的 entry/run/exit 实现。
	 *   entry: 父先于子执行
	 *   run:   父后于子执行
	 *   exit:  父后于子执行
	 * 注: 两个同父子状态之间互转时, 父状态的 exit/entry 不执行。
	 */
	const struct smf_state *parent;

#if CONFIG_SMF_INITIAL_TRANSITION
	/**
	 * 可选初始转移子状态（叶子状态传 NULL）: 进入本状态时自动
	 * 继续下探到 initial 链的叶子。
	 */
	const struct smf_state *initial;
#endif /* CONFIG_SMF_INITIAL_TRANSITION */
#endif /* CONFIG_SMF_ANCESTOR_SUPPORT */
};

/** 状态机当前上下文 */
struct smf_ctx {
	/** 当前正在执行的状态 */
	const struct smf_state *current;
	/** 上一次执行的状态 */
	const struct smf_state *previous;

#if CONFIG_SMF_ANCESTOR_SUPPORT
	/** 当前实际执行动作的状态（可能是父状态） */
	const struct smf_state *executing;
#endif /* CONFIG_SMF_ANCESTOR_SUPPORT */
	/**
	 * 由 smf_set_terminate 设置; 非零时 smf_run_state 返回该值,
	 * 用于终止状态机。
	 */
	int32_t terminate_val;
	/**
	 * SMF 内部用它转型为 "struct internal_ctx", 跟踪转移/终止等
	 * 内部标志（使用方不得读写）。
	 */
	uint32_t internal;

#ifdef CONFIG_SMF_INSTRUMENTATION
	/** 可选观测钩子指针（测试与调试用） */
	const struct smf_hooks *hooks;
#endif /* CONFIG_SMF_INSTRUMENTATION */
};

/**
 * @brief 初始化状态机并进入初始状态
 *
 * @param ctx        状态机上下文
 * @param init_state 初始状态（层次机沿 initial 链下探到叶子）
 */
void smf_set_initial(struct smf_ctx *ctx, const struct smf_state *init_state);

/**
 * @brief 状态转移: 编排旧状态的 exit 与新状态的 entry。
 *        层次机的最近公共祖先（LCA）的 exit/entry 不会执行。
 *
 * @param ctx       状态机上下文
 * @param new_state 目标状态（NULL 合法, 等效退出全部状态）
 */
void smf_set_state(struct smf_ctx *ctx, const struct smf_state *new_state);

/**
 * @brief 终止状态机
 *
 * @param ctx 状态机上下文
 * @param val 非零终止值, 由 smf_run_state 返回给调用方
 */
void smf_set_terminate(struct smf_ctx *ctx, int32_t val);

#ifdef CONFIG_SMF_INSTRUMENTATION
/**
 * @brief 为状态机设置观测钩子
 *
 * 必须在 smf_set_initial() 之后调用——set_initial 会把钩子指针清空。
 * set_initial 期间执行的 entry 动作不会被钩子捕获。
 *
 * @param ctx   状态机上下文
 * @param hooks 钩子集合指针, 传 NULL 关闭; 所指结构体须与状态机同生命周期
 */
void smf_set_hooks(struct smf_ctx *ctx, const struct smf_hooks *hooks);
#endif /* CONFIG_SMF_INSTRUMENTATION */

/**
 * @brief 执行状态机一次迭代（层次机会连带父状态的 run 动作）
 *
 * @param ctx 状态机上下文
 * @return 非零值表示状态机应终止——可能是到达终态, 或检测到应终止的错误
 */
int32_t smf_run_state(struct smf_ctx *ctx);

/**
 * @brief 获取当前叶子状态
 *
 * @note 若层次机未正确配置 initial 转移, 可能返回父状态（病态情形）。
 *
 * @param ctx 状态机上下文
 * @return 当前叶子状态
 */
static inline const struct smf_state *smf_get_current_leaf_state(const struct smf_ctx *const ctx)
{
	return ctx->current;
}

/**
 * @brief 获取当前实际执行动作的状态（可能是父状态）
 *
 * @param ctx 状态机上下文
 * @return 当前执行中的状态
 */
static inline const struct smf_state *
smf_get_current_executing_state(const struct smf_ctx *const ctx)
{
#if CONFIG_SMF_ANCESTOR_SUPPORT
	return ctx->executing;
#else
	return ctx->current;
#endif /* CONFIG_SMF_ANCESTOR_SUPPORT */
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_SMF_H_ */
