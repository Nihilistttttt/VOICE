#include "eeg_direction_feature.h"
#include "signal_analysis.h"
#include "algo_core.h"
#include "Serial.h"
#include "Message_Parser.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DIR_PRINT_HEADER_ON_START   0
#define DIR_FEAT_F_MIN              4
#define DIR_FEAT_F_MAX              30

extern uint8_t g_eeg_app_mode;

static uint16_t dir_rest_count = 0;
uint8_t dir_rest_valid = 0;

static uint16_t dir_task_zero_count = 0;
static uint8_t dir_task_zero_valid = 0;

float Direction_ClampFloat(float x, float min_v, float max_v)
{
    if (x < min_v) return min_v;
    if (x > max_v) return max_v;
    return x;
}

float Direction_SafePower(float p)
{
    if (p < DIR_ERD_POWER_EPS) return DIR_ERD_POWER_EPS;
    return p;
}

float Direction_LogRatio(float a, float b)
{
    float v = fast_logf(Direction_SafePower(a) / Direction_SafePower(b));
    return Direction_ClampFloat(v, -DIR_LOG_FEATURE_CLAMP, DIR_LOG_FEATURE_CLAMP);
}

void Direction_ResetRestBaseline(void)
{
    dir_rest_count = 0;
    dir_rest_valid = 1;
}

void Direction_ResetTaskZero(void)
{
    dir_task_zero_count = 0;
    dir_task_zero_valid = 1;
}

void Direction_UpdateRestBaseline(void)
{
    dir_rest_count++;
    dir_rest_valid = 1;
}

void Direction_UpdateTaskZero(void)
{
    dir_task_zero_count++;
    dir_task_zero_valid = 1;
}

void Direction_BuildFeature(float feature[DIR_FEAT_DIM_LOCAL],
                            float theta_pow[NUM_CHANNELS],
                            float alpha_pow[NUM_CHANNELS],
                            float beta_pow[NUM_CHANNELS])
{
    Direction_BuildRawFeature(feature, theta_pow, alpha_pow, beta_pow);
}

void Direction_BuildRawFeature(float feature[DIR_FEAT_DIM_LOCAL],
                               float theta_pow[NUM_CHANNELS],
                               float alpha_pow[NUM_CHANNELS],
                               float beta_pow[NUM_CHANNELS])
{
    float theta[NUM_CHANNELS];
    float mu[NUM_CHANNELS];
    float beta[NUM_CHANNELS];
    float total[NUM_CHANNELS];

    for (int i = 0; i < NUM_CHANNELS; i++) {
        theta[i] = Direction_SafePower(theta_pow[i]);
        mu[i] = Direction_SafePower(alpha_pow[i]);
        beta[i] = Direction_SafePower(beta_pow[i]);
    }

    for (int i = 0; i < NUM_CHANNELS; i++) {
        total[i] = Direction_SafePower(theta[i] + mu[i] + beta[i]);
    }

    feature[0] = Direction_LogRatio(mu[4],    mu[5])    * DIR_FEATURE_SCALE;
    feature[1] = Direction_LogRatio(beta[4],  beta[5])  * DIR_FEATURE_SCALE;
    feature[2] = Direction_LogRatio(mu[6],    mu[7])    * DIR_FEATURE_SCALE;
    feature[3] = Direction_LogRatio(beta[6],  beta[7])  * DIR_FEATURE_SCALE;
    feature[4] = Direction_LogRatio(theta[4], theta[5]) * DIR_FEATURE_SCALE;
    feature[5] = Direction_LogRatio(theta[6], theta[7]) * DIR_FEATURE_SCALE;
    feature[6] = Direction_LogRatio(total[4], total[5]) * DIR_FEATURE_SCALE;
    feature[7] = Direction_LogRatio(total[6], total[7]) * DIR_FEATURE_SCALE;

    feature[8]  = Direction_LogRatio(mu[4],   total[4]) * DIR_FEATURE_SCALE;
    feature[9]  = Direction_LogRatio(mu[5],   total[5]) * DIR_FEATURE_SCALE;
    feature[10] = Direction_LogRatio(mu[6],   total[6]) * DIR_FEATURE_SCALE;
    feature[11] = Direction_LogRatio(mu[7],   total[7]) * DIR_FEATURE_SCALE;
    feature[12] = Direction_LogRatio(beta[4], total[4]) * DIR_FEATURE_SCALE;
    feature[13] = Direction_LogRatio(beta[5], total[5]) * DIR_FEATURE_SCALE;
    feature[14] = Direction_LogRatio(beta[6], total[6]) * DIR_FEATURE_SCALE;
    feature[15] = Direction_LogRatio(beta[7], total[7]) * DIR_FEATURE_SCALE;
    feature[16] = Direction_LogRatio(beta[4], mu[4])    * DIR_FEATURE_SCALE;
    feature[17] = Direction_LogRatio(beta[5], mu[5])    * DIR_FEATURE_SCALE;
    feature[18] = Direction_LogRatio(beta[6], mu[6])    * DIR_FEATURE_SCALE;
    feature[19] = Direction_LogRatio(beta[7], mu[7])    * DIR_FEATURE_SCALE;

    feature[20] = Direction_LogRatio(mu[4],   mu[6])    * DIR_FEATURE_SCALE;
    feature[21] = Direction_LogRatio(mu[5],   mu[7])    * DIR_FEATURE_SCALE;
    feature[22] = Direction_LogRatio(beta[4], beta[6])  * DIR_FEATURE_SCALE;
    feature[23] = Direction_LogRatio(beta[5], beta[7])  * DIR_FEATURE_SCALE;
}


