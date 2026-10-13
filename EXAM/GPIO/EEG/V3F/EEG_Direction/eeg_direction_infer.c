#include "eeg_direction_infer.h"

#if V3F_LOCAL_INFER_ENABLE

#include "signal_analysis.h"
#include "eeg_direction_collect.h"
#include "Direction_Model.h"
#include "Serial.h"
#include <stdio.h>
#include <string.h>

extern uint8_t g_eeg_app_mode;
extern uint8_t g_paused;

static float dir_decision_hist[DIR_DECISION_ROWS][DIR_FEAT_DIM_LOCAL] = {{0.0f}};
static uint8_t dir_decision_write_idx = 0;
static uint8_t dir_decision_count = 0;
static uint8_t dir_result_row_tick = 0;
static uint32_t dir_result_window = 0;

void Direction_Infer1sReset(void)
{
    for (int r = 0; r < DIR_DECISION_ROWS; r++) {
        for (int i = 0; i < DIR_FEAT_DIM_LOCAL; i++) {
            dir_decision_hist[r][i] = 0.0f;
        }
    }
    dir_decision_write_idx = 0;
    dir_decision_count = 0;
    dir_result_row_tick = 0;
    dir_result_window = 0;
}

void Direction_InferAndPrint(float theta_pow[NUM_CHANNELS],
                             float alpha_pow[NUM_CHANNELS],
                             float beta_pow[NUM_CHANNELS])
{
    if (g_eeg_app_mode != EEG_APP_MODE_INFER) {
        return;
    }
    float feature[DIR_FEAT_DIM_LOCAL];
    int32_t score[DIR_CLASS_NUM];
    Direction_t dir;

    Direction_BuildFeature(feature, theta_pow, alpha_pow, beta_pow);
    dir = Direction_Model_Infer(feature, score);

    Serial_Printf(DIR_TEXT_PORT, "INTENT=%s,S_LEFT=%ld,S_RIGHT=%ld,trained=%d\r\n",
                  Direction_ToString(dir), (long)score[0], (long)score[1], Direction_Model_IsTrained());
}

void Direction_Infer1sUpdateAndPrint(float theta_pow[NUM_CHANNELS],
                                     float alpha_pow[NUM_CHANNELS],
                                     float beta_pow[NUM_CHANNELS])
{
    if (g_paused) return;
    if (g_eeg_app_mode != EEG_APP_MODE_INFER) {
        return;
    }

    float feature[DIR_FEAT_DIM_LOCAL];
    float avg_feature[DIR_FEAT_DIM_LOCAL];
    int32_t score[DIR_CLASS_NUM];
    Direction_t dir;
    int32_t conf;

    Direction_BuildFeature(feature, theta_pow, alpha_pow, beta_pow);

    for (int i = 0; i < DIR_FEAT_DIM_LOCAL; i++) {
        dir_decision_hist[dir_decision_write_idx][i] = feature[i];
    }
    dir_decision_write_idx = (uint8_t)((dir_decision_write_idx + 1) % DIR_DECISION_ROWS);

    if (dir_decision_count < DIR_DECISION_ROWS) {
        dir_decision_count++;
    }

    dir_result_row_tick++;
    if (dir_decision_count < DIR_DECISION_ROWS) {
        return;
    }
    if (dir_result_row_tick < DIR_RESULT_EVERY_ROWS) {
        return;
    }
    dir_result_row_tick = 0;

    for (int i = 0; i < DIR_FEAT_DIM_LOCAL; i++) {
        float sum = 0.0f;
        for (int r = 0; r < DIR_DECISION_ROWS; r++) {
            sum += dir_decision_hist[r][i];
        }
        avg_feature[i] = sum / (float)DIR_DECISION_ROWS;
    }

    dir = Direction_Model_Infer(avg_feature, score);
    conf = score[0] - score[1];
    if (conf < 0) conf = -conf;

    Serial_Printf(DIR_TEXT_PORT,
                  "RESULT,window=%lu,dt_ms=%u,win_rows=%u,INTENT=%s,S_LEFT=%ld,S_RIGHT=%ld,CONF=%ld,trained=%d\r\n",
                  (unsigned long)dir_result_window,
                  (unsigned int)DIR_RESULT_DT_MS,
                  (unsigned int)DIR_DECISION_ROWS,
                  Direction_ToString(dir),
                  (long)score[0],
                  (long)score[1],
                  (long)conf,
                  Direction_Model_IsTrained());

    dir_result_window++;
}

#endif /* V3F_LOCAL_INFER_ENABLE */
