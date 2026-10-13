#ifndef __GLXSS_SD_FW_H
#define __GLXSS_SD_FW_H

#include <stdint.h>
#include "glxss_me.h"

#define GLXSS_FW_START_SECTOR   1
#define GLXSS_FW_SIZE_SECTORS   ((GLXSS_FW_MAX_SIZE + 511) / 512)

uint32_t glxss_sd_fw_read(uint32_t offset, uint8_t *buf, uint32_t len);
int glxss_sd_fw_burn(void);
uint32_t glxss_sd_fw_get_size(void);

#endif
