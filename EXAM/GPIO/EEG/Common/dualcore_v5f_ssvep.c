#include "dualcore_v5f_ssvep.h"
#include "dualcore_v5f_dsp.h"
#include <math.h>
#include <string.h>

#if defined(Core_V5F)

static const float s_ssvep_target_freqs[SSVEP_NUM_TARGETS] = {11.0f, 13.0f, 15.0f, 17.0f};
static const float s_ssvep_freq_offsets[3] = {-0.05f, 0.0f, 0.05f};
static const float s_ssvep_bank_ranges[SSVEP_NUM_BANKS][2] = {
    {6.0f, 45.0f}, {10.0f, 45.0f}, {14.0f, 45.0f}, {18.0f, 45.0f}
};

static float s_ssvep_fbcca_weights[SSVEP_NUM_BANKS];

static const DualCore_IIR_SOS_Coeff_t s_ssvep_bank_coeff[SSVEP_NUM_BANKS] = {
    {
        .Sec = {
            {.b0=0.0210763774f, .b1=0.0421527548f, .b2=0.0210763774f, .a1=-0.6957205839f, .a2=0.1817637203f},
            {.b0=1.0000000000f, .b1=2.0000000000f, .b2=1.0000000000f, .a1=-0.6975927135f, .a2=0.5688595203f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-1.6938087451f, .a2=0.7241522337f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-1.8872199405f, .a2=0.9099211525f},
        },
        .NumSections = 4
    },
    {
        .Sec = {
            {.b0=0.0148267455f, .b1=0.0296534910f, .b2=0.0148267455f, .a1=-0.8294030252f, .a2=0.2798487665f},
            {.b0=1.0000000000f, .b1=2.0000000000f, .b2=1.0000000000f, .a1=-1.5078821466f, .a2=0.6014636975f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-0.7288510856f, .a2=0.6223360628f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-1.8126621353f, .a2=0.8750342354f},
        },
        .NumSections = 4
    },
    {
        .Sec = {
            {.b0=0.0099411257f, .b1=0.0198822514f, .b2=0.0099411257f, .a1=-0.8906240047f, .a2=0.3744098154f},
            {.b0=1.0000000000f, .b1=2.0000000000f, .b2=1.0000000000f, .a1=-1.3918122910f, .a2=0.5681894832f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-0.7531359576f, .a2=0.6730891293f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-1.7354693921f, .a2=0.8560198392f},
        },
        .NumSections = 4
    },
    {
        .Sec = {
            {.b0=0.0062619328f, .b1=0.0125238656f, .b2=0.0062619328f, .a1=-0.9115332556f, .a2=0.4578986151f},
            {.b0=1.0000000000f, .b1=2.0000000000f, .b2=1.0000000000f, .a1=-1.3130138273f, .a2=0.5806829751f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-0.7724801797f, .a2=0.7210932614f},
            {.b0=1.0000000000f, .b1=-2.0000000000f, .b2=1.0000000000f, .a1=-1.6531952128f, .a2=0.8495903967f},
        },
        .NumSections = 4
    },
};

static float s_ssvep_ring[SSVEP_NUM_CH][SSVEP_WINDOW_SIZE];
static float s_ssvep_filt_ring[SSVEP_NUM_BANKS][SSVEP_NUM_CH][SSVEP_WINDOW_SIZE];
static uint16_t s_ssvep_ring_write;
static uint16_t s_ssvep_ring_count;
static uint16_t s_ssvep_since_last;

static DualCore_IIR_SOS_State_t s_ssvep_notch_state[SSVEP_NUM_CH];
static DualCore_IIR_SOS_Coeff_t s_ssvep_notch_coeff;
static DualCore_IIR_SOS_State_t s_ssvep_bank_state[SSVEP_NUM_BANKS][SSVEP_NUM_CH];

static float s_ssvep_tukey[SSVEP_WINDOW_SIZE];

