#ifndef __EEG_DIRECTION_INFER_H
#define __EEG_DIRECTION_INFER_H

#include <stdint.h>
#include "eeg_direction_feature.h"

#ifndef V3F_LOCAL_INFER_ENABLE
#define V3F_LOCAL_INFER_ENABLE  0
#endif

#define DIR_DECISION_ROWS           4
#define DIR_RESULT_EVERY_ROWS       4
#define DIR_RESULT_DT_MS            2000

#if V3F_LOCAL_INFER_ENABLE
void Direction_InferAndPrint(float theta_pow[NUM_CHANNELS],
                             float alpha_pow[NUM_CHANNELS],
                             float beta_pow[NUM_CHANNELS]);
void Direction_Infer1sUpdateAndPrint(float theta_pow[NUM_CHANNELS],
                                     float alpha_pow[NUM_CHANNELS],
                                     float beta_pow[NUM_CHANNELS]);
void Direction_Infer1sReset(void);
#else
static inline void Direction_InferAndPrint(float theta_pow[NUM_CHANNELS],
                                           float alpha_pow[NUM_CHANNELS],
                                           float beta_pow[NUM_CHANNELS]) { (void)theta_pow; (void)alpha_pow; (void)beta_pow; }
static inline void Direction_Infer1sUpdateAndPrint(float theta_pow[NUM_CHANNELS],
                                                    float alpha_pow[NUM_CHANNELS],
                                                    float beta_pow[NUM_CHANNELS]) { (void)theta_pow; (void)alpha_pow; (void)beta_pow; }
static inline void Direction_Infer1sReset(void) {}
#endif

#endif
