/*
 * Copyright 2021 The Chromium OS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================================================================
 * Zephyr SMF 裸机移植版实现（VOICE 项目）
 * 来源: zephyrproject-rtos/zephyr main 分支 lib/smf/smf.c
 *
 * 移植差异（详见 Common/smf_port.h 文件头）:
 *   1. include: <zephyr/smf.h> → "smf.h"; 删除 <zephyr/logging/log.h>
 *      与 LOG_MODULE_REGISTER（裸机无 Zephyr 日志子系统）
 *   2. LOG_ERR → SMF_LOG_ERR（smf_port.h 的静默 shim, 正常路径不触发）
 *   3. 注释译为中文; 状态机核心逻辑（entry/run/exit 编排、LCA 计算、
 *      terminate 语义）与上游逐行一致。
 *============================================================================*/

#include "smf.h"

/**
 * @brief 私有结构体（仅本文件使用）: 转型解释 smf_ctx.internal 成员,
 *        跟踪转移/终止等内部标志。不直接使用, 只做指针转型。
 */
struct internal_ctx {
	bool new_state: 1;      /* 本轮 run 中发生了状态转移 */
	bool terminate: 1;      /* 已请求终止 */
	bool is_exit: 1;        /* 正处于 exit 阶段（禁止再次转移）*/
	bool handled: 1;        /* 事件已被处理, 停止向父状态传播 */
};

#ifdef CONFIG_SMF_INSTRUMENTATION
/* 动作观测钩子: 钩子存在且注册了对应回调时调用 */
static inline void invoke_action_hook(struct smf_ctx *ctx, const struct smf_state *state,
				      enum smf_action_type type)
{
	if (ctx->hooks && ctx->hooks->on_action) {
		ctx->hooks->on_action(ctx, state, type);
	}
}

/* 转移观测钩子 */
static inline void invoke_transition_hook(struct smf_ctx *ctx, const struct smf_state *source,
					  const struct smf_state *dest)
{
	if (ctx->hooks && ctx->hooks->on_transition) {
		ctx->hooks->on_transition(ctx, source, dest);
	}
}

/* 错误观测钩子 */
static inline void invoke_error_hook(struct smf_ctx *ctx, int code)
{
	if (ctx->hooks && ctx->hooks->on_error) {
		ctx->hooks->on_error(ctx, code);
	}
}

#define INVOKE_ACTION_HOOK(ctx, state, type) invoke_action_hook(ctx, state, type)

#define INVOKE_TRANSITION_HOOK(ctx, source, dest) invoke_transition_hook(ctx, source, dest)

#define INVOKE_ERROR_HOOK(ctx, code) invoke_error_hook(ctx, code)

#else /* CONFIG_SMF_INSTRUMENTATION */

/* 未启用观测: 全部展开为空语句, 零开销 */
#define INVOKE_ACTION_HOOK(ctx, state, type)                                                       \
	do {                                                                                       \
	} while (false)

#define INVOKE_TRANSITION_HOOK(ctx, source, dest)                                                  \
	do {                                                                                       \
	} while (false)

#define INVOKE_ERROR_HOOK(ctx, code)                                                               \
	do {                                                                                       \
	} while (false)

#endif /* CONFIG_SMF_INSTRUMENTATION */

#ifdef CONFIG_SMF_ANCESTOR_SUPPORT
/**
 * @brief 判断 test_state 是否为 target_state 的后代（沿 parent 链上溯）
 *
 * @param test_state 待判定的状态
 * @param target_state 疑似祖先状态
 * @return true=是后代（含相等）
 */
static bool is_descendant_of(const struct smf_state *test_state,
			     const struct smf_state *target_state)
{
	for (const struct smf_state *state = test_state; state != NULL; state = state->parent) {
		if (target_state == state) {
			return true;
		}
	}

	return false;
}

/**
 * @brief 从 states 沿 parent 链上溯, 找到父状态恰为 parent 的最近状态
 *        （用于定位"进入 parent 后第一个该执行 entry 的子状态"）
 *
 * @param states 起点
 * @param parent 目标父状态
 * @return parent 的直接子状态; 不存在则 NULL
 */
static const struct smf_state *get_child_of(const struct smf_state *states,
					    const struct smf_state *parent)
{
	const struct smf_state *state = states;

	while (state != NULL) {
		if (state->parent == parent) {
			return state;
		}

		state = state->parent;
	}

	return NULL;
}

