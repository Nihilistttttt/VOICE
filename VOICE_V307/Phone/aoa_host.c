#include "aoa_host.h"
#include "usb_host_config.h"
#include "ch32v30x_usbhs_host.h"
#include "debug.h"
#include <string.h>

#define Serial_Dbg_Printf printf

/*============================================================================
 * AOA Host — USB Android Open Accessory protocol (CH32V307 版)
 *
 * Flow:
 *   1. USB enumerate phone
 *   2. Send GET_PROTOCOL (ctrl req 51) -> check AOA support
 *   3. Send SEND_STRING (ctrl req 52) for manufacturer/model/version
 *   4. Send START_ACCESSORY_MODE (ctrl req 53)
 *   5. Phone re-enumerates as AOA device (VID=0x2D00)
 *   6. Find bulk endpoints, communicate
 *============================================================================*/

#define AOA_USB_ADDR        0x02
#define AOA_GET_PROTOCOL    51
#define AOA_SEND_STRING     52
#define AOA_START_MODE      53

#define AOA_VID_ACCESSORY   0x2D00
#define AOA_VID_ADB         0x2D01

#define AOA_MANUFACTURER    "VoiceCtrl"
#define AOA_MODEL           "CH32V307"
#define AOA_VERSION         "1.0"
#define AOA_DESCRIPTION     "Voice Control for Paraplegic Patients"
#define AOA_URI             ""
#define AOA_SERIAL          ""

static AOA_State_t s_aoa_state = AOA_STATE_DISCONNECTED;
static uint8_t  s_ep0_size = 64;
static uint8_t s_bulk_in_tog = 0;
static uint8_t s_bulk_out_tog = 0;
static uint8_t  s_bulk_in_ep = 0;
static uint8_t  s_bulk_out_ep = 0;
static uint16_t s_bulk_in_size = 512;
static uint16_t s_bulk_out_size = 512;

static uint8_t  s_dev_desc[18];
static uint8_t  s_cfg_buf[1024];

struct _ROOT_HUB_DEVICE RootHubDev[DEF_TOTAL_ROOT_HUB];
struct __HOST_CTL      HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

/* 检查端口是否使能 */
#define PORT_IS_ENABLED()  (USBHSH_CheckRootHubPortEnable() != 0)
/* 检查设备是否连接 */
#define DEV_IS_ATTACHED()  (USBHSH->MIS_ST & USBHS_UMS_DEV_ATTACH)

/*---------------------------------------------------------------------------
 * Send AOA control request (vendor-specific)
 *---------------------------------------------------------------------------*/
static uint8_t AOA_CtrlRequest(uint8_t bRequestType, uint8_t bRequest,
                                uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                                uint8_t *pbuf, uint16_t *plen)
{
    pUSBHS_SetupRequest->bRequestType = bRequestType;
    pUSBHS_SetupRequest->bRequest    = bRequest;
    pUSBHS_SetupRequest->wValue      = wValue;
    pUSBHS_SetupRequest->wIndex      = wIndex;
    pUSBHS_SetupRequest->wLength     = wLength;
    return USBHSH_CtrlTransfer(s_ep0_size, pbuf, plen);
}

/*---------------------------------------------------------------------------
 * Check if device supports AOA (GET_PROTOCOL)
 *---------------------------------------------------------------------------*/
static int AOA_CheckSupport(void)
{
    uint8_t  buf[2] = {0, 0};
    uint16_t len = 2;

    uint8_t err = AOA_CtrlRequest(0xC0, AOA_GET_PROTOCOL, 0, 0, 2, buf, &len);
    Serial_Dbg_Printf("[AOA] GET_PROTOCOL err=%d len=%d data=[%02X %02X]\r\n",
                      err, len, buf[0], buf[1]);

    if (err != ERR_SUCCESS || len < 2) return -1;

    uint16_t proto = buf[0] | ((uint16_t)buf[1] << 8);
    Serial_Dbg_Printf("[AOA] Protocol version=%d\r\n", proto);
    return (proto >= 1) ? 0 : -1;
}

/*---------------------------------------------------------------------------
 * Send accessory identification strings
 *---------------------------------------------------------------------------*/
