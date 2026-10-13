#include "aoa_host.h"
#include "Serial.h"
#include "usb_host_config.h"
#include "ch32h417_usbhs_host.h"
#include "debug.h"
#include <string.h>

extern volatile uint8_t USBHSH_DebugStage;
extern volatile uint8_t USBHSH_DebugError;
extern volatile uint16_t USBHSH_DebugRxLen;

/*============================================================================
 * AOA Host — USB Android Open Accessory protocol
 *
 * Flow:
 *   1. USB enumerate phone
 *   2. Send GET_PROTOCOL (ctrl req 51) → check AOA support
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
#define AOA_MODEL           "CH32H417"
#define AOA_VERSION         "1.0"
#define AOA_DESCRIPTION     "Voice Control for Paraplegic Patients"
#define AOA_URI             ""
#define AOA_SERIAL          ""

static AOA_State_t s_aoa_state = AOA_STATE_DISCONNECTED;
static uint8_t  s_ep0_size = 64;
static uint16_t s_bulk_in_tog = 0;
static uint16_t s_bulk_out_tog = 0;
static uint8_t  s_bulk_in_ep = 0;
static uint8_t  s_bulk_out_ep = 0;
static uint16_t s_bulk_in_size = 512;
static uint16_t s_bulk_out_size = 512;

static uint8_t  s_dev_desc[18];
static uint8_t  s_cfg_buf[1024];

struct _ROOT_HUB_DEVICE RootHubDev[DEF_TOTAL_ROOT_HUB];
struct __HOST_CTL      HostCtl[DEF_TOTAL_ROOT_HUB * DEF_ONE_USB_SUP_DEV_TOTAL];

/*---------------------------------------------------------------------------
 * USBHS GPIO init (PB8 D+, PB9 D-, AF10)
 *---------------------------------------------------------------------------*/
static void USBHS_GPIO_Init(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF10);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF10);
    Serial_Dbg_Printf("[AOA] GPIO PB8/PB9 AF10 configured\r\n");
}

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
    uint16_t cfg_len, i;

    enum_cnt = 0;

ENUM_RETRY:
    *is_aoa_device = 0;
    Delay_Ms(100);
    enum_cnt++;
    Delay_Ms(8 << enum_cnt);
    Serial_Dbg_Printf("[AOA] Enum attempt %d\r\n", enum_cnt);

    USBHSH->PORT_STATUS_CHG = 0xff;
    USBHSH->PORT_CTRL |= USBHS_UH_SET_PORT_RESET;
    USBHSH->CFG |= USBHS_UH_SOF_EN;

    {
        uint8_t reset_ok = 0;
        for (i = 0; i < 100; i++) {
            if (USBHSH->PORT_STATUS_CHG & USBHS_UHIF_PORT_RESET) {
                if ((USBHSH->PORT_STATUS & USBHS_UHIS_PORT_CONNECT) &&
                    (USBHSH->PORT_STATUS & USBHS_UHIS_PORT_EN)) {
                    USBHSH->PORT_STATUS_CHG = USBHS_UHIF_PORT_RESET;
                    reset_ok = 1;
                    break;
                }
            }
            Delay_Ms(10);
        }
        if (!reset_ok) {
            Serial_Dbg_Printf("[AOA] Reset timeout PORT=0x%08lX\r\n",
                              (unsigned long)USBHSH->PORT_STATUS);
            if (enum_cnt <= 5) goto ENUM_RETRY;
            return -1;
        }
    }

    USBHSH->DEV_ADDR = 0x00;
    speed = USBHSH_CheckRootHubPortSpeed();
    Serial_Dbg_Printf("[AOA] Port ready speed=%d PORT=0x%08lX\r\n",
                      speed, (unsigned long)USBHSH->PORT_STATUS);

    USBHSH->TX_DMA = (uint32_t)USBHS_TX_Buf;
    USBHSH->RX_DMA = (uint32_t)USBHS_RX_Buf;
    USBHSH->RX_MAX_LEN = 512;
    USBHSH->CFG = USBHS_UH_SOF_EN | USBHS_UD_DMA_EN | USBHS_UD_PHY_SUSPENDM;

    uint8_t desc_err = USBHSH_GetDeviceDescr(&s_ep0_size, s_dev_desc);
    Serial_Dbg_Printf("[AOA] GetDeviceDescr ret=%d ep0=%d\r\n", desc_err, s_ep0_size);
    if (desc_err != ERR_SUCCESS) {
        Serial_Dbg_Printf("[AOA] FAIL stage=%d err=%d\r\n", USBHSH_DebugStage, USBHSH_DebugError);
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

    USBHS_GPIO_Init();
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

    Serial_Dbg_Printf("[AOA] USBHS Host ready, PORT=0x%08lX\r\n",
                      (unsigned long)USBHSH->PORT_STATUS);
    return AOA_OK;
}

