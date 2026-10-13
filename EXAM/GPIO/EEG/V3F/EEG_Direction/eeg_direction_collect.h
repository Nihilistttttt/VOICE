#ifndef __EEG_DIRECTION_COLLECT_H
#define __EEG_DIRECTION_COLLECT_H

#include <stdint.h>
#include "eeg_direction_feature.h"
#include "eeg_fft.h"

#define DIR_LABEL_LEFT              2
#define DIR_LABEL_RIGHT             3

#define DIR_COLLECT_ROWS_PER_PHASE  20
#define DIR_REST_ROWS               10
#define DIR_SKIP_ROWS_AFTER_SWITCH  8

#define DIR_PHASE_REST              0
#define DIR_PHASE_LEFT              1
#define DIR_PHASE_RIGHT             2

#define CSP_WIN_SIZE                256
#define CSP_COLLECT_EVERY_ROWS      4
#define CSP_OUTPUT_SCALE_UVX1000    1000000000.0f
#define CSP_STREAM_ROWS_PER_FRAME    1

#define CMD_MODE_ENABLE             1

#if CMD_MODE_ENABLE
typedef enum {
    WORK_MODE_IDLE = 0,
    WORK_MODE_TRAIN,
    WORK_MODE_TEST
} WorkMode_t;

typedef enum {
    TRIAL_IDLE = 0,
    TRIAL_RUNNING,
    TRIAL_DONE
} TrialState_t;
#endif

void Direction_AutoCollectProcess(float theta_pow[NUM_CHANNELS],
                                  float alpha_pow[NUM_CHANNELS],
                                  float beta_pow[NUM_CHANNELS]);
void Direction_AutoCollectCSPProcess(RingBuffer_t *filt_buf);

/*
 * CSP窗口采用“先快照、后分片发送”的方式输出。
 * 每处理一帧ADS1299数据只发送少量文本行，避免一次输出256行堵塞V3F主循环。
 */
void Direction_CSPStreamTask(void);
void Direction_CSPStreamReset(void);
uint8_t Direction_CSPStreamBusy(void);

extern uint8_t dir_phase;
extern uint8_t dir_next_label;
extern uint16_t dir_phase_row_count;
extern uint16_t dir_skip_row_count;
extern uint16_t dir_round_count;
extern uint8_t csp_collect_row_tick;
extern uint32_t csp_window_id;
extern uint32_t csp_filtered_sample_count;

#if CMD_MODE_ENABLE
extern WorkMode_t g_work_mode;
extern TrialState_t g_trial_state;
extern uint8_t g_trial_label;
extern uint16_t g_trial_row_count;
extern uint8_t g_paused;
extern uint8_t g_skip_rows;
#endif

#define TRIAL_DURATION_ROWS 20

#endif