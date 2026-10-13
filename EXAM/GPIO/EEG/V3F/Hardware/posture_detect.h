#ifndef __POSTURE_DETECT_H
#define __POSTURE_DETECT_H

#include <stdint.h>
#include "Config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POSTURE_UNKNOWN = 0,
    POSTURE_SUPINE,
    POSTURE_PRONE,
    POSTURE_LEFT_SIDE,
    POSTURE_RIGHT_SIDE,
    POSTURE_SITTING,
    POSTURE_COUNT
} Posture_t;

typedef struct {
    Posture_t posture;
    float gravity_x;
    float gravity_y;
    float gravity_z;
    float confidence;
    uint32_t stable_ms;
} PostureResult_t;

typedef struct {
    float lpf_alpha;
    float supine_z_min;
    float prone_z_max;
    float side_x_abs_min;
    float sitting_y_min;
    float confidence_threshold;
    uint32_t stable_confirm_ms;
} PostureConfig_t;

void Posture_Init(const PostureConfig_t *cfg);
void Posture_FeedAccel(float ax_g, float ay_g, float az_g, uint32_t dt_ms);
const PostureResult_t *Posture_GetResult(void);
const char *Posture_ToString(Posture_t p);

#ifdef __cplusplus
}
#endif

#endif