volatile uint8_t  g_ipc_ssvep_valid = 0;
volatile int8_t   g_ipc_ssvep_raw_index = -1;
volatile int32_t  g_ipc_ssvep_ratio_q10000 = 0;
volatile int32_t  g_ipc_ssvep_best_score_q10000 = 0;
volatile int32_t  g_ipc_ssvep_margin_q10000 = 0;
volatile int32_t  g_ipc_ssvep_scores_q10000[SSVEP_NUM_TARGETS];
volatile uint32_t g_ipc_ssvep_sequence = 0;

volatile uint8_t  g_ssvep_analysis_pending = 0;

static void ssvep_init_notch(void)
{
    float w0 = 2.0f * SSVEP_PI * SSVEP_NOTCH_FREQ / SSVEP_FS;
    float cw = cosf(w0);
    float r = SSVEP_NOTCH_RHO;
    s_ssvep_notch_coeff.Sec[0].b0 = 1.0f;
    s_ssvep_notch_coeff.Sec[0].b1 = -2.0f * cw;
    s_ssvep_notch_coeff.Sec[0].b2 = 1.0f;
    s_ssvep_notch_coeff.Sec[0].a1 = -2.0f * r * cw;
    s_ssvep_notch_coeff.Sec[0].a2 = r * r;
    s_ssvep_notch_coeff.Sec[1] = s_ssvep_notch_coeff.Sec[0];
    s_ssvep_notch_coeff.NumSections = 2;
}

static void ssvep_init_tukey(void)
{
    uint16_t i;
    float n1 = (float)(SSVEP_WINDOW_SIZE - 1u);
    float edge = SSVEP_TUKEY_ALPHA * n1 / 2.0f;
    float sum = 0.0f;
    for (i = 0; i < SSVEP_WINDOW_SIZE; i++) {
        float v = 1.0f;
        if ((float)i < edge) {
            v = 0.5f * (1.0f + cosf(SSVEP_PI * (2.0f * (float)i / (SSVEP_TUKEY_ALPHA * n1) - 1.0f)));
        } else if ((float)i >= n1 * (1.0f - SSVEP_TUKEY_ALPHA / 2.0f)) {
            v = 0.5f * (1.0f + cosf(SSVEP_PI * (2.0f * (float)i / (SSVEP_TUKEY_ALPHA * n1) - 2.0f / SSVEP_TUKEY_ALPHA + 1.0f)));
        }
        if (v < 1e-8f) v = 1e-8f;
        s_ssvep_tukey[i] = v;
        sum += v;
    }
    float mean = sum / (float)SSVEP_WINDOW_SIZE;
    for (i = 0; i < SSVEP_WINDOW_SIZE; i++) s_ssvep_tukey[i] /= mean;
}

static void ssvep_init_weights(void)
{
    uint8_t b;
    for (b = 0; b < SSVEP_NUM_BANKS; b++) {
        float m = (float)(b + 1u);
        s_ssvep_fbcca_weights[b] = powf(m, -1.25f) + 0.25f;
    }
}

static uint8_t ssvep_is_channel_usable(const float *x, uint16_t n)
{
    float sum = 0.0f, vmin = 1e30f, vmax = -1e30f, var = 0.0f;
    uint16_t i;
    if (n < 10) return 0;
    for (i = 0; i < n; i++) {
        if (!isfinite(x[i])) return 0;
        sum += x[i];
        if (x[i] < vmin) vmin = x[i];
        if (x[i] > vmax) vmax = x[i];
    }
    float mean = sum / (float)n;
    for (i = 0; i < n; i++) {
        float d = x[i] - mean;
        var += d * d;
    }
    float std = sqrtf(var / (float)n);
    float ptp = vmax - vmin;
    return (std >= SSVEP_CHANNEL_MIN_STD_MV && std <= SSVEP_CHANNEL_MAX_STD_MV && ptp <= SSVEP_CHANNEL_MAX_PTP_MV) ? 1u : 0u;
}

