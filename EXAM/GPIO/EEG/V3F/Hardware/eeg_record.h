#ifndef __EEG_RECORD_H
#define __EEG_RECORD_H

#include <stdint.h>

int  eeg_record_start(void);
void eeg_record_stop(void);
void eeg_record_add_frame(const uint8_t *frame_27);
void eeg_record_poll(void);
int  eeg_record_is_active(void);
void eeg_record_set_patient(const char *id, const char *name);

#endif