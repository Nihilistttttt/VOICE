#include "glxss_me.h"
#include "debug.h"
#include "usb_host_config.h"
#include "ch32h417_usbhs_host.h"
#include <string.h>

#define Serial_Printf(...)

#ifndef GLXSS_LOG
#define GLXSS_LOG(...)
#endif
#ifndef GLXSS_ERR
#define GLXSS_ERR(...)
#endif

#ifndef USB_PID_NAK
#define USB_PID_NAK             0x0A
#endif

#define GLXSS_USB_ADDR          2

static uint8_t  s_ep0_size = 64;
static uint16_t s_bulk_tog = 0;

extern uint32_t SystemCoreClock;

static uint32_t glxss_get_ms(void)
{
    uint32_t cycle;
    __asm__ volatile("rdcycle %0" : "=r"(cycle));
    return cycle / (SystemCoreClock / 1000);
}

int glxss_usb_host_init(void)
{
    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] USB Host init in 5s (window for MCU flash)...\r\n");
    Delay_Ms(5000);
    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] USB Host init starting\r\n");

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF10);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF10);

    USBHS_Host_Init(ENABLE);

    memset(&RootHubDev[DEF_USBHS_PORT_INDEX].bStatus, 0, sizeof(ROOT_HUB_DEVICE));
    memset(&HostCtl[DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL].InterfaceNum,
           0, DEF_ONE_USB_SUP_DEV_TOTAL * sizeof(HOST_CTL));

    s_ep0_size = 64;
    s_bulk_tog = 0;

    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] USBHS Host init done, PORT_STATUS=0x%08lX\r\n",
                  (unsigned long)USBHSH->PORT_STATUS);
    return 0;
}

static void parse_bulk_endpoints(uint8_t *cfg_buf, uint16_t cfg_len)
{
    uint8_t index = DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL;
    uint8_t intf_num = 0;
    uint8_t in_num = 0, out_num = 0;
    uint16_t i = 0;

    HostCtl[index].InterfaceNum = 0;
    while (i < cfg_len) {
        if (cfg_buf[i + 1] == DEF_DECR_CONFIG) {
            if (((PUSB_CFG_DESCR)(&cfg_buf[i]))->bNumInterfaces > DEF_INTERFACE_NUM_MAX)
                HostCtl[index].InterfaceNum = DEF_INTERFACE_NUM_MAX;
            else
                HostCtl[index].InterfaceNum = ((PUSB_CFG_DESCR)(&cfg_buf[i]))->bNumInterfaces;
            i += cfg_buf[i];
        } else if (cfg_buf[i + 1] == DEF_DECR_INTERFACE) {
            if (intf_num >= DEF_INTERFACE_NUM_MAX) break;
            in_num = 0;
            out_num = 0;
            HostCtl[index].Interface[intf_num].InEndpNum = 0;
            HostCtl[index].Interface[intf_num].OutEndpNum = 0;
            i += cfg_buf[i];
            while (i < cfg_len) {
                if (cfg_buf[i + 1] == DEF_DECR_INTERFACE || cfg_buf[i + 1] == DEF_DECR_CONFIG)
                    break;
                if (cfg_buf[i + 1] == DEF_DECR_ENDPOINT) {
                    if (((PUSB_ENDP_DESCR)(&cfg_buf[i]))->bEndpointAddress & 0x80) {
                        if (in_num < 4) {
                            HostCtl[index].Interface[intf_num].InEndpAddr[in_num] =
                                ((PUSB_ENDP_DESCR)(&cfg_buf[i]))->bEndpointAddress & 0x0F;
                            HostCtl[index].Interface[intf_num].InEndpSize[in_num] =
                                ((PUSB_ENDP_DESCR)(&cfg_buf[i]))->wMaxPacketSizeL |
                                (uint16_t)(((PUSB_ENDP_DESCR)(&cfg_buf[i]))->wMaxPacketSizeH << 8);
                            HostCtl[index].Interface[intf_num].InEndpType[in_num] =
                                ((PUSB_ENDP_DESCR)(&cfg_buf[i]))->bmAttributes;
                            HostCtl[index].Interface[intf_num].InEndpNum++;
                            in_num++;
                        }
                    } else {
                        if (out_num < 4) {
                            HostCtl[index].Interface[intf_num].OutEndpAddr[out_num] =
                                ((PUSB_ENDP_DESCR)(&cfg_buf[i]))->bEndpointAddress & 0x0F;
                            HostCtl[index].Interface[intf_num].OutEndpSize[out_num] =
                                ((PUSB_ENDP_DESCR)(&cfg_buf[i]))->wMaxPacketSizeL |
                                (uint16_t)(((PUSB_ENDP_DESCR)(&cfg_buf[i]))->wMaxPacketSizeH << 8);
                            HostCtl[index].Interface[intf_num].OutEndpType[out_num] =
                                ((PUSB_ENDP_DESCR)(&cfg_buf[i]))->bmAttributes;
                            HostCtl[index].Interface[intf_num].OutEndpNum++;
                            out_num++;
                        }
                    }
                    i += cfg_buf[i];
                } else {
                    i += cfg_buf[i];
                }
            }
            intf_num++;
        } else {
            i += cfg_buf[i];
        }
    }

    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Parsed %d interfaces\r\n", HostCtl[index].InterfaceNum);
    for (int n = 0; n < HostCtl[index].InterfaceNum; n++) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS]  If%d: IN=%d OUT=%d\r\n", n,
                      HostCtl[index].Interface[n].InEndpNum,
                      HostCtl[index].Interface[n].OutEndpNum);
        for (int e = 0; e < HostCtl[index].Interface[n].OutEndpNum; e++) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS]    OUT ep=%d size=%d type=%d\r\n",
                          HostCtl[index].Interface[n].OutEndpAddr[e],
                          HostCtl[index].Interface[n].OutEndpSize[e],
                          HostCtl[index].Interface[n].OutEndpType[e]);
        }
    }
}

