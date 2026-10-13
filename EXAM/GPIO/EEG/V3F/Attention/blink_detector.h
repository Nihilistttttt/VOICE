#ifndef BLINK_DETECTOR_H
#define BLINK_DETECTOR_H

#include <stdint.h>
#include "algo_core.h"   // real_t, BlinkStatistics_t

#ifdef __cplusplus
extern "C" {
#endif

#define BLINK_BUFFER_SIZE      256       // 信号缓冲长度 (>= 2秒)
#define BLINK_MAX_EVENTS       20        // 单次检测最多事件数
#define BLINK_RECENT_MS        60000.0f  // 最近统计窗口 (60秒)
#define BLINK_HISTORY_CAPACITY 200       // 环形历史最大事件数

/* 眨眼事件（内部通用） */
typedef struct {
    real_t start_time;        // ms
    real_t duration;          // ms
    real_t peak_amplitude;    // ?V
    real_t peak_position;     // ms
    real_t confidence;        // 0~1
} BlinkEvent_t;

/* 检测器主结构 */
typedef struct {
    /* ---------- 参数 ---------- */
    real_t sample_rate;
    real_t prominence_k;
    int    min_interval_samples;
    int    min_duration_samples;
    int    max_duration_samples;
    real_t absolute_threshold;
    real_t min_prominence;
    real_t min_slope;
    int    lookahead_samples;
    int    buffer_capacity;

    /* ---------- 带通滤波器 (0.1~10 Hz, 4阶 → 2节) ---------- */
    real_t bp_b0[2], bp_b1[2], bp_b2[2];
    real_t bp_a1[2], bp_a2[2];
    real_t bp_w1[2], bp_w2[2];

    /* ---------- 信号缓冲 ---------- */
    real_t *filt_buffer;       // 滤波后信号
    real_t *raw_buffer;        // 原始信号 (用于幅度计算)
    uint8_t *valid_buffer;     // 有效性
    int     buf_count;         // 当前缓冲中有效点数
    int     buf_write;         // 写指针 (未用，保留)

    /* ---------- 极性自适应 ---------- */
    int     polarity;          // 0=未定, ±1
    int     polarity_min_samples;
    real_t  polarity_skew_threshold;
    real_t *polarity_scratch;  // 临时极性信号

    /* ---------- 事件历史 (环形) ---------- */
    BlinkEvent_t event_history[BLINK_HISTORY_CAPACITY];
    int          hist_head;       // 最旧事件索引
    int          hist_count;      // 当前存储的事件数 (≤ capacity)

    /* ---------- 当前块的检测事件 (临时) ---------- */
    BlinkEvent_t pending_events[BLINK_MAX_EVENTS];
    int          pending_count;

    /* ---------- 全局统计 ---------- */
    uint32_t total_blinks;         // 累计眨眼总数
    long     global_sample_index;  // 全局样本索引
    long     last_blink_index;     // 上一次眨眼全局索引 (用于间隔过滤)
    real_t   last_ts_ms;           // 最新处理的时间戳

    /* ---------- 内存管理 ---------- */
    int      buffer_allocated;     // 是否已动态分配内存
} BlinkDetector_t;

/* ========== 公开接口 ========== */

/* 初始化，返回 0 成功 */
int  blink_detector_init(BlinkDetector_t *bd, double sample_rate);

/* 逐样本处理 (raw 用于幅度，filt 用于波形检测，valid 为真) */
void blink_detector_process_sample(BlinkDetector_t *bd,
                                   real_t raw_sample,
                                   real_t filt_sample,
                                   uint8_t valid);

/* 一个信号块结束时调用，执行峰值检测与事件确认 */
void blink_detector_finish_block(BlinkDetector_t *bd,
                                 double block_timestamp_ms,
                                 int    block_len);

/* 获取本次块确认的眨眼事件 (最多 max_out 个，返回实际个数) */
int  blink_detector_get_pending_events(BlinkDetector_t *bd,
                                       BlinkEvent_t *out,
                                       int max_out);

/* 获取当前眨眼统计 (最近 60 秒) */
void blink_detector_get_statistics(const BlinkDetector_t *bd,
                                   BlinkStatistics_t *stats);

/* 释放资源 */
void blink_detector_deinit(BlinkDetector_t *bd);

#ifdef __cplusplus
}
#endif
#endif