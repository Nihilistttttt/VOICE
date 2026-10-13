#ifndef _GLXSS_ME_H_
#define _GLXSS_ME_H_

#include <stdint.h>

#define GLXSS_BOOTLOADER_VID    0x03E7
#define GLXSS_BOOTLOADER_PID    0x2150
#define GLXSS_DEVICE_VID        0x2E09
#define GLXSS_DEVICE_PID        0x0030

#define GLXSS_EP_BOOT_FW        0x01
#define GLXSS_EP_DATA_OUT       0x01

#define GLXSS_FW_MAX_SIZE       (2 * 1024 * 1024)
#define GLXSS_FW_CHUNK_SIZE     512

#define GLXSS_MAX_WIDTH         1280
#define GLXSS_MAX_HEIGHT        720
#define GLXSS_MAX_FRAME_SIZE    (GLXSS_MAX_WIDTH * GLXSS_MAX_HEIGHT * 4)

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t  orientation;
    uint8_t  rotation;
    uint16_t brightness;
} glxss_lcd_info_t;

typedef enum {
    GLXSS_OK                = 0,
    GLXSS_ERR_NOT_FOUND     = -1,
    GLXSS_ERR_ENUM          = -2,
    GLXSS_ERR_CTL_TRANSFER  = -3,
    GLXSS_ERR_BULK_WRITE    = -4,
    GLXSS_ERR_FW_TOO_LARGE  = -5,
    GLXSS_ERR_FW_READ       = -6,
    GLXSS_ERR_FW_TIMEOUT    = -7,
    GLXSS_ERR_NO_MEM        = -8,
    GLXSS_ERR_TIMEOUT       = -9,
    GLXSS_ERR_INVALID_PARAM = -10,
} glxss_err_t;

typedef uint32_t (*glxss_fw_read_t)(uint32_t offset, uint8_t *buf, uint32_t len);

int glxss_usb_host_init(void);
glxss_err_t glxss_usb_wait_device(uint16_t vid, uint16_t pid, uint32_t timeout_ms);
glxss_err_t glxss_usb_ctrl_transfer(uint8_t  bmRequestType,
                                    uint8_t  bRequest,
                                    uint16_t wValue,
                                    uint16_t wIndex,
                                    uint8_t *data,
                                    uint16_t wLength);
glxss_err_t glxss_usb_clear_halt(uint8_t ep);
glxss_err_t glxss_usb_bulk_write(uint8_t ep, const uint8_t *data, uint32_t len);

glxss_err_t glxss_init(glxss_fw_read_t fw_read,
                       uint32_t        fw_size,
                       uint32_t        timeout_ms);
glxss_err_t glxss_load_firmware(glxss_fw_read_t fw_read, uint32_t fw_size);
glxss_err_t glxss_get_lcd_info(glxss_lcd_info_t *info);
glxss_err_t glxss_display_frame(uint16_t width, uint16_t height, const uint8_t *data);
glxss_err_t glxss_set_brightness(uint16_t brightness);
glxss_err_t glxss_set_roll(uint8_t roll);
glxss_err_t glxss_power_switch(uint8_t on);
glxss_err_t glxss_set_display_mode(uint8_t mode);

#endif