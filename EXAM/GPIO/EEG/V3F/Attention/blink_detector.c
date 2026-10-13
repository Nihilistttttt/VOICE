#include "blink_detector.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* ---------- �ڲ����� ---------- */
static const real_t kDefaultProminenceK     = 4.0f;
static const real_t kMinIntervalMs          = 150.0f;
static const real_t kMinDurationMs          = 50.0f;
static const real_t kMaxDurationMs          = 500.0f;
static const real_t kAbsoluteThresholdUv    = 60.0f;
static const real_t kMinProminenceUv        = 30.0f;
static const real_t kMinSlopeUvPerSample    = 8.0f;
static const real_t kLookaheadMs           = 400.0f;
static const real_t kBufferSec             = 3.0f;
static const real_t kPolaritySec           = 2.0f;
static const real_t kPolaritySkewThreshold = 0.3f;

/* ---------- Ԥ�����ͨϵ�� (4�� Butterworth 0.1~10Hz, 250SPS) ---------- */
static void design_bandpass_sos_0_1_10_250(real_t *b0, real_t *b1, real_t *b2,
                                           real_t *a1, real_t *a2) {
    b0[0] = 0.93637848f; b1[0] = -1.87275696f; b2[0] = 0.93637848f;
    a1[0] = -1.95979169f; a2[0] = 0.96227012f;
    b0[1] = 1.0f;        b1[1] = -2.0f;        b2[1] = 1.0f;
    a1[1] = -1.90886498f; a2[1] = 0.91127901f;
}

/* ---------- �ڲ������������� ---------- */

static int  find_local_maxima(const real_t *x, int n, int *peaks, int max_peaks);
static void compute_prominences(const real_t *x, int n, const int *peaks, int np,
                                real_t *proms, int *left_bases, int *right_bases);
static void compute_widths(const real_t *x, int n, const int *peaks, int np,
                           const real_t *proms, const int *left_bases, const int *right_bases,
                           real_t rel_height, real_t *widths, real_t *left_ips, real_t *right_ips);
static real_t central_diff_abs_max(const real_t *x, int n, int start, int end);

/* ---------- �����¼���ʷ���� ---------- */
static void event_history_push(BlinkDetector_t *bd, const BlinkEvent_t *ev) {
    if (bd->hist_count < BLINK_HISTORY_CAPACITY) {
        // ������δ��
        int tail = (bd->hist_head + bd->hist_count) % BLINK_HISTORY_CAPACITY;
        bd->event_history[tail] = *ev;
        bd->hist_count++;
    } else {
        // �������
        bd->event_history[bd->hist_head] = *ev;
        bd->hist_head = (bd->hist_head + 1) % BLINK_HISTORY_CAPACITY;
    }
}

/* ---------- �����ӿ�ʵ�� ---------- */

int blink_detector_init(BlinkDetector_t *bd, double sample_rate) {
    memset(bd, 0, sizeof(BlinkDetector_t));
    bd->sample_rate = (real_t)sample_rate;
    bd->prominence_k = kDefaultProminenceK;

    bd->min_interval_samples = (int)(kMinIntervalMs * sample_rate / 1000.0);
    bd->min_duration_samples = (int)(kMinDurationMs * sample_rate / 1000.0);
    if (bd->min_duration_samples < 1) bd->min_duration_samples = 1;
    bd->max_duration_samples = (int)(kMaxDurationMs * sample_rate / 1000.0);
    bd->absolute_threshold = kAbsoluteThresholdUv;
    bd->min_prominence = kMinProminenceUv;
    bd->min_slope = kMinSlopeUvPerSample;
    bd->lookahead_samples = (int)(kLookaheadMs * sample_rate / 1000.0);
    bd->buffer_capacity = (int)(kBufferSec * sample_rate);
    bd->polarity_min_samples = (int)(kPolaritySec * sample_rate);
    bd->polarity_skew_threshold = kPolaritySkewThreshold;

    // �����ڴ�
    bd->filt_buffer = (real_t*)malloc(bd->buffer_capacity * sizeof(real_t));
    bd->raw_buffer  = (real_t*)malloc(bd->buffer_capacity * sizeof(real_t));
    bd->valid_buffer = (uint8_t*)malloc(bd->buffer_capacity * sizeof(uint8_t));
    bd->polarity_scratch = (real_t*)malloc(bd->buffer_capacity * sizeof(real_t));
    if (!bd->filt_buffer || !bd->raw_buffer || !bd->valid_buffer || !bd->polarity_scratch) {
        blink_detector_deinit(bd);
        return -1;
    }
    bd->buffer_allocated = 1;

    // ��ʼ���˲���ϵ����״̬
    design_bandpass_sos_0_1_10_250(bd->bp_b0, bd->bp_b1, bd->bp_b2, bd->bp_a1, bd->bp_a2);
    memset(bd->bp_w1, 0, sizeof(bd->bp_w1));
    memset(bd->bp_w2, 0, sizeof(bd->bp_w2));

    return 0;
}

