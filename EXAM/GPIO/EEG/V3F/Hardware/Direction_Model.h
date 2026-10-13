#ifndef __DIRECTION_MODEL_H
#define __DIRECTION_MODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DIR_CLASS_NUM 2

typedef enum {
    DIRECTION_LEFT = 0,
    DIRECTION_RIGHT = 1,
    DIRECTION_UNKNOWN = 2
} Direction_t;

/* 兼容旧接口：Signal_Analysis.c 仍然调用 Direction_Model_Infer()。
 * 内部实际由 Direction_Classifier.c 执行 LDA / Linear SVM / 小 MLP 推理。
 */
Direction_t Direction_Model_Infer(const float feature[24], int32_t score[DIR_CLASS_NUM]);
const char *Direction_ToString(Direction_t dir);
uint8_t Direction_Model_IsTrained(void);
const char *Direction_Model_Name(void);

#ifdef __cplusplus
}
#endif

#endif /* __DIRECTION_MODEL_H */
