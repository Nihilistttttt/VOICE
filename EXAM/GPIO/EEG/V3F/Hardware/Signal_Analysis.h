#ifndef __SIGNAL_ANALYSIS_H
#define __SIGNAL_ANALYSIS_H

#include <stdint.h>
#include "Config.h"
#include "Serial.h"

#define EEG_APP_MODE_COLLECT        1
#define EEG_APP_MODE_INFER          2
#define EEG_APP_MODE_COLLECT_CSP    3

#define V5F_ACTIVE_IDLE             0
#define V5F_ACTIVE_COLLECT          1
#define V5F_ACTIVE_INFER            2

extern volatile uint8_t g_v5f_active;
extern volatile uint8_t g_ssvep_active;

#define DIR_TEXT_PORT               SERIAL_PORT_WIFI

void Signal_Analysis_Start(void);

#endif