void blink_detector_process_sample(BlinkDetector_t *bd,
                                   real_t raw_sample, real_t filt_sample,
                                   uint8_t valid) {
    // ���뻺�壨�������ƶ�����ʱ������ɣ�
    if (bd->buf_count < bd->buffer_capacity) {
        bd->filt_buffer[bd->buf_count] = filt_sample;
        bd->raw_buffer[bd->buf_count]  = raw_sample;
        bd->valid_buffer[bd->buf_count] = valid;
        bd->buf_count++;
    } else {
        memmove(bd->filt_buffer, bd->filt_buffer + 1,
                (bd->buffer_capacity - 1) * sizeof(real_t));
        memmove(bd->raw_buffer,  bd->raw_buffer + 1,
                (bd->buffer_capacity - 1) * sizeof(real_t));
        memmove(bd->valid_buffer, bd->valid_buffer + 1,
                (bd->buffer_capacity - 1) * sizeof(uint8_t));
        bd->filt_buffer[bd->buffer_capacity - 1] = filt_sample;
        bd->raw_buffer[bd->buffer_capacity - 1]  = raw_sample;
        bd->valid_buffer[bd->buffer_capacity - 1] = valid;
    }
    bd->global_sample_index++;
}

void blink_detector_finish_block(BlinkDetector_t *bd,
                                 double block_timestamp_ms, int block_len) {
    int n = bd->buf_count;
    if (n < bd->lookahead_samples * 2) {
        // ���ݲ��㣬������ʱ���
        bd->last_ts_ms = (real_t)block_timestamp_ms;
        return;
    }

    // ---- ��������Ӧ ----
    if (bd->polarity == 0 && n >= bd->polarity_min_samples) {
        const real_t *x = bd->filt_buffer;
        real_t mean = 0.0f;
        for (int i = 0; i < n; i++) mean += x[i];
        mean /= n;

        real_t var = 0.0f, skew = 0.0f;
        for (int i = 0; i < n; i++) {
            real_t d = x[i] - mean;
            var += d * d;
            skew += d * d * d;
        }
        var /= n;
        skew /= n;
        real_t std = sqrtf(var);
        if (std > 1e-9f) {
            real_t skew_norm = skew / (std * std * std);
            if (fabsf(skew_norm) > bd->polarity_skew_threshold) {
                bd->polarity = (skew > 0) ? 1 : -1;
            }
        }
    }
    int pol = (bd->polarity != 0) ? bd->polarity : 1;

    // ---- ���Ի��ź� ----
    for (int i = 0; i < n; i++) {
        bd->polarity_scratch[i] = pol * bd->filt_buffer[i];
    }

    // ---- ��ֵ��� ----
    int peaks[BLINK_MAX_EVENTS];
    int np = find_local_maxima(bd->polarity_scratch, n, peaks, BLINK_MAX_EVENTS);
    if (np == 0) {
        bd->last_ts_ms = (real_t)block_timestamp_ms;
        return;
    }

    // ---- Prominence ��������ֵ���� ----
    real_t proms[BLINK_MAX_EVENTS];
    int left_bases[BLINK_MAX_EVENTS], right_bases[BLINK_MAX_EVENTS];
    compute_prominences(bd->polarity_scratch, n, peaks, np, proms, left_bases, right_bases);

    int keep[BLINK_MAX_EVENTS] = {0};
    real_t min_prom = bd->prominence_k * 1.0f; // �������Ƽ�Ϊ 1.0��ʵ��Ӧ���� MAD
    if (min_prom < bd->min_prominence) min_prom = bd->min_prominence;
    for (int i = 0; i < np; i++) {
        if (proms[i] >= min_prom) keep[i] = 1;
    }

    // ---- Width ��������� ----
    real_t widths[BLINK_MAX_EVENTS], left_ips[BLINK_MAX_EVENTS], right_ips[BLINK_MAX_EVENTS];
    compute_widths(bd->polarity_scratch, n, peaks, np, proms, left_bases, right_bases,
                   0.85f, widths, left_ips, right_ips);

    // ---- ������ȷ���¼� ----
    bd->pending_count = 0;
    const int confirm_boundary = n - bd->lookahead_samples;
    for (int i = 0; i < np && bd->pending_count < BLINK_MAX_EVENTS; i++) {
        if (!keep[i]) continue;
        int peak_local = peaks[i];
        if (peak_local > confirm_boundary) continue;  // δȷ��
        if (!bd->valid_buffer[peak_local]) continue;
        real_t duration_ms = widths[i] / bd->sample_rate * 1000.0f;

        // ---- 1. б�ʹ��� ----
        int s_start = (int)(left_ips[i]) - 5;
        if (s_start < 0) s_start = 0;
        int s_end   = (int)(right_ips[i]) + 5;
        if (s_end >= n) s_end = n - 1;
        real_t max_slope = central_diff_abs_max(bd->polarity_scratch, n, s_start, s_end);
        if (max_slope < bd->min_slope) continue;

        // ---- 2. ʱ������ ----
        if (duration_ms < kMinDurationMs || duration_ms > kMaxDurationMs) continue;

        // ---- 3. ������� ----
        long global_peak = bd->global_sample_index - n + peak_local;
        if (global_peak - bd->last_blink_index < bd->min_interval_samples) continue;

        // ---- 4. ���ȹ��� (ԭʼ�źŷ��ֵ) ----
        int amp_r = (int)(0.3f * bd->sample_rate);
        int a_s = peak_local - amp_r; if (a_s < 0) a_s = 0;
        int a_e = peak_local + amp_r; if (a_e >= n) a_e = n - 1;
        real_t raw_min = bd->raw_buffer[a_s], raw_max = bd->raw_buffer[a_s];
        for (int k = a_s + 1; k <= a_e; k++) {
            if (bd->raw_buffer[k] < raw_min) raw_min = bd->raw_buffer[k];
            if (bd->raw_buffer[k] > raw_max) raw_max = bd->raw_buffer[k];
        }
        real_t amp = raw_max - raw_min;
        if (amp < bd->absolute_threshold) continue;

        // ---- 5. �����ϸ���� ----
        if (duration_ms > 120.0f && max_slope < 10.0f && amp < 150.0f) continue;

        // ---- 6. ��ݶ� (EMG) ���� ----
        int seg_s = (int)(left_ips[i]); if (seg_s < 0) seg_s = 0;
        int seg_e = (int)(right_ips[i]) + 1; if (seg_e >= n) seg_e = n - 1;
        real_t path_len = 0.0f;
        for (int k = seg_s + 1; k < seg_e; k++) {
            path_len += fabsf(bd->polarity_scratch[k] - bd->polarity_scratch[k-1]);
        }
        real_t peak_val = bd->polarity_scratch[peak_local];
        real_t start_val = bd->polarity_scratch[seg_s];
        real_t end_val   = bd->polarity_scratch[seg_e - 1];
        real_t expected = fabsf(peak_val - start_val) + fabsf(peak_val - end_val);
        if (expected < 1.0f) expected = 1.0f;
        if (path_len / expected > 1.7f) continue;

        // ---- �¼����� ----
        bd->last_blink_index = global_peak;
        real_t confidence = fminf(1.0f, proms[i] / (bd->min_prominence * 3.0f));
        real_t start_time = (real_t)block_timestamp_ms - (n - left_ips[i]) / bd->sample_rate * 1000.0f;
        real_t peak_time  = (real_t)block_timestamp_ms - (n - peak_local) / bd->sample_rate * 1000.0f;

        BlinkEvent_t ev = {
            .start_time = start_time,
            .duration = duration_ms,
            .peak_amplitude = amp,
            .peak_position = peak_time,
            .confidence = confidence
        };
        bd->pending_events[bd->pending_count++] = ev;
        bd->total_blinks++;

        // ���뻷����ʷ
        event_history_push(bd, &ev);
    }

    bd->last_ts_ms = (real_t)block_timestamp_ms;
}

