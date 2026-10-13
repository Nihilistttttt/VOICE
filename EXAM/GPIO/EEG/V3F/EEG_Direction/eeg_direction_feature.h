#ifndef __EEG_DIRECTION_FEATURE_H
#define __EEG_DIRECTION_FEATURE_H

#include <stdint.h>

#define DIR_FEAT_DIM_LOCAL          24
#define DIR_ERD_POWER_EPS           1.0e-18f
#define DIR_LOG_FEATURE_CLAMP       6.0f
#define DIR_FEATURE_SCALE           10000.0f

#ifndef NUM_CHANNELS
#define NUM_CHANNELS                8
#endif

float Direction_ClampFloat(float x, float min_v, float max_v);
float Direction_SafePower(float p);
float Direction_LogRatio(float a, float b);

void Direction_BuildRawFeature(float feature[DIR_FEAT_DIM_LOCAL],
                               float theta_pow[NUM_CHANNELS],
                               float alpha_pow[NUM_CHANNELS],
                               float beta_pow[NUM_CHANNELS]);
void Direction_BuildFeature(float feature[DIR_FEAT_DIM_LOCAL],
                            float theta_pow[NUM_CHANNELS],
                            float alpha_pow[NUM_CHANNELS],
                            float beta_pow[NUM_CHANNELS]);

void Direction_ResetRestBaseline(void);
void Direction_UpdateRestBaseline(void);
void Direction_ResetTaskZero(void);
void Direction_UpdateTaskZero(void);


void Direction_PrintFeatureHeader(void);
void Direction_PrintFeatureCSV(uint8_t label,
                               float theta_pow[NUM_CHANNELS],
                               float alpha_pow[NUM_CHANNELS],
                               float beta_pow[NUM_CHANNELS]);

/*
 * 按原有 DIRCSV 协议输出 V5F 实际用于推理的 24 维整数特征。
 * 串口格式保持不变，因此不会影响现有上位机显示与解析。
 */
void Direction_PrintV5FFeatureCSV(uint8_t label,
                                  const int32_t feature[DIR_FEAT_DIM_LOCAL]);

extern uint8_t dir_rest_valid;

#endif