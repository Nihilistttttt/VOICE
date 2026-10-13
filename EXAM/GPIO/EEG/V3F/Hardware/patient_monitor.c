#include "patient_monitor.h"
#include <math.h>
#include <string.h>

#ifdef HAS_ICM42605

static PM_Config_t s_cfg;
static PM_Result_t s_result;

static Posture_t s_prev_stable_posture;
static uint32_t s_prev_stable_start;
static uint8_t  s_prev_stable_confirmed;
static uint32_t s_no_turn_base_tick;
static uint32_t s_last_update_tick;
static uint8_t  s_tick_ready;

typedef enum {
    FALL_STATE_IDLE = 0,
    FALL_STATE_GYRO_TRIGGERED,
    FALL_STATE_CONFIRMED,
    FALL_STATE_COOLDOWN
} FallState_t;

static FallState_t s_fall_state;
static uint32_t s_fall_state_tick;
static float    s_fall_peak_gyro_in_event;
static float    s_fall_peak_acc_in_event;
static Posture_t s_fall_posture_at_event;

static float PM_Sqrtf(float x)
{
    return sqrtf(x);
}


static PM_Event_t PM_ConfirmFall(const PostureResult_t *posture,
                                 uint32_t tick_ms)
{
    s_fall_state = FALL_STATE_CONFIRMED;
    s_fall_state_tick = tick_ms;

    s_result.fall_detected  = 1u;
    s_result.fall_tick      = tick_ms;
    s_result.fall_peak_gyro = s_fall_peak_gyro_in_event;
    s_result.fall_peak_acc  = s_fall_peak_acc_in_event;
    s_result.fall_posture   = posture->posture;

    return PM_EVENT_FALL;
}

void PM_Init(const PM_Config_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
    } else {
        s_cfg.fall_gyro_dps       = 250.0f;
        s_cfg.fall_acc_g          = 2.0f;
        s_cfg.fall_cooldown_ms    = 10000u;
        s_cfg.turn_min_stable_ms  = 3000u;
        s_cfg.no_turn_alert_ms    = 7200000u;
    }

    memset(&s_result, 0, sizeof(s_result));
    s_result.turn_from = POSTURE_UNKNOWN;
    s_result.turn_to   = POSTURE_UNKNOWN;
    s_result.fall_posture = POSTURE_UNKNOWN;

    s_prev_stable_posture    = POSTURE_UNKNOWN;
    s_prev_stable_start      = 0u;
    s_prev_stable_confirmed  = 0u;
    s_no_turn_base_tick      = 0u;
    s_last_update_tick       = 0u;
    s_tick_ready             = 0u;

    s_fall_state             = FALL_STATE_IDLE;
    s_fall_state_tick        = 0u;
    s_fall_peak_gyro_in_event = 0.0f;
    s_fall_peak_acc_in_event  = 0.0f;
    s_fall_posture_at_event   = POSTURE_UNKNOWN;
}

static PM_Event_t PM_DetectTurn(const PostureResult_t *posture, uint32_t tick_ms)
{
    Posture_t cur = posture->posture;

    if (cur == POSTURE_UNKNOWN) {
        return PM_EVENT_NONE;
    }

    if (cur != s_prev_stable_posture) {
        if (s_prev_stable_confirmed &&
            s_prev_stable_posture != POSTURE_UNKNOWN &&
            s_prev_stable_posture != POSTURE_SITTING) {

            uint32_t prev_duration = tick_ms - s_prev_stable_start;
            if (prev_duration >= s_cfg.turn_min_stable_ms &&
                cur != POSTURE_SITTING) {

                s_result.turn_count++;
                s_result.turn_from = s_prev_stable_posture;
                s_result.turn_to   = cur;
                s_result.last_turn_tick = tick_ms;
                s_result.no_turn_alert = 0u;
                s_result.no_turn_duration_ms = 0u;
                s_no_turn_base_tick = tick_ms;

                s_prev_stable_posture   = cur;
                s_prev_stable_start     = tick_ms;
                s_prev_stable_confirmed = 0u;

                return PM_EVENT_TURN;
            }
        }

        s_prev_stable_posture   = cur;
        s_prev_stable_start     = tick_ms;
        s_prev_stable_confirmed = 0u;
    } else {
        uint32_t stable_ms = tick_ms - s_prev_stable_start;
        if (stable_ms >= s_cfg.turn_min_stable_ms) {
            s_prev_stable_confirmed = 1u;
        }
    }

    s_result.current_posture_tick = tick_ms;
    return PM_EVENT_NONE;
}