static void AOA_SendStrings(void)
{
    const char *strings[] = { AOA_MANUFACTURER, AOA_MODEL, AOA_VERSION,
                              AOA_URI, AOA_SERIAL, AOA_DESCRIPTION };
    for (int i = 0; i < 6; i++) {
        if (strings[i][0] == '\0') continue;
        uint16_t slen = strlen(strings[i]) + 1;
        uint16_t len = slen;
        uint8_t err = AOA_CtrlRequest(0x40, AOA_SEND_STRING, 0, i, slen,
                                      (uint8_t *)strings[i], &len);
        Serial_Dbg_Printf("[AOA] SEND_STRING[%d] \"%s\" err=%d\r\n", i, strings[i], err);
    }
}

/*---------------------------------------------------------------------------
 * Find bulk endpoints in config descriptor (AOA uses class=0xFF)
 *---------------------------------------------------------------------------*/
static int AOA_FindEndpoints(uint8_t *cfg_buf, uint16_t cfg_len)
{
    uint16_t i = 0;
    s_bulk_in_ep = 0;
    s_bulk_out_ep = 0;

    while (i < cfg_len) {
        if (cfg_buf[i + 1] == DEF_DECR_INTERFACE) {
            uint8_t intf_len = cfg_buf[i];
            Serial_Dbg_Printf("[AOA] If class=0x%02X sub=0x%02X\r\n",
                              cfg_buf[i+5], cfg_buf[i+6]);
            uint16_t j = i + intf_len;
            while (j < cfg_len) {
                if (cfg_buf[j + 1] == DEF_DECR_ENDPOINT) {
                    uint8_t  ep_addr = cfg_buf[j + 2];
                    uint8_t  ep_attr = cfg_buf[j + 3];
                    uint16_t ep_size = cfg_buf[j + 4] | ((uint16_t)cfg_buf[j + 5] << 8);

                    if ((ep_attr & 0x03) == 0x02) {
                        if (ep_addr & 0x80) {
                            s_bulk_in_ep = ep_addr;
                            s_bulk_in_size = ep_size;
                            Serial_Dbg_Printf("[AOA]   IN  ep=0x%02X size=%d\r\n",
                                              s_bulk_in_ep, s_bulk_in_size);
                        } else {
                            s_bulk_out_ep = ep_addr;
                            s_bulk_out_size = ep_size;
                            Serial_Dbg_Printf("[AOA]   OUT ep=0x%02X size=%d\r\n",
                                              s_bulk_out_ep, s_bulk_out_size);
                        }
                    }
                }
                if (cfg_buf[j + 1] == DEF_DECR_INTERFACE || cfg_buf[j + 1] == DEF_DECR_CONFIG) break;
                j += cfg_buf[j];
            }
            if (s_bulk_in_ep && s_bulk_out_ep) return 0;
            i += intf_len;
        } else {
            i += cfg_buf[i];
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------
 * USB enumerate device (reset, get desc, set addr, get cfg, set cfg)
 *---------------------------------------------------------------------------*/
static int AOA_Enumerate(uint8_t *is_aoa_device)
{
    uint8_t  speed, enum_cnt;
    uint16_t cfg_len;
    uint8_t  pspeed;

    enum_cnt = 0;

ENUM_RETRY:
    *is_aoa_device = 0;
    Delay_Ms(100);
    enum_cnt++;
    Delay_Ms(8 << enum_cnt);
    Serial_Dbg_Printf("[AOA] Enum attempt %d\r\n", enum_cnt);

    /* 端口复位 + 等待设备稳定连接 (参考 V307 官方示例) */
    USBHSH_ResetRootHubPort(0);
    {
        uint16_t i;
        uint8_t  s;
        for (i = 0, s = 0; i < DEF_RE_ATTACH_TIMEOUT; i++) {
            if (USBHSH_EnableRootHubPort(&pspeed) == ERR_SUCCESS) {
                i = 0;
                s++;
                if (s > 6) break;
            }
            Delay_Ms(1);
        }
        if (i) {
            Serial_Dbg_Printf("[AOA] EnableRootHubPort timeout\r\n");
            if (enum_cnt <= 5) goto ENUM_RETRY;
            return -1;
        }
    }

    speed = USBHSH_CheckRootHubPortSpeed();
    Serial_Dbg_Printf("[AOA] Port ready speed=%d\r\n", speed);

    uint8_t desc_err = USBHSH_GetDeviceDescr(&s_ep0_size, s_dev_desc);
    Serial_Dbg_Printf("[AOA] GetDeviceDescr ret=%d ep0=%d\r\n", desc_err, s_ep0_size);
    if (desc_err != ERR_SUCCESS) {
        Serial_Dbg_Printf("[AOA] FAIL GetDeviceDescr err=%d\r\n", desc_err);
        if (enum_cnt <= 5) goto ENUM_RETRY;
        return -1;
    }

    uint16_t vid = s_dev_desc[8] | ((uint16_t)s_dev_desc[9] << 8);
    uint16_t pid = s_dev_desc[10] | ((uint16_t)s_dev_desc[11] << 8);
    Serial_Dbg_Printf("[AOA] VID=0x%04X PID=0x%04X ep0=%d\r\n", vid, pid, s_ep0_size);

    if (pid == AOA_VID_ACCESSORY || pid == AOA_VID_ADB) {
        Serial_Dbg_Printf("[AOA] Already in Accessory Mode!\r\n");
        *is_aoa_device = 1;
    }

    uint8_t addr_err = USBHSH_SetUsbAddress(s_ep0_size, AOA_USB_ADDR);
    if (addr_err != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_RETRY;
        return -1;
    }
    Delay_Ms(5);

    uint8_t cfg_err = USBHSH_GetConfigDescr(s_ep0_size, s_cfg_buf, sizeof(s_cfg_buf), &cfg_len);
    if (cfg_err != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_RETRY;
        return -1;
    }

    uint8_t setcfg_err = USBHSH_SetUsbConfig(s_ep0_size,
                        ((PUSB_CFG_DESCR)s_cfg_buf)->bConfigurationValue);
    if (setcfg_err != ERR_SUCCESS) {
        if (enum_cnt <= 5) goto ENUM_RETRY;
        return -1;
    }

    Serial_Dbg_Printf("[AOA] Config desc hex (%d bytes):", cfg_len);
    for (uint16_t d = 0; d < cfg_len && d < 64; d++) {
        if (d % 16 == 0) Serial_Dbg_Printf("\r\n  ");
        Serial_Dbg_Printf("%02X ", s_cfg_buf[d]);
    }
    Serial_Dbg_Printf("\r\n");

    Delay_Ms(200);
    s_bulk_in_tog = 0;
    s_bulk_out_tog = 0;

    if (AOA_FindEndpoints(s_cfg_buf, cfg_len) != 0) {
        Serial_Dbg_Printf("[AOA] No bulk endpoints found\r\n");
        return -1;
    }

    if (!(*is_aoa_device)) {
        if (AOA_CheckSupport() == 0) {
            Serial_Dbg_Printf("[AOA] Non-standard VID/PID, supports AOA but not in AOA mode\r\n");
        } else {
            Serial_Dbg_Printf("[AOA] Non-standard VID/PID, GET_PROTOCOL failed, likely in AOA mode\r\n");
            *is_aoa_device = 1;
        }
    }

    RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_SUCCESS;
    RootHubDev[DEF_USBHS_PORT_INDEX].bAddress = AOA_USB_ADDR;
    RootHubDev[DEF_USBHS_PORT_INDEX].bEp0MaxPks = s_ep0_size;

    return 0;
}

/*===========================================================================
 * Public API
 *===========================================================================*/

AOA_Err_t AOA_Host_Init(void)
{
    s_aoa_state = AOA_STATE_DISCONNECTED;
    Serial_Dbg_Printf("[AOA] === Host Init ===\r\n");

    USBHS_RCC_Init();
    Serial_Dbg_Printf("[AOA] 5s delay before USBHS init...\r\n");
    Delay_Ms(5000);
    USBHS_Host_Init(ENABLE);
    Delay_Ms(100);

    memset(&RootHubDev[DEF_USBHS_PORT_INDEX].bStatus, 0, sizeof(ROOT_HUB_DEVICE));
    memset(&HostCtl[DEF_USBHS_PORT_INDEX * DEF_ONE_USB_SUP_DEV_TOTAL].InterfaceNum,
           0, DEF_ONE_USB_SUP_DEV_TOTAL * sizeof(HOST_CTL));

    s_ep0_size = 64;
    s_bulk_in_tog = 0;
    s_bulk_out_tog = 0;

    Serial_Dbg_Printf("[AOA] USBHS Host ready\r\n");
    return AOA_OK;
}

AOA_Err_t AOA_Host_Connect(void)
{
    uint32_t tick = 0;
    uint8_t  is_aoa;

    Serial_Dbg_Printf("\r\n[AOA] === Connect ===\r\n");

    /* Force USB Host off/on so Android detects a clean disconnect+reconnect.
     * This clears Android's AOA framework state so it will respond to
     * START_ACCESSORY again (otherwise the phone ignores START after the
     * first AOA session ends). */
    Serial_Dbg_Printf("[AOA] Resetting USB Host port...\r\n");
    USBHS_Host_Init(DISABLE);
    Delay_Ms(500);
    USBHS_Host_Init(ENABLE);
    Delay_Ms(100);
    s_aoa_state = AOA_STATE_DISCONNECTED;
    s_bulk_in_tog = 0;
    s_bulk_out_tog = 0;

    Serial_Dbg_Printf("[AOA] Waiting for Android device (no timeout)...\r\n");

    int connect_fail_count = 0;

    while (1) {
        uint8_t status = USBHSH_CheckRootHubPortStatus(ROOT_DEV_DISCONNECT);

        if (status == ROOT_DEV_CONNECTED) {
            Serial_Dbg_Printf("[AOA] Device detected!\r\n");

            if (AOA_Enumerate(&is_aoa) != 0) {
                Serial_Dbg_Printf("[AOA] Enumerate failed, retry...\r\n");
                Delay_Ms(1000);
                continue;
            }

            if (is_aoa) {
                Serial_Dbg_Printf("[AOA] === AOA Mode Ready! ===\r\n");
                Serial_Dbg_Printf("[AOA] Waiting 3s for app to open accessory...\r\n");
                Delay_Ms(3000);
                s_aoa_state = AOA_STATE_READY;
                return AOA_OK;
            }

            Serial_Dbg_Printf("[AOA] Checking AOA support...\r\n");
            if (AOA_CheckSupport() != 0) {
                Serial_Dbg_Printf("[AOA] Device does not support AOA, retry...\r\n");
                Delay_Ms(2000);
                continue;
            }

            Serial_Dbg_Printf("[AOA] Sending accessory strings...\r\n");
            AOA_SendStrings();

            Serial_Dbg_Printf("[AOA] Starting accessory mode...\r\n");
            uint8_t start_err = AOA_CtrlRequest(0x40, AOA_START_MODE, 0, 0, 0, NULL, NULL);
            Serial_Dbg_Printf("[AOA] START_ACCESSORY err=%d\r\n", start_err);

            RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_DISCONNECT;
            s_aoa_state = AOA_STATE_DISCONNECTED;
            s_bulk_in_tog = 0;
            s_bulk_out_tog = 0;

            /* Phase 1: Wait for phone to naturally disconnect */
            Serial_Dbg_Printf("[AOA] Waiting for device to disconnect (5s timeout)...\r\n");
            uint8_t did_disconnect = 0;
            for (int i = 0; i < 50; i++) {
                Delay_Ms(100);
                if (!DEV_IS_ATTACHED()) {
                    Serial_Dbg_Printf("[AOA] Device disconnected after %dms\r\n", (i + 1) * 100);
                    did_disconnect = 1;
                    break;
                }
            }

            if (!did_disconnect) {
                Serial_Dbg_Printf("[AOA] No disconnect, forcing port reset...\r\n");
                USBHSH_ResetRootHubPort(0);
                Delay_Ms(500);
            }

            /* Phase 2: Wait for phone to reconnect as AOA device */
            int restart_count = 0;
            for (int retry = 0; retry < 30; retry++) {
                Delay_Ms(1000);
                status = USBHSH_CheckRootHubPortStatus(ROOT_DEV_DISCONNECT);
                if (status == ROOT_DEV_CONNECTED) {
                    Serial_Dbg_Printf("[AOA] Re-enumeration detected!\r\n");
                    if (AOA_Enumerate(&is_aoa) == 0 && is_aoa) {
                        Serial_Dbg_Printf("[AOA] === AOA Mode Ready! ===\r\n");
                        Serial_Dbg_Printf("[AOA] Waiting 3s for app to open accessory...\r\n");
                        Delay_Ms(3000);
                        s_aoa_state = AOA_STATE_READY;
                        return AOA_OK;
                    }
                    Serial_Dbg_Printf("[AOA] Re-enum but not AOA device\r\n");
                    if (restart_count >= 3) {
                        Serial_Dbg_Printf("[AOA] Max restart attempts (%d), giving up...\r\n", restart_count);
                        break;
                    }
                    if (AOA_CheckSupport() == 0) {
                        Serial_Dbg_Printf("[AOA] Still supports AOA, re-sending START (%d/3)...\r\n", restart_count + 1);
                        restart_count++;
                        AOA_SendStrings();
                        AOA_CtrlRequest(0x40, AOA_START_MODE, 0, 0, 0, NULL, NULL);
                        for (int d = 0; d < 30; d++) {
                            Delay_Ms(100);
                            if (!DEV_IS_ATTACHED()) {
                                Serial_Dbg_Printf("[AOA] Disconnected after re-START\r\n");
                                break;
                            }
                        }
                    }
                    RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_DISCONNECT;
                }
                Serial_Dbg_Printf("[AOA] Re-enum wait %d/30\r\n", retry + 1);
            }

            connect_fail_count++;
            if (connect_fail_count >= 3) {
                Serial_Dbg_Printf("[AOA] %d consecutive failures, returning error (close app and retry)...\r\n", connect_fail_count);
                USBHSH_ResetRootHubPort(0);
                Delay_Ms(2000);
                return AOA_ERR_PROTOCOL;
            }

            Serial_Dbg_Printf("[AOA] Re-enumeration failed, forcing port reset...\r\n");
            USBHSH_ResetRootHubPort(0);
            Delay_Ms(2000);
            continue;
        }

        Delay_Ms(10);
        tick += 10;
        if (tick % 3000 == 0) {
            Serial_Dbg_Printf("[AOA] Waiting... (%lus)\r\n", (unsigned long)(tick / 1000));
        }
    }
}

AOA_Err_t AOA_Host_Disconnect(void)
{
    Serial_Dbg_Printf("[AOA] Disconnect\r\n");
    s_aoa_state = AOA_STATE_DISCONNECTED;
    RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_DISCONNECT;
    return AOA_OK;
}

AOA_State_t AOA_Host_GetState(void)
{
    return s_aoa_state;
}

AOA_Err_t AOA_Host_Send(const uint8_t *data, uint16_t len)
{
    if (s_aoa_state != AOA_STATE_READY) return AOA_ERR_NOT_INIT;
    if (!PORT_IS_ENABLED()) {
        Serial_Dbg_Printf("[AOA] Port not enabled\r\n");
        s_aoa_state = AOA_STATE_DISCONNECTED;
        return AOA_ERR_TX;
    }

    Serial_Dbg_Printf("[AOA] TX %d bytes:", len);
    for (uint16_t i = 0; i < len && i < 32; i++) Serial_Dbg_Printf(" %02X", data[i]);
    Serial_Dbg_Printf("\r\n");

    for (int retry = 0; retry < 3; retry++) {
        uint8_t err = USBHSH_SendEndpData(s_bulk_out_ep, &s_bulk_out_tog, (uint8_t *)data, len);
        if (err == ERR_SUCCESS) return AOA_OK;

        Serial_Dbg_Printf("[AOA] TX err=%d (retry %d/3)\r\n", err, retry + 1);
        s_bulk_out_tog = 0;
        Delay_Ms(200);

        if (!PORT_IS_ENABLED()) {
            Serial_Dbg_Printf("[AOA] Port lost during TX\r\n");
            s_aoa_state = AOA_STATE_DISCONNECTED;
            return AOA_ERR_TX;
        }
    }

    Serial_Dbg_Printf("[AOA] TX failed after 3 retries\r\n");
    return AOA_ERR_TX;
}

AOA_Err_t AOA_Host_Recv(uint8_t *buf, uint16_t buf_size, uint16_t *actual_len)
{
    if (s_aoa_state != AOA_STATE_READY) return AOA_ERR_NOT_INIT;
    if (!PORT_IS_ENABLED()) return AOA_ERR_RX;

    uint16_t len = buf_size;
    uint8_t err = USBHSH_GetEndpData(s_bulk_in_ep, &s_bulk_in_tog, buf, &len);

    if (err == ERR_SUCCESS && len > 0) {
        *actual_len = len;
        Serial_Dbg_Printf("[AOA] RX %d bytes:", len);
        for (uint16_t i = 0; i < len && i < 32; i++) Serial_Dbg_Printf(" %02X", buf[i]);
        Serial_Dbg_Printf("\r\n");
        return AOA_OK;
    }
    *actual_len = 0;
    return AOA_ERR_RX;
}

void AOA_Host_Poll(void)
{
    if (s_aoa_state == AOA_STATE_READY) {
        if (!PORT_IS_ENABLED()) {
            Delay_Ms(50);
            if (!PORT_IS_ENABLED()) {
                Delay_Ms(100);
                if (!PORT_IS_ENABLED()) {
                    Serial_Dbg_Printf("[AOA] Port lost! State->DISCONNECTED\r\n");
                    s_aoa_state = AOA_STATE_DISCONNECTED;
                }
            }
        }
    }
}