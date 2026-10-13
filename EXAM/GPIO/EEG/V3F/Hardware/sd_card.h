#ifndef __SD_CARD_H
#define __SD_CARD_H

#include <stdint.h>

#define SD_SECTOR_SIZE      512
#define SD_INIT_OK          0
#define SD_INIT_NO_CARD    -1
#define SD_INIT_ERROR      -2
#define SD_CRC_ERROR       -3

typedef enum {
    SD_TYPE_UNKNOWN = 0,
    SD_TYPE_V1      = 1,
    SD_TYPE_V2      = 2,
    SD_TYPE_SDHC    = 4
} SD_CardType_t;

int  SD_Init(void);
int  SD_IsCardPresent(void);
SD_CardType_t SD_GetType(void);
int  SD_ReadSector(uint32_t sector, uint8_t *buf);
int  SD_WriteSector(uint32_t sector, const uint8_t *buf);
int  SD_ReadMultiSector(uint32_t sector, uint8_t *buf, uint32_t count);
int  SD_WriteMultiSector(uint32_t sector, const uint8_t *buf, uint32_t count);
int  SD_SelfTest(void);

#endif /* __SD_CARD_H */