int blink_detector_get_pending_events(BlinkDetector_t *bd, BlinkEvent_t *out, int max_out) {
    int n = (bd->pending_count < max_out) ? bd->pending_count : max_out;
    memcpy(out, bd->pending_events, n * sizeof(BlinkEvent_t));
    bd->pending_count = 0;
    return n;
}

void blink_detector_get_statistics(const BlinkDetector_t *bd, BlinkStatistics_t *stats) {
    memset(stats, 0, sizeof(BlinkStatistics_t));
    stats->total_blinks = bd->total_blinks;

    if (bd->hist_count == 0) return;

    real_t cur_time = bd->last_ts_ms;
    real_t window_start = cur_time - BLINK_RECENT_MS;
    int recent_count = 0;
    real_t dur_sum = 0.0f;
    int slow_count = 0;

    // ����������ʷ
    for (int i = 0; i < bd->hist_count; i++) {
        int idx = (bd->hist_head + i) % BLINK_HISTORY_CAPACITY;
        const BlinkEvent_t *ev = &bd->event_history[idx];
        if (ev->start_time >= window_start && ev->start_time <= cur_time) {
            recent_count++;
            dur_sum += ev->duration;
            if (ev->duration > 200.0f) slow_count++;
        }
    }

    stats->recent_count = recent_count;
    stats->blink_rate   = (real_t)recent_count;   // ��������ܴ��� (��/����)
    stats->slow_blink_ratio = (recent_count > 0) ? (real_t)slow_count / recent_count : 0.0f;
    stats->avg_duration = (recent_count > 0) ? dur_sum / recent_count : 0.0f;
}