glxss_err_t glxss_usb_wait_device(uint16_t vid, uint16_t pid, uint32_t timeout_ms)
{
    uint32_t tick_start = glxss_get_ms();
    static uint8_t  dev_desc[18], cfg_buf[1024];
    uint16_t cfg_len;
    uint8_t  speed, retry;
    uint8_t  poll_cnt = 0;

    while (1) {
        uint8_t status = USBHSH_CheckRootHubPortStatus(ROOT_DEV_DISCONNECT);

        if (status == ROOT_DEV_CONNECTED) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Device connected! Resetting...\r\n");
            USBHSH_ResetRootHubPort();
            for (retry = 0; retry < 200; retry++) {
                if (USBHSH_EnableRootHubPort(&speed) == ERR_SUCCESS) break;
                Delay_Ms(1);
            }
            if (retry >= 200) {
                Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] EnableRootHubPort failed\r\n");
                continue;
            }

            if (USBHSH_GetDeviceDescr(&s_ep0_size, dev_desc) != ERR_SUCCESS) {
                Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] GetDeviceDescr failed\r\n");
                continue;
            }

            uint16_t dv = dev_desc[8] | ((uint16_t)dev_desc[9] << 8);
            uint16_t dp = dev_desc[10] | ((uint16_t)dev_desc[11] << 8);
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Found VID=0x%04X PID=0x%04X\r\n", dv, dp);
            if ((vid && dv != vid) || (pid && dp != pid)) continue;

            uint8_t addr_err = USBHSH_SetUsbAddress(s_ep0_size, GLXSS_USB_ADDR);
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] SetUsbAddress: %d\r\n", addr_err);
            if (addr_err != ERR_SUCCESS) continue;
            Delay_Ms(5);

            uint8_t cfg_err = USBHSH_GetConfigDescr(s_ep0_size, cfg_buf, sizeof(cfg_buf), &cfg_len);
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] GetConfigDescr: %d len=%d\r\n", cfg_err, cfg_len);
            if (cfg_err != ERR_SUCCESS) continue;

            uint8_t set_cfg_err = USBHSH_SetUsbConfig(s_ep0_size, ((PUSB_CFG_DESCR)cfg_buf)->bConfigurationValue);
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] SetUsbConfig: %d cfgVal=%d\r\n", set_cfg_err, ((PUSB_CFG_DESCR)cfg_buf)->bConfigurationValue);
            if (set_cfg_err != ERR_SUCCESS) continue;

            parse_bulk_endpoints(cfg_buf, cfg_len);

            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] CfgDesc hex dump (%d bytes):\r\n", cfg_len);
            for (uint16_t di = 0; di < cfg_len; di += 16) {
                Serial_Printf(SERIAL_PORT_DEBUG, "%03X:", di);
                for (uint16_t dj = 0; dj < 16 && (di + dj) < cfg_len; dj++)
                    Serial_Printf(SERIAL_PORT_DEBUG, " %02X", cfg_buf[di + dj]);
                Serial_Printf(SERIAL_PORT_DEBUG, "\r\n");
            }

            RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_SUCCESS;
            RootHubDev[DEF_USBHS_PORT_INDEX].bAddress = GLXSS_USB_ADDR;
            RootHubDev[DEF_USBHS_PORT_INDEX].bEp0MaxPks = s_ep0_size;
            RootHubDev[DEF_USBHS_PORT_INDEX].DeviceIndex = DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL;

            Delay_Ms(50);

            return GLXSS_OK;
        }

        if ((glxss_get_ms() - tick_start) > timeout_ms) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Wait device timeout (%lums)\r\n",
                          (unsigned long)timeout_ms);
            return GLXSS_ERR_TIMEOUT;
        }
        Delay_Ms(5);
        if (++poll_cnt % 200 == 0) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] Waiting... PORT=0x%08lX\r\n",
                          (unsigned long)USBHSH->PORT_STATUS);
        }
    }
}

