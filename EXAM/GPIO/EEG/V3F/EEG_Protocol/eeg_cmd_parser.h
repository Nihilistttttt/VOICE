#ifndef __EEG_CMD_PARSER_H
#define __EEG_CMD_PARSER_H

#include <stdint.h>

void Parse_CommandBinary(const uint8_t *payload, uint16_t len, const char *source);

void Retry_Tick(void);
void Retry_Store(const uint8_t *payload, uint16_t len, uint8_t cmd);
void Retry_StoreEx(const uint8_t *payload, uint16_t len, uint8_t cmd, uint8_t max_count);
uint8_t Retry_GetSeq(void);

#endif
