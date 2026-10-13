/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* 适配V307 SDIO SD卡驱动(sdio_sd.h):
 *   SD_ReadDisk/SD_WriteDisk(sector为512字节扇区号)
 *   SDCardInfo.Capacity/512 = 总扇区数
 * 上层main.c已先调SD_Init(), 故disk_status/initialize直接返回READY
 */

#include "ff.h"           /* Obtains integer types */
#include "diskio.h"       /* Declarations of disk functions */
#include "sdio_sd.h"

#define DEV_MMC     1   /* SD card on physical drive 1 (对应卷号"1:") */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
    BYTE pdrv       /* Physical drive nmuber to identify the drive */
)
{
    DSTATUS stat = 0;
    switch (pdrv) {
    case DEV_MMC:
        return stat;
    }
    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
    BYTE pdrv               /* Physical drive nmuber to identify the drive */
)
{
    DSTATUS stat = 0;
    switch (pdrv) {
    case DEV_MMC:
        return stat;
    }
    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
    BYTE pdrv,      /* Physical drive nmuber to identify the drive */
    BYTE *buff,     /* Data buffer to store read data */
    LBA_t sector,   /* Start sector in LBA */
    UINT count      /* Number of sectors to read */
)
{
    switch (pdrv) {
    case DEV_MMC:
        if (SD_ReadDisk(buff, sector, count) == SD_OK) {
            return RES_OK;
        }
        return RES_ERROR;
    }
    return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
    BYTE pdrv,          /* Physical drive nmuber to identify the drive */
    const BYTE *buff,   /* Data to be written */
    LBA_t sector,       /* Start sector in LBA */
    UINT count          /* Number of sectors to write */
)
{
    switch (pdrv) {
    case DEV_MMC:
        if (SD_WriteDisk((u8 *)buff, sector, count) == SD_OK) {
            return RES_OK;
        }
        return RES_ERROR;
    }
    return RES_PARERR;
}

#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
    BYTE pdrv,      /* Physical drive nmuber (0..) */
    BYTE cmd,       /* Control code */
    void *buff      /* Buffer to send/receive control data */
)
{
    switch (pdrv) {
    case DEV_MMC:
        switch (cmd) {
        case CTRL_SYNC:
            break;
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = SDCardInfo.CardCapacity / 512;
            break;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            break;
        case GET_BLOCK_SIZE:
            *(WORD *)buff = 1;
            break;
        case CTRL_TRIM:
            break;
        default:
            return RES_PARERR;
        }
        return RES_OK;
    }
    return RES_PARERR;
}

DWORD get_fattime (void)
{
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)10 << 21)
         | ((DWORD)1 << 16);
}
