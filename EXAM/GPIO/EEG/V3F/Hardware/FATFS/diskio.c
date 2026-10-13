#include "ff.h"
#include "diskio.h"
#include "sd_card.h"

#define DEV_SD      0

static DSTATUS s_sd_stat = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != DEV_SD) return STA_NOINIT;
    return s_sd_stat;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != DEV_SD) return STA_NOINIT;
    if (SD_Init() == 0) {
        s_sd_stat &= ~STA_NOINIT;
    }
    return s_sd_stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != DEV_SD) return RES_PARERR;
    if (s_sd_stat & STA_NOINIT) return RES_NOTRDY;
    if (count == 0) return RES_PARERR;

    if (SD_ReadMultiSector((uint32_t)sector, buff, (uint32_t)count) == 0) {
        return RES_OK;
    }
    return RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != DEV_SD) return RES_PARERR;
    if (s_sd_stat & STA_NOINIT) return RES_NOTRDY;
    if (count == 0) return RES_PARERR;

    if (SD_WriteMultiSector((uint32_t)sector, buff, (uint32_t)count) == 0) {
        return RES_OK;
    }
    return RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != DEV_SD) return RES_PARERR;
    if (s_sd_stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD*)buff = 67108864UL;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD*)buff = SD_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(WORD*)buff = 1;
        return RES_OK;
    }
    return RES_PARERR;
}

DWORD get_fattime(void)
{
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)8 << 21)
         | ((DWORD)6 << 16)
         | ((DWORD)12 << 11)
         | ((DWORD)0 << 5)
         | ((DWORD)0);
}