static void ssvep_remove_mean(float *x, uint16_t n)
{
    float sum = 0.0f;
    uint16_t i;
    for (i = 0; i < n; i++) sum += x[i];
    float mean = sum / (float)n;
    for (i = 0; i < n; i++) x[i] -= mean;
}

static void ssvep_normalize(float *x, uint16_t n)
{
    float sum = 0.0f;
    uint16_t i;
    ssvep_remove_mean(x, n);
    for (i = 0; i < n; i++) sum += x[i] * x[i];
    float std = sqrtf(sum / (float)n) + 1e-8f;
    for (i = 0; i < n; i++) x[i] /= std;
}

static float ssvep_canonical_corr_2ch(const float *x0, const float *x1,
                                       const float *y_rows[], uint8_t q,
                                       uint16_t n)
{
    float ws = 0.0f;
    uint16_t i;
    uint8_t r, c;
    for (i = 0; i < n; i++) ws += s_ssvep_tukey[i];
    float inv_ws = 1.0f / ws;

    float mx[2] = {0.0f, 0.0f};
    float my[8];
    for (r = 0; r < q; r++) my[r] = 0.0f;
    for (i = 0; i < n; i++) {
        float w = s_ssvep_tukey[i] * inv_ws;
        mx[0] += w * x0[i];
        mx[1] += w * x1[i];
        for (r = 0; r < q; r++) my[r] += w * y_rows[r][i];
    }

    float cxx[2][2] = {{0}}, cxy[2][8] = {{0}}, cyy[8][8] = {{0}};
    for (i = 0; i < n; i++) {
        float w = s_ssvep_tukey[i] * inv_ws;
        float dx0 = x0[i] - mx[0];
        float dx1 = x1[i] - mx[1];
        float dy[8];
        for (r = 0; r < q; r++) dy[r] = y_rows[r][i] - my[r];
        cxx[0][0] += w * dx0 * dx0;
        cxx[0][1] += w * dx0 * dx1;
        cxx[1][1] += w * dx1 * dx1;
        for (r = 0; r < q; r++) {
            cxy[0][r] += w * dx0 * dy[r];
            cxy[1][r] += w * dx1 * dy[r];
            for (c = r; c < q; c++) cyy[r][c] += w * dy[r] * dy[c];
        }
    }
    cxx[1][0] = cxx[0][1];
    cxx[0][0] += SSVEP_REG;
    cxx[1][1] += SSVEP_REG;
    for (r = 0; r < q; r++) {
        for (c = 0; c < r; c++) cyy[r][c] = cyy[c][r];
        cyy[r][r] += SSVEP_REG;
    }

    float det_xx = cxx[0][0] * cxx[1][1] - cxx[0][1] * cxx[1][0];
    if (fabsf(det_xx) < 1e-20f) return 0.0f;
    float inv_xx[2][2];
    inv_xx[0][0] = cxx[1][1] / det_xx;
    inv_xx[0][1] = -cxx[0][1] / det_xx;
    inv_xx[1][0] = -cxx[1][0] / det_xx;
    inv_xx[1][1] = cxx[0][0] / det_xx;

    float inv_yy[8][8];
    {
        float a[8][16];
        uint8_t k;
        memset(a, 0, sizeof(a));
        for (r = 0; r < q; r++) {
            for (c = 0; c < q; c++) a[r][c] = cyy[r][c];
            a[r][q + r] = 1.0f;
        }
        for (c = 0; c < q; c++) {
            uint8_t piv = c;
            for (r = c + 1; r < q; r++) if (fabsf(a[r][c]) > fabsf(a[piv][c])) piv = r;
            if (fabsf(a[piv][c]) < 1e-12f) return 0.0f;
            if (piv != c) {
                for (k = 0; k < 2 * q; k++) {
                    float tmp = a[piv][k]; a[piv][k] = a[c][k]; a[c][k] = tmp;
                }
            }
            float div = a[c][c];
            for (k = 0; k < 2 * q; k++) a[c][k] /= div;
            for (r = 0; r < q; r++) {
                if (r == c) continue;
                float fac = a[r][c];
                if (fac == 0.0f) continue;
                for (k = 0; k < 2 * q; k++) a[r][k] -= fac * a[c][k];
            }
        }
        for (r = 0; r < q; r++) for (c = 0; c < q; c++) inv_yy[r][c] = a[r][q + c];
    }

    float tmp[2][8];
    for (r = 0; r < 2; r++) for (c = 0; c < q; c++) {
        float s = 0.0f;
        for (uint8_t k = 0; k < 2; k++) s += inv_xx[r][k] * cxy[k][c];
        tmp[r][c] = s;
    }

    float yyi_xyT[8][2];
    for (r = 0; r < q; r++) for (c = 0; c < 2; c++) {
        float s = 0.0f;
        for (uint8_t k = 0; k < q; k++) s += inv_yy[r][k] * cxy[c][k];
        yyi_xyT[r][c] = s;
    }

    float M[2][2] = {{0}};
    for (r = 0; r < 2; r++) for (c = 0; c < 2; c++) {
        float s = 0.0f;
        for (uint8_t k = 0; k < q; k++) s += tmp[r][k] * yyi_xyT[k][c];
        M[r][c] = s;
    }

    float trace = M[0][0] + M[1][1];
    float det = M[0][0] * M[1][1] - M[0][1] * M[1][0];
    float disc = trace * trace - 4.0f * det;
    if (disc < 0.0f) disc = 0.0f;
    float max_eig = (trace + sqrtf(disc)) / 2.0f;
    if (max_eig < 0.0f) max_eig = 0.0f;
    if (max_eig > 1.0f) max_eig = 1.0f;
    return sqrtf(max_eig);
}