/**
 * @brief 求两个状态（互不为祖先）的最近公共祖先（LCA）
 *
 * @param source 转移源状态
 * @param dest 转移目标状态
 * @return LCA 状态; 无公共祖先则 NULL
 */
static const struct smf_state *get_lca_of(const struct smf_state *source,
					  const struct smf_state *dest)
{
	for (const struct smf_state *ancestor = source->parent; ancestor != NULL;
	     ancestor = ancestor->parent) {
		/* dest 是某个祖先的后代 → 该祖先即最近公共祖先 */
		if (is_descendant_of(dest, ancestor)) {
			return ancestor;
		}
	}

	return NULL;
}

/**
 * @brief 从 topmost 的直接子状态一路执行 entry 到 new_state
 *
 * @param ctx 状态机上下文
 * @param new_state 转移目标状态
 * @param topmost 不执行其 entry 的最顶层状态
 * @return true=状态机应终止（entry 内调用了 terminate）
 */
static bool smf_execute_all_entry_actions(struct smf_ctx *const ctx,
					  const struct smf_state *new_state,
					  const struct smf_state *topmost)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;

	if (new_state == topmost) {
		/* 目标即 topmost 本身: 没有子状态需要进入 */
		return false;
	}

	for (const struct smf_state *to_execute = get_child_of(new_state, topmost);
	     to_execute != NULL && to_execute != new_state;
	     to_execute = get_child_of(new_state, to_execute)) {
		/* 记录正在执行 entry 的状态——防 entry 内又调用 smf_set_state() */
		ctx->executing = to_execute;
		/* 执行除 topmost 之外的所有 entry */
		if (to_execute->entry) {
			INVOKE_ACTION_HOOK(ctx, to_execute, SMF_ACTION_ENTRY);
			to_execute->entry(ctx);

			/* entry 内已请求终止则不再继续 */
			if (internal->terminate) {
				ctx->executing = ctx->current;
				return true;
			}
		}
	}

	/* 最后执行目标状态自己的 entry */
	ctx->executing = new_state;
	if (new_state->entry) {
		INVOKE_ACTION_HOOK(ctx, new_state, SMF_ACTION_ENTRY);
		new_state->entry(ctx);

		/* entry 内已请求终止则不再继续 */
		if (internal->terminate) {
			ctx->executing = ctx->current;
			return true;
		}
	}

	ctx->executing = ctx->current;

	return false;
}

/**
 * @brief 执行所有祖先状态的 run 动作（子先父后, 逆层次序）
 *
 * @param ctx 状态机上下文
 */
static void smf_execute_ancestor_run_actions(struct smf_ctx *const ctx)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;
	/* 逆序执行所有祖先 run */

	/* 当前状态已请求终止 → 直接返回 */
	if (internal->terminate) {
		return;
	}

	/* 子状态已转移或已处理 → 不再向父状态传播 */
	if (internal->new_state || internal->handled) {
		return;
	}

	/* 逐层执行父状态 run */
	for (const struct smf_state *tmp_state = ctx->current->parent; tmp_state != NULL;
	     tmp_state = tmp_state->parent) {
		/* 记录执行位置——防祖先内调用 smf_set_state() */
		ctx->executing = tmp_state;
		/* 执行父状态 run */
		if (tmp_state->run) {
			INVOKE_ACTION_HOOK(ctx, tmp_state, SMF_ACTION_RUN);
			enum smf_state_result rc = tmp_state->run(ctx);

			if (rc == SMF_EVENT_HANDLED) {
				internal->handled = true;
			}
			/* 已请求终止则不再继续 */
			if (internal->terminate) {
				ctx->executing = ctx->current;
				return;
			}

			/* 该状态已处理/已转移 → 停止传播 */
			if (internal->new_state || internal->handled) {
				break;
			}
		}
	}

	/* 所有 run 执行完毕 */

	ctx->executing = ctx->current;
}

/**
 * @brief 从 ctx->current 一路执行 exit 到 topmost 的直接子状态
 *
 * @param ctx 状态机上下文
 * @param topmost 不执行其 exit 的最顶层状态
 * @return true=状态机应终止（exit 内调用了 terminate）
 */
