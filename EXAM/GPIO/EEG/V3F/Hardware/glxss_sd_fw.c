#include "glxss_sd_fw.h"
#include "sd_card.h"
#include "Serial.h"
#include "FATFS/ff.h"
#include <string.h>

#define FW_FILENAME    "firmware.img"

static FATFS s_fs;
static FIL   s_fw_file;
static int   s_fw_opened = 0;

static int ensure_mounted(void) {
    static int mounted = 0;
    if (mounted) return 0;
    if (f_mount(&s_fs, "", 1) != FR_OK) return -1;
    mounted = 1;
    return 0;
}

uint32_t glxss_sd_fw_get_size(void)
{
    if (ensure_mounted() != 0) return 0;
    if (f_open(&s_fw_file, FW_FILENAME, FA_READ) != FR_OK) return 0;
    s_fw_opened = 1;
    uint32_t size = f_size(&s_fw_file);
    if (size == 0 || size > GLXSS_FW_MAX_SIZE) {
        f_close(&s_fw_file);
        s_fw_opened = 0;
        return 0;
    }
    return size;
}

uint32_t glxss_sd_fw_read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (!s_fw_opened) return 0;
    if (f_lseek(&s_fw_file, offset) != FR_OK) return 0;
    UINT br = 0;
    if (f_read(&s_fw_file, buf, len, &br) != FR_OK) return 0;
    return br;
}

int glxss_sd_fw_burn(void)
{
    uint8_t sec_buf[SD_SECTOR_SIZE];
    uint16_t idx;
    uint32_t total_data_bytes;

    if (ensure_mounted() != 0) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[BURN] f_mount FAIL, try f_mkfs...\r\n");
        BYTE work[4096];
        if (f_mkfs("", 0, work, sizeof(work)) != FR_OK) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[BURN] f_mkfs FAIL\r\n");
            return -1;
        }
        if (f_mount(&s_fs, "", 1) != FR_OK) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[BURN] f_mount FAIL after mkfs\r\n");
            return -1;
        }
    }

    Serial_SendByte(SERIAL_PORT_DEBUG, 0xAA);

    idx = 0;
    uint8_t len_buf[4];
    uint32_t poll_cnt = 0;
    while (idx < 4) {
        if (Serial_IsDataReady(SERIAL_PORT_DEBUG)) {
            uint8_t *buf;
            uint16_t len = Serial_GetDataPacket(SERIAL_PORT_DEBUG, &buf);
            for (uint16_t i = 0; i < len && idx < 4; i++)
                len_buf[idx++] = buf[i];
        }
        poll_cnt++;
        if (poll_cnt >= 200000) {
            Serial_SendByte(SERIAL_PORT_DEBUG, 0xAA);
            poll_cnt = 0;
        }
    }

    total_data_bytes = len_buf[0] | (len_buf[1] << 8) | (len_buf[2] << 16) | (len_buf[3] << 24);

    if (total_data_bytes > GLXSS_FW_MAX_SIZE) {
        Serial_SendByte(SERIAL_PORT_DEBUG, 0xE0);
        return -1;
    }

    Serial_SendByte(SERIAL_PORT_DEBUG, 0xBB);
    Serial_SendByte(SERIAL_PORT_DEBUG, 0xCC);

    FIL fw;
    if (f_open(&fw, FW_FILENAME, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        Serial_SendByte(SERIAL_PORT_DEBUG, 0xE1);
        return -2;
    }

    uint32_t total_sectors = (total_data_bytes + SD_SECTOR_SIZE - 1) / SD_SECTOR_SIZE;
    uint32_t written_bytes = 0;

    for (uint32_t sec = 0; sec < total_sectors; sec++) {
        memset(sec_buf, 0xFF, SD_SECTOR_SIZE);

        uint32_t received = 0;
        while (received < SD_SECTOR_SIZE) {
            if (Serial_IsDataReady(SERIAL_PORT_DEBUG)) {
                uint8_t *buf;
                uint16_t len = Serial_GetDataPacket(SERIAL_PORT_DEBUG, &buf);
                uint16_t copy = (received + len > SD_SECTOR_SIZE) ? (SD_SECTOR_SIZE - received) : len;
                memcpy(sec_buf + received, buf, copy);
                received += copy;
            }
        }

        uint32_t write_len = SD_SECTOR_SIZE;
        if (written_bytes + write_len > total_data_bytes) {
            write_len = total_data_bytes - written_bytes;
        }

        UINT bw = 0;
        if (f_write(&fw, sec_buf, write_len, &bw) != FR_OK || bw != write_len) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[BURN] f_write fail at sector %lu\r\n", (unsigned long)sec);
            Serial_SendByte(SERIAL_PORT_DEBUG, 0xE2);
            f_close(&fw);
            return -2;
        }
        written_bytes += write_len;

        Delay_Ms(5);
        Serial_SendByte(SERIAL_PORT_DEBUG, 0xEE);
    }

    f_close(&fw);
    Serial_SendByte(SERIAL_PORT_DEBUG, 0xFF);

    Serial_Printf(SERIAL_PORT_DEBUG, "[BURN] Done: %lu bytes to %s\r\n",
                  (unsigned long)total_data_bytes, FW_FILENAME);

    return 0;
}
