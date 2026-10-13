#ifndef __DIRECTION_CLASSIFIER_H
#define __DIRECTION_CLASSIFIER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DIR_CLASSIFIER_INPUT_DIM 24
#define DIR_CLASSIFIER_CLASS_NUM 2

#define DIR_CLASSIFIER_MODEL_LDA        1
#define DIR_CLASSIFIER_MODEL_LINEAR_SVM 2
#define DIR_CLASSIFIER_MODEL_MLP        3

#ifndef DIR_CLASSIFIER_MODEL_TYPE
#define DIR_CLASSIFIER_MODEL_TYPE DIR_CLASSIFIER_MODEL_LINEAR_SVM
#endif

#ifndef DIR_CLASSIFIER_ENABLE_UNKNOWN
#define DIR_CLASSIFIER_ENABLE_UNKNOWN 0
#endif

#ifndef DIR_CLASSIFIER_CONF_THRESHOLD
#define DIR_CLASSIFIER_CONF_THRESHOLD 800
#endif

#ifndef DIR_CLASSIFIER_LINEAR_SCORE_GAIN
#define DIR_CLASSIFIER_LINEAR_SCORE_GAIN 1.0f
#endif

typedef enum {
    DIR_CLS_LEFT = 0,
    DIR_CLS_RIGHT = 1,
    DIR_CLS_UNKNOWN = 2
} DirectionClassifierIntent_t;

typedef struct {
    DirectionClassifierIntent_t intent;
    int32_t score_left;      /* 0~10000 */
    int32_t score_right;     /* 0~10000 */
    int32_t confidence;      /* abs(score_left-score_right) */
    uint8_t trained;
} DirectionClassifierResult_t;

DirectionClassifierIntent_t Direction_Classifier_Predict(const float feature[DIR_CLASSIFIER_INPUT_DIM],
                                                          DirectionClassifierResult_t *result);
const char *Direction_Classifier_ToString(DirectionClassifierIntent_t intent);
const char *Direction_Classifier_ModelName(void);
uint8_t Direction_Classifier_IsTrained(void);

#ifdef __cplusplus
}
#endif

#endif /* __DIRECTION_CLASSIFIER_H */
