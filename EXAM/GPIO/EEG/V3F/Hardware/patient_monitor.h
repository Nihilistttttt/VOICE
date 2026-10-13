#ifndef __PATIENT_MONITOR_H
#define __PATIENT_MONITOR_H

#include <stdint.h>
#include "posture_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_EVENT_NONE       = 0,
    PM_EVENT_TURN       = 1,
    PM_EVENT_FALL       = 2,
    PM_EVENT_NO_TURN    = 3
} PM_Event_t;

typedef struct {
    float fall_gyro_dps;
    float fall_acc_g;
    uint32_t fall_cooldown_ms;
    uint32_t turn_min_stable_ms;
    uint32_t no_turn_alert_ms;
} PM_Config_t;

typedef struct {
    uint32_t turn_count;
    uint32_t last_turn_tick;
    uint32_t current_posture_tick;
    Posture_t turn_from;
    Posture_t turn_to;
    uint8_t  fall_detected;
    uint32_t fall_tick;
    float    fall_peak_gyro;
    float    fall_peak_acc;
    Posture_t fall_posture;
    uint8_t  no_turn_alert;
    uint32_t no_turn_duration_ms;
} PM_Result_t;

void PM_Init(const PM_Config_t *cfg);
PM_Event_t PM_Update(const PostureResult_t *posture,
                      float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
                      float acc_mag_g,
                      uint32_t tick_ms);
const PM_Result_t *PM_GetResult(void);
void PM_ClearFall(void);
void PM_ClearNoTurnAlert(void);

#ifdef __cplusplus
}
#endif

#endif