static bool smf_execute_all_exit_actions(struct smf_ctx *const ctx, const struct smf_state *topmost)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;
	const struct smf_state *tmp_state = ctx->executing;

	for (const struct smf_state *to_execute = ctx->current;
	     to_execute != NULL && to_execute != topmost; to_execute = to_execute->parent) {
		if (to_execute->exit) {
			ctx->executing = to_execute;
			INVOKE_ACTION_HOOK(ctx, to_execute, SMF_ACTION_EXIT);
			to_execute->exit(ctx);

			/* exit 内已请求终止则不再继续 */
			if (internal->terminate) {
				ctx->executing = tmp_state;
				return true;
			}
		}
	}

	ctx->executing = tmp_state;

	return false;
}
#endif /* CONFIG_SMF_ANCESTOR_SUPPORT */

/**
 * @brief 把状态机内部标志复位为默认值。
 *        smf_set_initial() 与 smf_set_state() 入口、smf_run_state() 开头调用。
 *
 * @param ctx 状态机上下文
 */
static void smf_clear_internal_state(struct smf_ctx *const ctx)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;

	internal->is_exit = false;
	internal->terminate = false;
	internal->handled = false;
	internal->new_state = false;
}

void smf_set_initial(struct smf_ctx *const ctx, const struct smf_state *init_state)
{
#ifdef CONFIG_SMF_INITIAL_TRANSITION
	/*
	 * 最终目标是 init_state 沿 initial 链能到达的最深叶子——
	 * 把它作为真正的初始状态。
	 */
	while (init_state->initial != NULL) {
		init_state = init_state->initial;
	}
#endif

	smf_clear_internal_state(ctx);
	ctx->current = init_state;
	ctx->previous = NULL;
	ctx->terminate_val = 0;

#ifdef CONFIG_SMF_INSTRUMENTATION
	ctx->hooks = NULL;
#endif /* CONFIG_SMF_INSTRUMENTATION */

#ifdef CONFIG_SMF_ANCESTOR_SUPPORT
	struct internal_ctx *const internal = (void *)&ctx->internal;

	ctx->executing = init_state;
	/* topmost 是 init_state 沿父链的根（parent == NULL）*/
	const struct smf_state *topmost = get_child_of(init_state, NULL);

	/* 先执行根状态的 entry——smf_execute_all_entry_actions() 不会执行它 */
	if (topmost->entry) {
		ctx->executing = topmost;
		INVOKE_ACTION_HOOK(ctx, topmost, SMF_ACTION_ENTRY);
		topmost->entry(ctx);
		ctx->executing = init_state;
		if (internal->terminate) {
			/* entry 内已请求终止 → 不再继续 */
			return;
		}
	}

	if (smf_execute_all_entry_actions(ctx, init_state, topmost)) {
		/* 已请求终止 → 不再继续 */
		return;
	}
#else
	/* 扁平机: 只执行初始状态自己的 entry */
	if (init_state->entry) {
		INVOKE_ACTION_HOOK(ctx, init_state, SMF_ACTION_ENTRY);
		init_state->entry(ctx);
	}
#endif
}

void smf_set_state(struct smf_ctx *const ctx, const struct smf_state *new_state)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;

	if (new_state == NULL) {
		SMF_LOG_ERR("new_state cannot be NULL");
		INVOKE_ERROR_HOOK(ctx, SMF_ERR_NULL_TRANSITION);
		return;
	}

	/*
	 * exit 阶段内调用 smf_set_state 没有意义——已经处于转移之中,
	 * 指定的目标状态必然被忽略。
	 */
	if (internal->is_exit) {
		SMF_LOG_ERR("Calling %s from exit action", __func__);
		INVOKE_ERROR_HOOK(ctx, SMF_ERR_TRANSITION_IN_EXIT);
		return;
	}