static float ssvep_canonical_corr_1ch(const float *x,
                                       const float *y_rows[], uint8_t q,
                                       uint16_t n)
{
    float ws = 0.0f;
    uint16_t i;
    uint8_t r, c;
    for (i = 0; i < n; i++) ws += s_ssvep_tukey[i];
    float inv_ws = 1.0f / ws;

    float mx = 0.0f;
    float my[8];
    for (r = 0; r < q; r++) my[r] = 0.0f;
    for (i = 0; i < n; i++) {
        float w = s_ssvep_tukey[i] * inv_ws;
        mx += w * x[i];
        for (r = 0; r < q; r++) my[r] += w * y_rows[r][i];
    }

    float cxx = 0.0f;
    float cxy[8] = {0};
    float cyy[8][8] = {{0}};
    for (i = 0; i < n; i++) {
        float w = s_ssvep_tukey[i] * inv_ws;
        float dx = x[i] - mx;
        cxx += w * dx * dx;
        for (r = 0; r < q; r++) {
            float dyr = y_rows[r][i] - my[r];
            cxy[r] += w * dx * dyr;
            for (c = r; c < q; c++) cyy[r][c] += w * dyr * (y_rows[c][i] - my[c]);
        }
    }
    for (r = 0; r < q; r++) {
        for (c = 0; c < r; c++) cyy[r][c] = cyy[c][r];
        cyy[r][r] += SSVEP_REG;
    }
    cxx += SSVEP_REG;
    if (cxx <= 0.0f) return 0.0f;

    float inv_yy[8][8];
    {
        float a[8][16];
        uint8_t k;
        memset(a, 0, sizeof(a));
        for (r = 0; r < q; r++) {
            for (c = 0; c < q; c++) a[r][c] = cyy[r][c];
            a[r][q + r] = 1.0f;
        }
        for (c = 0; c < q; c++) {
            uint8_t piv = c;
            for (r = c + 1; r < q; r++) if (fabsf(a[r][c]) > fabsf(a[piv][c])) piv = r;
            if (fabsf(a[piv][c]) < 1e-12f) return 0.0f;
            if (piv != c) {
                for (k = 0; k < 2 * q; k++) {
                    float tmp = a[piv][k]; a[piv][k] = a[c][k]; a[c][k] = tmp;
                }
            }
            float div = a[c][c];
            for (k = 0; k < 2 * q; k++) a[c][k] /= div;
            for (r = 0; r < q; r++) {
                if (r == c) continue;
                float fac = a[r][c];
                if (fac == 0.0f) continue;
                for (k = 0; k < 2 * q; k++) a[r][k] -= fac * a[c][k];
            }
        }
        for (r = 0; r < q; r++) for (c = 0; c < q; c++) inv_yy[r][c] = a[r][q + c];
    }

    float val = 0.0f;
    for (r = 0; r < q; r++) {
        float tmp = 0.0f;
        for (c = 0; c < q; c++) tmp += inv_yy[r][c] * cxy[c];
        val += cxy[r] * tmp;
    }
    val /= cxx;
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    return sqrtf(val);
}