static PM_Event_t PM_DetectFall(float gyro_mag, float acc_mag_g,
                                 const PostureResult_t *posture,
                                 uint32_t tick_ms)
{
    PM_Event_t event = PM_EVENT_NONE;

    switch (s_fall_state) {
    case FALL_STATE_IDLE:
        if (gyro_mag >= s_cfg.fall_gyro_dps) {
            s_fall_state = FALL_STATE_GYRO_TRIGGERED;
            s_fall_state_tick = tick_ms;
            s_fall_peak_gyro_in_event = gyro_mag;
            s_fall_peak_acc_in_event  = acc_mag_g;
            s_fall_posture_at_event   = posture->posture;
            if (acc_mag_g >= s_cfg.fall_acc_g) {
                event = PM_ConfirmFall(posture, tick_ms);
            }
        }
        break;

    case FALL_STATE_GYRO_TRIGGERED:
        if (gyro_mag > s_fall_peak_gyro_in_event) {
            s_fall_peak_gyro_in_event = gyro_mag;
        }
        if (acc_mag_g > s_fall_peak_acc_in_event) {
            s_fall_peak_acc_in_event = acc_mag_g;
        }

        if (acc_mag_g >= s_cfg.fall_acc_g) {
            event = PM_ConfirmFall(posture, tick_ms);
        } else if ((tick_ms - s_fall_state_tick) > 2000u) {
            s_fall_state = FALL_STATE_IDLE;
        }
        break;

    case FALL_STATE_CONFIRMED:
        if ((tick_ms - s_fall_state_tick) >= s_cfg.fall_cooldown_ms) {
            s_fall_state = FALL_STATE_COOLDOWN;
            s_fall_state_tick = tick_ms;
        }
        break;

    case FALL_STATE_COOLDOWN:
        if ((tick_ms - s_fall_state_tick) >= s_cfg.fall_cooldown_ms) {
            s_fall_state = FALL_STATE_IDLE;
            s_result.fall_detected = 0u;
        }
        break;
    }

    return event;
}

static PM_Event_t PM_CheckNoTurn(uint32_t tick_ms)
{
    if (!s_tick_ready) {
        return PM_EVENT_NONE;
    }

    uint32_t elapsed = tick_ms - s_no_turn_base_tick;
    s_result.no_turn_duration_ms = elapsed;

    if (elapsed >= s_cfg.no_turn_alert_ms) {
        if (s_result.no_turn_alert) {
            return PM_EVENT_NONE;
        }
        s_result.no_turn_alert = 1u;
        return PM_EVENT_NO_TURN;
    }

    s_result.no_turn_alert = 0u;
    return PM_EVENT_NONE;
}

PM_Event_t PM_Update(const PostureResult_t *posture,
                      float gyro_x_dps, float gyro_y_dps, float gyro_z_dps,
                      float acc_mag_g,
                      uint32_t tick_ms)
{
    PM_Event_t event = PM_EVENT_NONE;
    PM_Event_t ev;

    s_last_update_tick = tick_ms;
    if (!s_tick_ready) {
        s_no_turn_base_tick = tick_ms;
        s_tick_ready = 1u;
    }

    ev = PM_DetectTurn(posture, tick_ms);
    if (ev != PM_EVENT_NONE) event = ev;

    float gyro_mag = PM_Sqrtf(gyro_x_dps * gyro_x_dps +
                               gyro_y_dps * gyro_y_dps +
                               gyro_z_dps * gyro_z_dps);

    ev = PM_DetectFall(gyro_mag, acc_mag_g, posture, tick_ms);
    if (ev != PM_EVENT_NONE) event = ev;

    ev = PM_CheckNoTurn(tick_ms);
    if (ev != PM_EVENT_NONE && event == PM_EVENT_NONE) {
        event = ev;
    }

    return event;
}

const PM_Result_t *PM_GetResult(void)
{
    return &s_result;
}

void PM_ClearFall(void)
{
    s_result.fall_detected = 0u;
    /* Keep the event state in cooldown after reporting it. */
    if (s_fall_state == FALL_STATE_CONFIRMED) {
        s_fall_state = FALL_STATE_COOLDOWN;
    }
}

void PM_ClearNoTurnAlert(void)
{
    s_result.no_turn_alert = 0u;
    s_result.no_turn_duration_ms = 0u;
    s_no_turn_base_tick = s_last_update_tick;
}


#endif /* HAS_ICM42605 */