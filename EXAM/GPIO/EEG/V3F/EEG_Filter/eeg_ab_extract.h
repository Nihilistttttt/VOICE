#ifndef __EEG_AB_EXTRACT_H
#define __EEG_AB_EXTRACT_H

#include <stdint.h>
#include "eeg_direction_feature.h"

void AB_PrintAlphaBetaCSV(float theta_pow[NUM_CHANNELS],
                          float alpha_pow[NUM_CHANNELS],
                          float beta_pow[NUM_CHANNELS]);

#endif