static void ssvep_generate_refs(float freq, float low, float high,
                                 float refs[][SSVEP_WINDOW_SIZE],
                                 uint8_t *out_rows)
{
    uint8_t row = 0;
    float nyq = SSVEP_FS / 2.0f;
    uint8_t h;
    uint16_t i;
    for (h = 1; h <= SSVEP_HARMONICS; h++) {
        float hf = (float)h * freq;
        if (hf < low || hf > high || hf >= nyq) continue;
        float delta = 2.0f * SSVEP_PI * hf / SSVEP_FS;
        float cd = cosf(delta);
        float sd = sinf(delta);
        float s_val = 0.0f;
        float c_val = 1.0f;
        refs[row][0] = s_val;
        refs[row + 1u][0] = c_val;
        for (i = 1; i < SSVEP_WINDOW_SIZE; i++) {
            float s_next = s_val * cd + c_val * sd;
            float c_next = c_val * cd - s_val * sd;
            refs[row][i] = s_next;
            refs[row + 1u][i] = c_next;
            s_val = s_next;
            c_val = c_next;
        }
        row += 2u;
    }
    *out_rows = row;
}


static __attribute__((aligned(4))) float s_a_win[SSVEP_NUM_CH][SSVEP_WINDOW_SIZE];
static __attribute__((aligned(4))) float s_a_filt_win[SSVEP_NUM_BANKS][SSVEP_NUM_CH][SSVEP_WINDOW_SIZE];
static float s_a_scores[SSVEP_NUM_TARGETS];
static __attribute__((aligned(4))) float s_a_ref_buf[8][SSVEP_WINDOW_SIZE];
static const float *s_a_y_rows[8];
static uint8_t s_a_usable[SSVEP_NUM_CH];
static uint8_t s_a_usable_count;
static float s_a_best_total[SSVEP_NUM_TARGETS];
static uint8_t s_analyze_step = 0;

static void ssvep_analyze_init(void)
{
    uint8_t ch, b;
    uint16_t i;

    g_ipc_ssvep_sequence++;
    s_a_usable_count = 0;

    uint16_t start = (s_ssvep_ring_count >= SSVEP_WINDOW_SIZE) ? s_ssvep_ring_write : 0;
    for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
        for (i = 0; i < SSVEP_WINDOW_SIZE; i++) {
            s_a_win[ch][i] = s_ssvep_ring[ch][(start + i) % SSVEP_WINDOW_SIZE];
        }
        s_a_usable[ch] = ssvep_is_channel_usable(s_a_win[ch], SSVEP_WINDOW_SIZE);
        if (s_a_usable[ch]) s_a_usable_count++;
    }
    if (s_a_usable_count == 0) {
        g_ipc_ssvep_raw_index = -1;
        g_ipc_ssvep_valid = 1;
        s_analyze_step = 0;
        return;
    }

    for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
        if (!s_a_usable[ch]) continue;
        ssvep_remove_mean(s_a_win[ch], SSVEP_WINDOW_SIZE);
    }

    for (b = 0; b < SSVEP_NUM_BANKS; b++) {
        for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
            if (!s_a_usable[ch]) continue;
            for (i = 0; i < SSVEP_WINDOW_SIZE; i++) {
                s_a_filt_win[b][ch][i] = s_ssvep_filt_ring[b][ch][(start + i) % SSVEP_WINDOW_SIZE];
            }
            ssvep_normalize(s_a_filt_win[b][ch], SSVEP_WINDOW_SIZE);
        }
    }

    for (ch = 0; ch < SSVEP_NUM_TARGETS; ch++) {
        s_a_best_total[ch] = -1.0f;
        s_a_scores[ch] = 0.0f;
    }
    s_analyze_step = 2;
}