void blink_detector_deinit(BlinkDetector_t *bd) {
    if (bd->buffer_allocated) {
        free(bd->filt_buffer);
        free(bd->raw_buffer);
        free(bd->valid_buffer);
        free(bd->polarity_scratch);
        bd->buffer_allocated = 0;
    }
}

/* ========== �ڲ��˲����⺯��ʵ�� ========== */

static int find_local_maxima(const real_t *x, int n, int *peaks, int max_peaks) {
    int cnt = 0;
    int i = 1;
    while (i < n - 1) {
        if (x[i - 1] < x[i]) {
            int ahead = i + 1;
            while (ahead < n - 1 && x[ahead] == x[i]) ahead++;
            if (x[ahead] < x[i]) {
                int left = i, right = ahead - 1;
                int mid = (left + right) / 2;
                if (cnt < max_peaks) peaks[cnt++] = mid;
                i = ahead;
            }
        }
        i++;
    }
    return cnt;
}

static void compute_prominences(const real_t *x, int n, const int *peaks, int np,
                                real_t *proms, int *left_bases, int *right_bases) {
    for (int p = 0; p < np; p++) {
        int peak = peaks[p];
        real_t peak_val = x[peak];
        // �����
        int i = peak;
        real_t left_min = peak_val;
        int left_base = peak;
        while (i >= 0 && x[i] <= peak_val) {
            if (x[i] < left_min) { left_min = x[i]; left_base = i; }
            i--;
        }
        // �һ���
        i = peak;
        real_t right_min = peak_val;
        int right_base = peak;
        while (i < n && x[i] <= peak_val) {
            if (x[i] < right_min) { right_min = x[i]; right_base = i; }
            i++;
        }
        left_bases[p] = left_base;
        right_bases[p] = right_base;
        real_t lower = (left_min < right_min) ? left_min : right_min;
        proms[p] = peak_val - lower;
    }
}

static void compute_widths(const real_t *x, int n, const int *peaks, int np,
                           const real_t *proms, const int *left_bases, const int *right_bases,
                           real_t rel_height, real_t *widths, real_t *left_ips, real_t *right_ips) {
    for (int p = 0; p < np; p++) {
        int peak = peaks[p];
        real_t h = x[peak] - proms[p] * rel_height;
        // �󽻵�
        int i = peak;
        while (i > left_bases[p] && x[i] > h) i--;
        real_t left = (real_t)i;
        if (i < peak && x[i] < h) left += (h - x[i]) / (x[i+1] - x[i]);
        // �ҽ���
        i = peak;
        while (i < right_bases[p] && x[i] > h) i++;
        real_t right = (real_t)i;
        if (i > peak && x[i] < h) right -= (h - x[i]) / (x[i-1] - x[i]);

        widths[p] = right - left;
        left_ips[p] = left;
        right_ips[p] = right;
    }
}

static real_t central_diff_abs_max(const real_t *x, int n, int start, int end) {
    real_t maxv = 0.0f;
    for (int i = start + 1; i < end - 1; i++) {
        real_t d = fabsf((x[i+1] - x[i-1]) * 0.5f);
        if (d > maxv) maxv = d;
    }
    return maxv;
}