glxss_err_t glxss_usb_ctrl_transfer(uint8_t  bmRequestType,
                                    uint8_t  bRequest,
                                    uint16_t wValue,
                                    uint16_t wIndex,
                                    uint8_t *data,
                                    uint16_t wLength)
{
    pUSBHS_SetupRequest->bRequestType = bmRequestType;
    pUSBHS_SetupRequest->bRequest     = bRequest;
    pUSBHS_SetupRequest->wValue       = wValue;
    pUSBHS_SetupRequest->wIndex       = wIndex;
    pUSBHS_SetupRequest->wLength      = wLength;

    uint16_t xfer_len = wLength;
    uint8_t err = USBHSH_CtrlTransfer(s_ep0_size, data, &xfer_len);

    return (err == ERR_SUCCESS) ? GLXSS_OK : GLXSS_ERR_CTL_TRANSFER;
}

glxss_err_t glxss_usb_clear_halt(uint8_t ep)
{
    uint8_t err = USBHSH_ClearEndpStall(s_ep0_size, ep);
    if (err != ERR_SUCCESS) {
        Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] ClearEndpStall ep=%d err=0x%02X\r\n", ep, err);
        return GLXSS_ERR_CTL_TRANSFER;
    }

    uint8_t index = DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL;
    for (uint8_t n = 0; n < HostCtl[index].InterfaceNum; n++) {
        for (uint8_t e = 0; e < HostCtl[index].Interface[n].OutEndpNum; e++) {
            if (HostCtl[index].Interface[n].OutEndpAddr[e] == ep) {
                HostCtl[index].Interface[n].OutEndpTog[e] = 0;
            }
        }
        for (uint8_t e = 0; e < HostCtl[index].Interface[n].InEndpNum; e++) {
            if (HostCtl[index].Interface[n].InEndpAddr[e] == ep) {
                HostCtl[index].Interface[n].InEndpTog[e] = 0;
            }
        }
    }

    s_bulk_tog = 0;
    return GLXSS_OK;
}

#define GLXSS_NAK_RETRY_MAX     2000
#define GLXSS_NAK_RETRY_DELAY   1

glxss_err_t glxss_usb_bulk_write(uint8_t ep, const uint8_t *data, uint32_t len)
{
    uint8_t index = DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL;
    uint8_t out_ep = 0;
    uint16_t out_size = 0;
    uint16_t *out_tog = NULL;

    for (uint8_t n = 0; n < HostCtl[index].InterfaceNum; n++) {
        for (uint8_t e = 0; e < HostCtl[index].Interface[n].OutEndpNum; e++) {
            if (HostCtl[index].Interface[n].OutEndpType[e] == 0x02) {
                out_ep = HostCtl[index].Interface[n].OutEndpAddr[e];
                out_size = HostCtl[index].Interface[n].OutEndpSize[e];
                out_tog = &HostCtl[index].Interface[n].OutEndpTog[e];
                break;
            }
        }
        if (out_tog) break;
    }

    if (out_tog == NULL) {
        out_ep = ep;
        out_size = 512;
        static uint16_t fallback_tog = 0;
        out_tog = &fallback_tog;
    }

    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > out_size) chunk = out_size;

        uint16_t nak_retry = 0;
        uint8_t err;
        do {
            err = USBHSH_SendEndpData(out_ep, out_tog,
                                      (uint8_t *)&data[offset], (uint16_t)chunk);
            if (err == (USB_PID_NAK | ERR_USB_TRANSFER)) {
                Delay_Ms(GLXSS_NAK_RETRY_DELAY);
                nak_retry++;
                if (nak_retry >= GLXSS_NAK_RETRY_MAX) {
                    Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] SendEndpData NAK timeout ep=%d offset=%lu\r\n",
                                  out_ep, (unsigned long)offset);
                    return GLXSS_ERR_BULK_WRITE;
                }
                continue;
            }
            break;
        } while (1);

        if (err != ERR_SUCCESS) {
            Serial_Printf(SERIAL_PORT_DEBUG, "[GLXSS] SendEndpData err=0x%02X ep=%d tog=%d chunk=%lu offset=%lu\r\n",
                          err, out_ep, *out_tog, (unsigned long)chunk, (unsigned long)offset);
            return GLXSS_ERR_BULK_WRITE;
        }
        offset += chunk;
    }
    return GLXSS_OK;
}