#ifndef __WAV_PLAYER_H
#define __WAV_PLAYER_H

#include <stdint.h>

#define WAV_PLAYER_BUF_SAMPLES   4096
#define WAV_PLAYER_HALF_SAMPLES  (WAV_PLAYER_BUF_SAMPLES / 2)

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_size;
    uint32_t data_offset;
} wav_header_t;

int  wav_player_play(const char *filename);
void wav_player_stop(void);
int  wav_player_is_playing(void);
void wav_player_poll(void);
void wav_player_set_volume(uint8_t vol_pct);
void wav_player_set_loop(int enable);

#endif