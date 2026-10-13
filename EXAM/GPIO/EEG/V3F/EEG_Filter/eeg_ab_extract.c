#include "eeg_ab_extract.h"
#include "signal_analysis.h"
#include "Serial.h"
#include <math.h>
#include <stdio.h>

#define AB_EXTRACT_PRINT_ENABLE     0
#define AB_POWER_SCALE              1000000000000000.0f
#define AB_PCT_SCALE                10000.0f
#define AB_DB_SCALE                 100.0f
#define AB_RATIO_SCALE              10000.0f
#define AB_REL_EPS                  1.0e-18f

static long AB_RoundToLong(float x)
{
    if (x >= 0.0f) return (long)(x + 0.5f);
    return (long)(x - 0.5f);
}

static long AB_LogRatioScaled(float a, float b) __attribute__((unused));
static long AB_LogRatioScaled(float a, float b)
{
    float aa = a;
    float bb = b;
    if (aa < AB_REL_EPS) aa = AB_REL_EPS;
    if (bb < AB_REL_EPS) bb = AB_REL_EPS;
    return AB_RoundToLong(logf(aa / bb) * AB_RATIO_SCALE);
}

static long AB_PowerToDb100(float p) __attribute__((unused));
static long AB_PowerToDb100(float p)
{
    p = Direction_SafePower(p);
    return AB_RoundToLong((10.0f * 0.4342944819f * logf(p)) * AB_DB_SCALE);
}

void AB_PrintAlphaBetaCSV(float theta_pow[NUM_CHANNELS],
                          float alpha_pow[NUM_CHANNELS],
                          float beta_pow[NUM_CHANNELS])
{
#if AB_EXTRACT_PRINT_ENABLE
    float a0 = Direction_SafePower(alpha_pow[0]);
    float a1 = Direction_SafePower(alpha_pow[1]);
    float b0 = Direction_SafePower(beta_pow[0]);
    float b1 = Direction_SafePower(beta_pow[1]);

    float t0 = Direction_SafePower(theta_pow[0] + alpha_pow[0] + beta_pow[0]);
    float t1 = Direction_SafePower(theta_pow[1] + alpha_pow[1] + beta_pow[1]);

    long a0_p15 = AB_RoundToLong(a0 * AB_POWER_SCALE);
    long a1_p15 = AB_RoundToLong(a1 * AB_POWER_SCALE);
    long b0_p15 = AB_RoundToLong(b0 * AB_POWER_SCALE);
    long b1_p15 = AB_RoundToLong(b1 * AB_POWER_SCALE);

    long a0_pct = AB_RoundToLong((a0 / t0) * AB_PCT_SCALE);
    long a1_pct = AB_RoundToLong((a1 / t1) * AB_PCT_SCALE);
    long b0_pct = AB_RoundToLong((b0 / t0) * AB_PCT_SCALE);
    long b1_pct = AB_RoundToLong((b1 / t1) * AB_PCT_SCALE);

    long a0_db = AB_PowerToDb100(a0);
    long a1_db = AB_PowerToDb100(a1);
    long b0_db = AB_PowerToDb100(b0);
    long b1_db = AB_PowerToDb100(b1);

    long alpha_lr = AB_LogRatioScaled(a0, a1);
    long beta_lr  = AB_LogRatioScaled(b0, b1);
    long ch0_beta_alpha = AB_LogRatioScaled(b0, a0);
    long ch1_beta_alpha = AB_LogRatioScaled(b1, a1);
    long total_lr = AB_LogRatioScaled(t0, t1);

    Serial_Printf(DIR_TEXT_PORT,
                  "ABCSV,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\r\n",
                  a0_p15, a1_p15, b0_p15, b1_p15,
                  a0_pct, a1_pct, b0_pct, b1_pct,
                  a0_db, a1_db, b0_db, b1_db,
                  alpha_lr, beta_lr, ch0_beta_alpha, ch1_beta_alpha, total_lr);
#endif
}