#ifdef CONFIG_SMF_ANCESTOR_SUPPORT
	const struct smf_state *topmost;

	if (ctx->executing != new_state && ctx->executing->parent == new_state->parent) {
		/* 兄弟转移优化: 同父的两个子状态互转 */
		topmost = ctx->executing->parent;
	} else if (is_descendant_of(ctx->executing, new_state)) {
		/* 目标是我们当前状态的祖先 */
		topmost = new_state;
	} else if (is_descendant_of(new_state, ctx->executing)) {
		/* 我们是目标状态的祖先 */
		topmost = ctx->executing;
	} else {
		/* 无直接层次关系 → 求最近公共祖先 */
		topmost = get_lca_of(ctx->executing, new_state);
	}

	internal->is_exit = true;
	internal->new_state = true;

	/* 自下而上执行 exit（不含 topmost）*/
	if (smf_execute_all_exit_actions(ctx, topmost)) {
		/* exit 内已请求终止 → 不再继续 */
		return;
	}

	/* 自转移: 单独补执行 exit */
	if ((ctx->executing == new_state) && (new_state->exit)) {
		INVOKE_ACTION_HOOK(ctx, new_state, SMF_ACTION_EXIT);
		new_state->exit(ctx);

		/* exit 内已请求终止 → 不再继续 */
		if (internal->terminate) {
			return;
		}
	}

	internal->is_exit = false;

	/* 自转移: 单独补执行 entry */
	if ((ctx->executing == new_state) && (new_state->entry)) {
		INVOKE_ACTION_HOOK(ctx, new_state, SMF_ACTION_ENTRY);
		new_state->entry(ctx);

		/* entry 内已请求终止 → 不再继续 */
		if (internal->terminate) {
			return;
		}
	}
#ifdef CONFIG_SMF_INITIAL_TRANSITION
	/*
	 * 最终目标是 new_state 沿 initial 链能到达的最深叶子。
	 */
	while (new_state->initial != NULL) {
		new_state = new_state->initial;
	}
#endif

	/* 更新状态变量 */
	ctx->previous = ctx->current;
	ctx->current = new_state;
	ctx->executing = new_state;

	INVOKE_TRANSITION_HOOK(ctx, ctx->previous, ctx->current);

	/* 自上而下执行 entry（不含 topmost）*/
	if (smf_execute_all_entry_actions(ctx, new_state, topmost)) {
		/* entry 内已请求终止 → 不再继续 */
		return;
	}
#else
	/* 扁平机的转移非常简单: */
	if (ctx->current->exit) {
		internal->is_exit = true;
		INVOKE_ACTION_HOOK(ctx, ctx->current, SMF_ACTION_EXIT);
		ctx->current->exit(ctx);
		/* exit 内已请求终止 → 不再继续 */
		if (internal->terminate) {
			return;
		}
		internal->is_exit = false;
	}
	/* 更新状态变量 */
	ctx->previous = ctx->current;
	ctx->current = new_state;

	INVOKE_TRANSITION_HOOK(ctx, ctx->previous, ctx->current);

	if (ctx->current->entry) {
		INVOKE_ACTION_HOOK(ctx, ctx->current, SMF_ACTION_ENTRY);
		ctx->current->entry(ctx);
		/* entry 内已请求终止 → 不再继续 */
		if (internal->terminate) {
			return;
		}
	}
#endif
}

void smf_set_terminate(struct smf_ctx *const ctx, int32_t val)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;

	internal->terminate = true;
	ctx->terminate_val = val;
}

int32_t smf_run_state(struct smf_ctx *const ctx)
{
	struct internal_ctx *const internal = (void *)&ctx->internal;

	/* 已请求终止 → 直接返回终止值 */
	if (internal->terminate) {
		return ctx->terminate_val;
	}

	/* run 里可能发生转移, 先清内部标志保证转移被正确处理 */
	smf_clear_internal_state(ctx);

#ifdef CONFIG_SMF_ANCESTOR_SUPPORT
	ctx->executing = ctx->current;
	if (ctx->current->run) {
		INVOKE_ACTION_HOOK(ctx, ctx->current, SMF_ACTION_RUN);
		enum smf_state_result rc = ctx->current->run(ctx);

		if (rc == SMF_EVENT_HANDLED) {
			internal->handled = true;
		}
	}

	/* 传播给祖先状态的 run */
	smf_execute_ancestor_run_actions(ctx);
#else
	if (ctx->current->run) {
		INVOKE_ACTION_HOOK(ctx, ctx->current, SMF_ACTION_RUN);
		ctx->current->run(ctx);
	}
#endif

	/* run 内已请求终止 → 返回终止值 */
	if (internal->terminate) {
		return ctx->terminate_val;
	}

	return 0;
}

#ifdef CONFIG_SMF_INSTRUMENTATION
void smf_set_hooks(struct smf_ctx *const ctx, const struct smf_hooks *hooks)
{
	ctx->hooks = hooks;
}
#endif /* CONFIG_SMF_INSTRUMENTATION */