AOA_Err_t AOA_Host_Connect(void)
{
    uint32_t tick = 0;
    uint8_t  is_aoa;

    Serial_Dbg_Printf("\r\n[AOA] === Connect ===\r\n");

    /* If port still shows connected, force reset to clear STALL state */
    if (USBHSH->PORT_STATUS & 0x02) {
        Serial_Dbg_Printf("[AOA] Port still active, forcing reset...\r\n");
        USBHSH->PORT_CTRL |= USBHS_UH_SET_PORT_RESET;
        Delay_Ms(50);
        USBHSH->PORT_CTRL &= ~USBHS_UH_SET_PORT_RESET;
        Delay_Ms(500);
        s_aoa_state = AOA_STATE_DISCONNECTED;
        s_bulk_in_tog = 0;
        s_bulk_out_tog = 0;
    }

    Serial_Dbg_Printf("[AOA] Waiting for Android device (no timeout)...\r\n");

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

            /* After START_ACCESSORY, phone should disconnect and re-enumerate
             * as AOA device (VID=0x18D1 PID=0x2D00).
             * Strategy: first wait for natural disconnect (doesn't interrupt AOA
             * switching). If phone doesn't disconnect, force hard port reset as
             * fallback to re-enumerate the device. */
            RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_DISCONNECT;
            s_aoa_state = AOA_STATE_DISCONNECTED;
            s_bulk_in_tog = 0;
            s_bulk_out_tog = 0;

            /* Phase 1: Wait for phone to naturally disconnect (port goes inactive).
             * The phone drops USB connection as part of switching to AOA mode. */
            Serial_Dbg_Printf("[AOA] Waiting for device to disconnect (5s timeout)...\r\n");
            uint8_t did_disconnect = 0;
            for (int i = 0; i < 50; i++) {
                Delay_Ms(100);
                if ((USBHSH->PORT_STATUS & USBHS_UHIS_PORT_CONNECT) == 0) {
                    Serial_Dbg_Printf("[AOA] Device disconnected after %dms\r\n", (i + 1) * 100);
                    did_disconnect = 1;
                    break;
                }
            }

            if (!did_disconnect) {
                /* Phone didn't disconnect on its own. Force a hard port reset
                 * to generate a USB bus reset signal. This causes the phone to
                 * re-enumerate. Soft reset (SOF toggle) doesn't work because it
                 * doesn't generate a bus reset. */
                Serial_Dbg_Printf("[AOA] No disconnect, forcing port reset...\r\n");
                USBHSH->PORT_STATUS_CHG = 0xff;
                USBHSH->PORT_CTRL |= USBHS_UH_SET_PORT_RESET;
                Delay_Ms(60);
                USBHSH->PORT_CTRL &= ~USBHS_UH_SET_PORT_RESET;
                Delay_Ms(500);
                USBHSH->CFG |= USBHS_UH_SOF_EN;
            }

            /* Phase 2: Wait for phone to reconnect as AOA device.
             * Give it up to 30 seconds to complete the switch. */
            Serial_Dbg_Printf("[AOA] Waiting for AOA re-enumeration (30s timeout)...\r\n");
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
                    /* If device still supports AOA, try sending START_ACCESSORY again */
                    if (AOA_CheckSupport() == 0) {
                        Serial_Dbg_Printf("[AOA] Still supports AOA, re-sending START...\r\n");
                        AOA_SendStrings();
                        AOA_CtrlRequest(0x40, AOA_START_MODE, 0, 0, 0, NULL, NULL);
                        /* Wait for natural disconnect */
                        for (int d = 0; d < 30; d++) {
                            Delay_Ms(100);
                            if ((USBHSH->PORT_STATUS & USBHS_UHIS_PORT_CONNECT) == 0) {
                                Serial_Dbg_Printf("[AOA] Disconnected after re-START\r\n");
                                break;
                            }
                        }
                    }
                    RootHubDev[DEF_USBHS_PORT_INDEX].bStatus = ROOT_DEV_DISCONNECT;
                }
                Serial_Dbg_Printf("[AOA] Re-enum wait %d/30 PORT=0x%08lX\r\n",
                                  retry + 1, (unsigned long)USBHSH->PORT_STATUS);
            }

            Serial_Dbg_Printf("[AOA] Re-enumeration failed, forcing port reset...\r\n");
            USBHSH->PORT_STATUS_CHG = 0xff;
            USBHSH->PORT_CTRL |= USBHS_UH_SET_PORT_RESET;
            Delay_Ms(60);
            USBHSH->PORT_CTRL &= ~USBHS_UH_SET_PORT_RESET;
            Delay_Ms(500);
            USBHSH->CFG |= USBHS_UH_SOF_EN;
            Delay_Ms(2000);
            continue;
        }

        Delay_Ms(10);
        tick += 10;
        if (tick % 3000 == 0) {
            Serial_Dbg_Printf("[AOA] Waiting... (%lus) PORT=0x%08lX\r\n",
                              (unsigned long)(tick / 1000),
                              (unsigned long)USBHSH->PORT_STATUS);
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
    if ((USBHSH->PORT_STATUS & 0x02) == 0) {
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

        if ((USBHSH->PORT_STATUS & 0x02) == 0) {
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
    if ((USBHSH->PORT_STATUS & 0x02) == 0) return AOA_ERR_RX;

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
        if ((USBHSH->PORT_STATUS & 0x02) == 0) {
            Delay_Ms(50);
            if ((USBHSH->PORT_STATUS & 0x02) == 0) {
                Delay_Ms(100);
                if ((USBHSH->PORT_STATUS & 0x02) == 0) {
                    Serial_Dbg_Printf("[AOA] Port lost! State->DISCONNECTED\r\n");
                    s_aoa_state = AOA_STATE_DISCONNECTED;
                }
            }
        }
    }
}