void Direction_PrintFeatureHeader(void)
{
    if (g_eeg_app_mode != EEG_APP_MODE_COLLECT) {
        return;
    }
#if DIR_PRINT_HEADER_ON_START
    Serial_Printf(DIR_TEXT_PORT, "DIRCSV,label,...\r\n");
    Serial_Printf(DIR_TEXT_PORT, "DIRCFG,...\r\n");
    Serial_Printf(DIR_TEXT_PORT, "TASK=REST,prepare,then left hand\r\n");
#endif
}

/*
 * DIRCSV 的公共输出函数。
 * V3F 原特征和 V5F 同源特征共用这一套格式化代码，
 * 避免重复引入一整套 snprintf 输出流程，减小 V3F 链接压力。
 */
static void Direction_PrintIntFeatureCSV(uint8_t label,
                                         const int32_t feature[DIR_FEAT_DIM_LOCAL])
{
    int i;

    if (feature == 0) {
        return;
    }

    {
        uint8_t payload[1 + DIR_FEAT_DIM_LOCAL * 4];
        payload[0] = label;
        for (i = 0; i < DIR_FEAT_DIM_LOCAL; i++) {
            memcpy(payload + 1 + i * 4, &feature[i], 4);
        }
        Pack_Frame(SERIAL_PORT_DEBUG, CMD_DIRCSV, payload, sizeof(payload));
        Pack_Frame(SERIAL_PORT_WIFI, CMD_DIRCSV, payload, sizeof(payload));
    }
}

void Direction_PrintFeatureCSV(uint8_t label,
                               float theta_pow[NUM_CHANNELS],
                               float alpha_pow[NUM_CHANNELS],
                               float beta_pow[NUM_CHANNELS])
{
    float feature_float[DIR_FEAT_DIM_LOCAL];
    int32_t feature_int[DIR_FEAT_DIM_LOCAL];
    int i;

    if (!dir_rest_valid) {
        return;
    }

    Direction_BuildFeature(feature_float, theta_pow, alpha_pow, beta_pow);

    for (i = 0; i < DIR_FEAT_DIM_LOCAL; i++) {
        feature_int[i] = (feature_float[i] >= 0.0f)
                       ? (int32_t)(feature_float[i] + 0.5f)
                       : (int32_t)(feature_float[i] - 0.5f);
    }

    Direction_PrintIntFeatureCSV(label, feature_int);
}

void Direction_PrintV5FFeatureCSV(uint8_t label,
                                  const int32_t feature[DIR_FEAT_DIM_LOCAL])
{
    /*
     * 串口协议仍为 DIRCSV,label,feat0...feat23。
     * 这里只更换特征来源，不改变上位机解析格式。
     */
    Direction_PrintIntFeatureCSV(label, feature);
}
