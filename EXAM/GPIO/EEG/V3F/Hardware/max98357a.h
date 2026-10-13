#ifndef __MAX98357A_H
#define __MAX98357A_H

#include <stdint.h>

#define MAX98357A_SAMPLE_RATE       48000
#define MAX98357A_TONE_BUF_SIZE     192

typedef enum {
    MAX98357A_GAIN_15DB_I2S     = 0,
    MAX98357A_GAIN_15DB_LEFT    = 1,
    MAX98357A_GAIN_15DB_RIGHT   = 2,
    MAX98357A_GAIN_12DB         = 3,
    MAX98357A_GAIN_9DB          = 4,
    MAX98357A_GAIN_6DB          = 5,
    MAX98357A_GAIN_3DB          = 6,
    MAX98357A_GAIN_0DB          = 7
} max98357a_gain_t;

typedef void (*max98357a_dma_cb_t)(int half);

int  max98357a_init(max98357a_gain_t gain);
void max98357a_shutdown(void);
void max98357a_wakeup(max98357a_gain_t gain);
void max98357a_stop(void);
int  max98357a_play_tone(uint32_t freq_hz, uint32_t duration_ms, uint16_t amplitude);
int  max98357a_play_buffer(const int16_t *buf, uint32_t len, uint32_t loop);
int  max98357a_play_stream(int16_t *buf, uint32_t total_samples, max98357a_dma_cb_t cb);
int  max98357a_selftest(void);
int  max98357a_is_playing(void);

#endif