static void ssvep_analyze_step(uint8_t step_idx)
{
    uint8_t t = step_idx / 3u;
    uint8_t h_idx = step_idx % 3u;
    uint8_t b;

    float target = s_ssvep_target_freqs[t];
    float ref_freq = target + s_ssvep_freq_offsets[h_idx];
    if (ref_freq <= 0.0f) {
        s_analyze_step++;
        return;
    }
    float total = 0.0f;
    for (b = 0; b < SSVEP_NUM_BANKS; b++) {
        float low = s_ssvep_bank_ranges[b][0];
        float high = s_ssvep_bank_ranges[b][1];
        uint8_t q;
        ssvep_generate_refs(ref_freq, low, high, s_a_ref_buf, &q);
        if (q == 0) continue;

        for (uint8_t rr = 0; rr < q; rr++) s_a_y_rows[rr] = s_a_ref_buf[rr];

        float rho = 0.0f;
        if (s_a_usable_count >= 2 && s_a_usable[0] && s_a_usable[1]) {
            rho = ssvep_canonical_corr_2ch(
                s_a_filt_win[b][0], s_a_filt_win[b][1], s_a_y_rows, q, SSVEP_WINDOW_SIZE);
        } else if (s_a_usable_count == 1) {
            uint8_t single_ch = s_a_usable[0] ? 0 : 1;
            rho = ssvep_canonical_corr_1ch(
                s_a_filt_win[b][single_ch], s_a_y_rows, q, SSVEP_WINDOW_SIZE);
        }
        float bank_score = rho * rho;
        total += s_ssvep_fbcca_weights[b] * bank_score;
    }
    if (total > s_a_best_total[t]) s_a_best_total[t] = total;
    s_analyze_step++;
}

static void ssvep_analyze_finalize(void)
{
    uint8_t t;
    int8_t best = -1, second = -1;

    for (t = 0; t < SSVEP_NUM_TARGETS; t++) {
        s_a_scores[t] = (s_a_best_total[t] > 0.0f) ? s_a_best_total[t] : 0.0f;
    }
    for (t = 0; t < (int8_t)SSVEP_NUM_TARGETS; t++) {
        if (best < 0 || s_a_scores[t] > s_a_scores[best]) {
            second = best;
            best = (int8_t)t;
        } else if (second < 0 || s_a_scores[t] > s_a_scores[second]) {
            second = (int8_t)t;
        }
    }

    float best_score = (best >= 0) ? s_a_scores[best] : 0.0f;
    float second_score = (second >= 0) ? s_a_scores[second] : 0.0f;
    float ratio = (second >= 0) ? best_score / (second_score + 1e-12f) : 999.0f;
    if (ratio > 200000.0f) ratio = 200000.0f;
    float margin = best_score - second_score;

    int8_t raw_idx;
    if (best >= 0 && best_score >= SSVEP_MIN_SCORE
        && ratio >= SSVEP_RATIO_THRESHOLD
        && margin >= SSVEP_MARGIN_THRESHOLD) {
        raw_idx = best;
    } else {
        raw_idx = -1;
    }

    g_ipc_ssvep_raw_index = raw_idx;
    g_ipc_ssvep_ratio_q10000 = (int32_t)(ratio * 10000.0f + 0.5f);
    if (g_ipc_ssvep_ratio_q10000 > 2000000000L) g_ipc_ssvep_ratio_q10000 = 2000000000L;
    g_ipc_ssvep_best_score_q10000 = (int32_t)(best_score * 10000.0f + 0.5f);
    g_ipc_ssvep_margin_q10000 = (int32_t)(margin * 10000.0f + 0.5f);
    for (t = 0; t < SSVEP_NUM_TARGETS; t++) {
        g_ipc_ssvep_scores_q10000[t] = (int32_t)(s_a_scores[t] * 10000.0f + 0.5f);
    }
    g_ipc_ssvep_valid = 1;
    s_analyze_step = 0;
}

void DualCore_V5F_SSVEP_PushSample(float o1_mv, float oz_mv)
{
    uint8_t b, ch;
    float samples[SSVEP_NUM_CH] = {oz_mv, o1_mv};

    for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
        float notched = DualCore_IIR_SOS_Step(samples[ch], &s_ssvep_notch_coeff, &s_ssvep_notch_state[ch]);
        s_ssvep_ring[ch][s_ssvep_ring_write] = notched;
        for (b = 0; b < SSVEP_NUM_BANKS; b++) {
            s_ssvep_filt_ring[b][ch][s_ssvep_ring_write] =
                DualCore_IIR_SOS_Step(notched, &s_ssvep_bank_coeff[b], &s_ssvep_bank_state[b][ch]);
        }
    }

    s_ssvep_ring_write = (s_ssvep_ring_write + 1u) % SSVEP_WINDOW_SIZE;
    if (s_ssvep_ring_count < SSVEP_WINDOW_SIZE) s_ssvep_ring_count++;
    s_ssvep_since_last++;

    if (s_ssvep_ring_count >= SSVEP_WINDOW_SIZE && s_ssvep_since_last >= SSVEP_STEP_SIZE) {
        s_ssvep_since_last = 0;
        g_ssvep_analysis_pending = 1;
    }
}

void DualCore_V5F_SSVEP_RunPending(void)
{
    if (g_ssvep_analysis_pending) {
        g_ssvep_analysis_pending = 0;
        s_analyze_step = 1;
    }
    if (s_analyze_step == 1) {
        ssvep_analyze_init();
    } else if (s_analyze_step >= 2 && s_analyze_step <= 13) {
        ssvep_analyze_step(s_analyze_step - 2u);
    } else if (s_analyze_step == 14) {
        ssvep_analyze_finalize();
    }
}

void DualCore_V5F_SSVEP_Init(void)
{
    ssvep_init_notch();
    ssvep_init_tukey();
    ssvep_init_weights();
    DualCore_V5F_SSVEP_Reset();
}

void DualCore_V5F_SSVEP_Reset(void)
{
    uint8_t b, ch;
    uint16_t i;

    for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
        memset(&s_ssvep_notch_state[ch], 0, sizeof(DualCore_IIR_SOS_State_t));
        for (b = 0; b < SSVEP_NUM_BANKS; b++) {
            memset(&s_ssvep_bank_state[b][ch], 0, sizeof(DualCore_IIR_SOS_State_t));
        }
    }
    for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
        for (i = 0; i < SSVEP_WINDOW_SIZE; i++) s_ssvep_ring[ch][i] = 0.0f;
    }
    for (b = 0; b < SSVEP_NUM_BANKS; b++) {
        for (ch = 0; ch < SSVEP_NUM_CH; ch++) {
            for (i = 0; i < SSVEP_WINDOW_SIZE; i++) s_ssvep_filt_ring[b][ch][i] = 0.0f;
        }
    }
    s_ssvep_ring_write = 0;
    s_ssvep_ring_count = 0;
    s_ssvep_since_last = 0;
    g_ipc_ssvep_valid = 0;
    g_ipc_ssvep_raw_index = -1;
    g_ipc_ssvep_ratio_q10000 = 0;
    g_ipc_ssvep_best_score_q10000 = 0;
    g_ipc_ssvep_margin_q10000 = 0;
    for (i = 0; i < SSVEP_NUM_TARGETS; i++) g_ipc_ssvep_scores_q10000[i] = 0;
    g_ipc_ssvep_sequence = 0;
}

#endif