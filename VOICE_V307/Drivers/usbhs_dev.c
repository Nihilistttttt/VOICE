#include "usbhs_dev.h"

/* 整个USBHS设备实现由usbhs_dev.h里的USBHS_DEBUG_EN开关控制,
 * 关闭时本文件编译为空, 不影响已有功能 */
#ifdef USBHS_DEBUG_EN

#include "sw_udisk.h"

volatile uint32_t USBHS_Int_Count = 0;
volatile uint32_t USBHS_Int_Setup = 0;
volatile uint32_t USBHS_Int_Trans = 0;
volatile uint32_t USBHS_Int_Rst = 0;
volatile uint32_t USBHS_Int_Detect = 0;
volatile uint32_t USBHS_Int_Susp = 0;
volatile uint8_t  USBHS_Dbg_RstSnap[8];
volatile uint8_t  USBHS_Dbg_RstIdx = 0;
volatile uint8_t  USBHS_Test_Flag;
__attribute__ ((aligned(4))) uint8_t IFTest_Buf[53] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xFE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD,
    0xFC, 0x7E, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0x7E
};

const uint8_t *pUSBHS_Descr;

volatile uint8_t  USBHS_SetupReqCode;
volatile uint8_t  USBHS_SetupReqType;
volatile uint16_t USBHS_SetupReqValue;
volatile uint16_t USBHS_SetupReqIndex;
volatile uint16_t USBHS_SetupReqLen;

volatile uint8_t  USBHS_DevConfig;
volatile uint8_t  USBHS_DevAddr;
volatile uint16_t USBHS_DevMaxPackLen;
volatile uint8_t  USBHS_DevSpeed;
volatile uint8_t  USBHS_DevSleepStatus;
volatile uint8_t  USBHS_DevEnumStatus;

__attribute__ ((aligned(4))) uint8_t USBHS_EP0_Buf[DEF_USBD_UEP0_SIZE];
__attribute__ ((aligned(4))) uint8_t UDisk_HS_In_Buf[DEF_UDISK_PACK_512];
__attribute__ ((aligned(4))) uint8_t UDisk_HS_Out_Buf[DEF_UDISK_PACK_512];

volatile uint8_t  USBHS_Endp_Busy[DEF_UEP_NUM];

void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void USB_TestMode_Deal(void)
{
    USBHS_Test_Flag &= ~0x80;
    if (USBHS_SetupReqIndex == 0x0100)
    {
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_J;
        USBHSD->CONTROL |= USBHS_UC_HOST_MODE;
    }
    else if (USBHS_SetupReqIndex == 0x0200)
    {
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_K;
        USBHSD->CONTROL |= USBHS_UC_HOST_MODE;
    }
    else if (USBHS_SetupReqIndex == 0x0300)
    {
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_PACKET;
        USBHSD->DEV_AD = 0x00;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_NAK;
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    }
    else if (USBHS_SetupReqIndex == 0x0400)
    {
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_PACKET;
        USBHSD->CONTROL |= USBHS_UC_HOST_MODE;
        USBHSH->HOST_EP_CONFIG = USBHS_UH_EP_TX_EN | USBHS_UH_EP_RX_EN;
        USBHSH->HOST_EP_TYPE |= 0xff;
        USBHSH->HOST_TX_DMA = (uint32_t)(&IFTest_Buf[0]);
        USBHSH->HOST_TX_LEN = 53;
        USBHSH->HOST_EP_PID = (USB_PID_SETUP << 4);
        USBHSH->INT_FG = USBHS_UIF_TRANSFER;
    }
}

void USBHS_Dev_RCC_Init(void)
{
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
}

void USBHS_Device_Endp_Init(void)
{
    USBHSD->ENDP_CONFIG = USBHS_UEP0_T_EN | USBHS_UEP0_R_EN | USBHS_UEP2_T_EN | USBHS_UEP3_R_EN;

    USBHSD->UEP0_MAX_LEN  = DEF_USBD_UEP0_SIZE;
    USBHSD->UEP2_MAX_LEN  = DEF_USB_EP2_HS_SIZE;
    USBHSD->UEP3_MAX_LEN  = DEF_USB_EP3_HS_SIZE;

    USBHSD->UEP0_DMA    = (uint32_t)(uint8_t *)USBHS_EP0_Buf;
    USBHSD->UEP2_TX_DMA = (uint32_t)(uint8_t *)UDisk_HS_In_Buf;
    USBHSD->UEP3_RX_DMA = (uint32_t)(uint8_t *)UDisk_HS_Out_Buf;

    USBHSD->UEP0_TX_LEN  = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;

    USBHSD->UEP2_TX_LEN  = 0;
    USBHSD->UEP2_TX_CTRL = USBHS_UEP_T_RES_NAK;

    USBHSD->UEP3_RX_CTRL = USBHS_UEP_R_RES_ACK;

    for (uint8_t i = 0; i < DEF_UEP_NUM; i++)
    {
        USBHS_Endp_Busy[i] = 0;
    }
}

void USBHS_Device_Init(FunctionalState sta)
{
    if (sta)
    {
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        Delay_Us(10);
        USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
        USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
        USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
        USBHSD->INT_EN = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER | USBHS_UIE_DETECT | USBHS_UIE_SUSPEND;
        USBHS_Device_Endp_Init();
        USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
        NVIC_EnableIRQ(USBHS_IRQn);
    }
    else
    {
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        Delay_Us(10);
        USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
        NVIC_DisableIRQ(USBHS_IRQn);
    }
}

uint8_t USBHS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod)
{
    uint8_t endp_buf_mode, endp_en, endp_tx_ctrl;

    if ((endp >= DEF_UEP1) && (endp <= DEF_UEP15))
    {
        endp_en = USBHSD->ENDP_CONFIG;
        if (endp_en & USBHSD_UEP_TX_EN(endp))
        {
            if ((USBHS_Endp_Busy[endp] & DEF_UEP_BUSY) == 0x00)
            {
                endp_buf_mode = USBHSD->BUF_MODE;
                if (endp_buf_mode & USBHSD_UEP_DOUBLE_BUF(endp))
                {
                    if ((endp_en & USBHSD_UEP_RX_EN(endp)) == 0x00)
                    {
                        endp_tx_ctrl = USBHSD_UEP_TXCTRL(endp);
                        if (mod == DEF_UEP_DMA_LOAD)
                        {
                            if ((endp_tx_ctrl & USBHS_UEP_T_TOG_DATA1) == 0)
                            {
                                USBHSD_UEP_TXDMA(endp) = (uint32_t)pbuf;
                            }
                            else
                            {
                                USBHSD_UEP_RXDMA(endp) = (uint32_t)pbuf;
                            }
                        }
                        else if (mod == DEF_UEP_CPY_LOAD)
                        {
                            if ((endp_tx_ctrl & USBHS_UEP_T_TOG_DATA1) == 0)
                            {
                                memcpy(USBHSD_UEP_TXBUF(endp), pbuf, len);
                            }
                            else
                            {
                                memcpy(USBHSD_UEP_RXBUF(endp), pbuf, len);
                            }
                        }
                        else
                        {
                            return 1;
                        }
                    }
                    else
                    {
                        return 1;
                    }
                }
                else
                {
                    if (mod == DEF_UEP_DMA_LOAD)
                    {
                        USBHSD_UEP_TXDMA(endp) = (uint32_t)pbuf;
                    }
                    else if (mod == DEF_UEP_CPY_LOAD)
                    {
                        memcpy(USBHSD_UEP_TXBUF(endp), pbuf, len);
                    }
                    else
                    {
                        return 1;
                    }
                }
                USBHS_Endp_Busy[endp] |= DEF_UEP_BUSY;
                USBHSD_UEP_TLEN(endp) = len;
                USBHSD_UEP_TXCTRL(endp) = (USBHSD_UEP_TXCTRL(endp) &= ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
            }
            else
            {
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }
    else
    {
        return 1;
    }

    return 0;
}

void USBHS_IRQHandler(void)
{
    uint8_t  intflag, intst, errflag;
    uint16_t len;

    USBHS_Int_Count++;
    intflag = USBHSD->INT_FG;
    intst = USBHSD->INT_ST;
    if (intflag & USBHS_UIF_TRANSFER)
    {
        USBHS_Int_Trans++;
        switch (intst & USBHS_UIS_TOKEN_MASK)
        {
            case USBHS_UIS_TOKEN_IN:
                switch (intst & (USBHS_UIS_TOKEN_MASK | USBHS_UIS_ENDP_MASK))
                {
                    case USBHS_UIS_TOKEN_IN | DEF_UEP0:
                        if (USBHS_SetupReqLen == 0)
                        {
                            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
                        }
                        if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
                        {
                        }
                        else
                        {
                            switch (USBHS_SetupReqCode)
                            {
                                case USB_GET_DESCRIPTOR:
                                    len = USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                                    memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
                                    USBHS_SetupReqLen -= len;
                                    pUSBHS_Descr += len;
                                    USBHSD->UEP0_TX_LEN = len;
                                    USBHSD->UEP0_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                                    break;

                                case USB_SET_ADDRESS:
                                    USBHSD->DEV_AD = USBHS_DevAddr;
                                    break;

                                default:
                                    USBHSD->UEP0_TX_LEN = 0;
                                    break;
                            }
                        }
                        if (USBHS_Test_Flag & 0x80)
                        {
                            USB_TestMode_Deal();
                        }
                        break;

                    case USBHS_UIS_TOKEN_IN | DEF_UEP2:
                        USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                        USBHSD->UEP2_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                        USBHS_Endp_Busy[DEF_UEP2] &= ~DEF_UEP_BUSY;
                        UDISK_In_EP_Deal();
                        break;

                    default:
                        break;
                }
                break;

            case USBHS_UIS_TOKEN_OUT:
                switch (intst & (USBHS_UIS_TOKEN_MASK | USBHS_UIS_ENDP_MASK))
                {
                    case USBHS_UIS_TOKEN_OUT | DEF_UEP0:
                        len = USBHSH->RX_LEN;
                        if (intst & USBHS_UIS_TOG_OK)
                        {
                            if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
                            {
                            }
                            else
                            {
                            }
                            USBHS_SetupReqLen -= len;
                            if (USBHS_SetupReqLen == 0)
                            {
                                USBHSD->UEP0_TX_LEN  = 0;
                                USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                            }
                        }
                        break;

                    case USBHS_UIS_TOKEN_OUT | DEF_UEP3:
                        if (intst & USBHS_UIS_TOG_OK)
                        {
                            len = (uint16_t)(USBHSD->RX_LEN);
                            USBHSD->UEP3_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                            USBHSD->UEP3_RX_CTRL = ((USBHSD->UEP3_RX_CTRL) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                            UDISK_Out_EP_Deal(UDisk_HS_Out_Buf, len);
                            USBHSD->UEP3_RX_CTRL = ((USBHSD->UEP3_RX_CTRL) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
                        }
                        break;

                    default:
                        errflag = 0xFF;
                        break;
                }
                break;

            case USBHS_UIS_TOKEN_SOF:
                break;

            default:
                break;
        }
        USBHSD->INT_FG = USBHS_UIF_TRANSFER;
    }
    else if (intflag & USBHS_UIF_SETUP_ACT)
    {
        USBHS_Int_Setup++;
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;

        USBHS_SetupReqType  = pUSBHS_SetupReqPak->bRequestType;
        USBHS_SetupReqCode  = pUSBHS_SetupReqPak->bRequest;
        USBHS_SetupReqLen   = pUSBHS_SetupReqPak->wLength;
        USBHS_SetupReqValue = pUSBHS_SetupReqPak->wValue;
        USBHS_SetupReqIndex = pUSBHS_SetupReqPak->wIndex;

        len = 0;
        errflag = 0;
        if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
        {
            if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
            {
                if (USBHS_SetupReqCode == CMD_UDISK_GET_MAX_LUN)
                {
                    USBHS_EP0_Buf[0] = 0;
                    pUSBHS_Descr = (uint8_t*)USBHS_EP0_Buf;
                    len = 1;
                }
                else if (USBHS_SetupReqCode == CMD_UDISK_RESET)
                {
                    Udisk_Sense_Key = 0x00;
                    Udisk_Sense_ASC = 0x00;
                    Udisk_CSW_Status = 0x00;
                    Udisk_Transfer_Status = 0x00;
                    UDISK_Transfer_DataLen = 0x00;
                }
                else
                {
                    errflag = 0xFF;
                }
            }
            else
            {
                errflag = 0xFF;
            }
        }
        else
        {
            switch (USBHS_SetupReqCode)
            {
                case USB_GET_DESCRIPTOR:
                    switch ((uint8_t)(USBHS_SetupReqValue >> 8))
                    {
                        case USB_DESCR_TYP_DEVICE:
                            pUSBHS_Descr = MyDevDescr;
                            len = DEF_USBD_DEVICE_DESC_LEN;
                            break;

                        case USB_DESCR_TYP_CONFIG:
                            if ((USBHSD->SPEED_TYPE & USBHS_SPEED_TYPE_MASK) == USBHS_SPEED_HIGH)
                            {
                                USBHS_DevSpeed = USBHS_SPEED_HIGH;
                                USBHS_DevMaxPackLen = DEF_USBD_HS_PACK_SIZE;
                                UDISK_Pack_Size = DEF_UDISK_PACK_512;
                            }
                            else
                            {
                                USBHS_DevSpeed = USBHS_SPEED_FULL;
                                USBHS_DevMaxPackLen = DEF_USBD_FS_PACK_SIZE;
                                UDISK_Pack_Size = DEF_UDISK_PACK_64;
                            }
                            if (USBHS_DevSpeed == USBHS_SPEED_HIGH)
                            {
                                pUSBHS_Descr = MyCfgDescr_HS;
                                len = DEF_USBD_CONFIG_HS_DESC_LEN;
                            }
                            else
                            {
                                pUSBHS_Descr = MyCfgDescr_FS;
                                len = DEF_USBD_CONFIG_FS_DESC_LEN;
                            }
                            break;

                        case USB_DESCR_TYP_STRING:
                            switch ((uint8_t)(USBHS_SetupReqValue & 0xFF))
                            {
                                case DEF_STRING_DESC_LANG:
                                    pUSBHS_Descr = MyLangDescr;
                                    len = DEF_USBD_LANG_DESC_LEN;
                                    break;
                                case DEF_STRING_DESC_MANU:
                                    pUSBHS_Descr = MyManuInfo;
                                    len = DEF_USBD_MANU_DESC_LEN;
                                    break;
                                case DEF_STRING_DESC_PROD:
                                    pUSBHS_Descr = MyProdInfo;
                                    len = DEF_USBD_PROD_DESC_LEN;
                                    break;
                                case DEF_STRING_DESC_SERN:
                                    pUSBHS_Descr = MySerNumInfo;
                                    len = DEF_USBD_SN_DESC_LEN;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                            break;

                        case USB_DESCR_TYP_QUALIF:
                            pUSBHS_Descr = MyQuaDesc;
                            len = DEF_USBD_QUALFY_DESC_LEN;
                            break;

                        case USB_DESCR_TYP_BOS:
                            errflag = 0xFF;
                            break;

                        case USB_DESCR_TYP_SPEED:
                            if (USBHS_DevSpeed == USBHS_SPEED_HIGH)
                            {
                                memcpy(&TAB_USB_HS_OSC_DESC[2], &MyCfgDescr_FS[2], DEF_USBD_CONFIG_FS_DESC_LEN - 2);
                                pUSBHS_Descr = (uint8_t *)&TAB_USB_HS_OSC_DESC[0];
                                len = DEF_USBD_CONFIG_FS_DESC_LEN;
                            }
                            else if (USBHS_DevSpeed == USBHS_SPEED_FULL)
                            {
                                memcpy(&TAB_USB_FS_OSC_DESC[2], &MyCfgDescr_HS[2], DEF_USBD_CONFIG_HS_DESC_LEN - 2);
                                pUSBHS_Descr = (uint8_t *)&TAB_USB_FS_OSC_DESC[0];
                                len = DEF_USBD_CONFIG_HS_DESC_LEN;
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                            break;

                        default:
                            errflag = 0xFF;
                            break;
                    }
                    if (USBHS_SetupReqLen > len)
                    {
                        USBHS_SetupReqLen = len;
                    }
                    len = (USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                    memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
                    pUSBHS_Descr += len;
                    break;

                case USB_SET_ADDRESS:
                    USBHS_DevAddr = (uint16_t)(USBHS_SetupReqValue & 0xFF);
                    break;

                case USB_GET_CONFIGURATION:
                    USBHS_EP0_Buf[0] = USBHS_DevConfig;
                    if (USBHS_SetupReqLen > 1)
                    {
                        USBHS_SetupReqLen = 1;
                    }
                    break;

                case USB_SET_CONFIGURATION:
                    USBHS_DevConfig = (uint8_t)(USBHS_SetupReqValue & 0xFF);
                    USBHS_DevEnumStatus = 0x01;
                    break;

                case USB_CLEAR_FEATURE:
                    if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                    {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == 0x01)
                        {
                            USBHS_DevSleepStatus &= ~0x01;
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                    }
                    else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT)
                        {
                            switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF))
                            {
                                case (DEF_UEP2 | DEF_UEP_IN):
                                    USBHSD->UEP2_TX_CTRL = USBHS_UEP_T_RES_NAK;
                                    if (Udisk_Transfer_Status & DEF_UDISK_CSW_UP_FLAG)
                                    {
                                        UDISK_Up_CSW();
                                    }
                                    break;
                                case (DEF_UEP3 | DEF_UEP_OUT):
                                    USBHSD->UEP3_RX_CTRL = USBHS_UEP_R_RES_ACK;
                                    if (Udisk_Transfer_Status & DEF_UDISK_CSW_UP_FLAG)
                                    {
                                        UDISK_Up_CSW();
                                    }
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                    }
                    else
                    {
                        errflag = 0xFF;
                    }
                    break;

                case USB_SET_FEATURE:
                    if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                    {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP)
                        {
                            if (MyCfgDescr_FS[7] & 0x20)
                            {
                                USBHS_DevSleepStatus |= 0x01;
                            }
                            else
                            {
                                errflag = 0xFF;
                            }
                        }
                        else if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == 0x02)
                        {
                            if ((USBHS_SetupReqIndex == 0x0100) ||
                                (USBHS_SetupReqIndex == 0x0200) ||
                                (USBHS_SetupReqIndex == 0x0300) ||
                                (USBHS_SetupReqIndex == 0x0400))
                            {
                                USBHS_Test_Flag |= 0x80;
                            }
                        }
                        else
                        {
                            errflag = 0xFF;
                        }
                    }
                    else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT)
                        {
                            switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF))
                            {
                                case (DEF_UEP2 | DEF_UEP_IN):
                                    USBHSD->UEP2_TX_CTRL = (USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
                                    break;
                                case (DEF_UEP3 | DEF_UEP_OUT):
                                    USBHSD->UEP3_RX_CTRL = (USBHSD->UEP3_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL;
                                    break;
                                default:
                                    errflag = 0xFF;
                                    break;
                            }
                        }
                    }
                    break;

                case USB_GET_INTERFACE:
                    USBHS_EP0_Buf[0] = 0x00;
                    if (USBHS_SetupReqLen > 1)
                    {
                        USBHS_SetupReqLen = 1;
                    }
                    break;

                case USB_SET_INTERFACE:
                    break;

                case USB_GET_STATUS:
                    USBHS_EP0_Buf[0] = 0x00;
                    USBHS_EP0_Buf[1] = 0x00;
                    if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                    {
                        switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF))
                        {
                            case (DEF_UEP_IN | DEF_UEP2):
                                if (((USBHSD->UEP2_TX_CTRL) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
                                {
                                    USBHS_EP0_Buf[0] = 0x01;
                                }
                                break;
                            case (DEF_UEP_OUT | DEF_UEP3):
                                if (((USBHSD->UEP3_RX_CTRL) & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL)
                                {
                                    USBHS_EP0_Buf[0] = 0x01;
                                }
                                break;
                            default:
                                errflag = 0xFF;
                                break;
                        }
                    }
                    else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                    {
                        if (USBHS_DevSleepStatus & 0x01)
                        {
                            USBHS_EP0_Buf[0] = 0x02;
                        }
                    }
                    if (USBHS_SetupReqLen > 2)
                    {
                        USBHS_SetupReqLen = 2;
                    }
                    break;

                default:
                    errflag = 0xFF;
                    break;
            }
        }

        if (errflag == 0xFF)
        {
            USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_STALL;
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_STALL;
        }
        else
        {
            if (USBHS_SetupReqType & DEF_UEP_IN)
            {
                len = (USBHS_SetupReqLen > DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                USBHS_SetupReqLen -= len;
                USBHSD->UEP0_TX_LEN = len;
                USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
            }
            else
            {
                if (USBHS_SetupReqLen == 0)
                {
                    USBHSD->UEP0_TX_LEN = 0;
                    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                }
                else
                {
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
                }
            }
        }
        USBHSD->INT_FG = USBHS_UIF_SETUP_ACT;
    }
    else if (intflag & USBHS_UIF_BUS_RST)
    {
        USBHS_Int_Rst++;
        /* 记录本次复位时PHY协商的速度快照(0=FS 1=HS 2=LS), 主循环串口打印 */
        USBHS_Dbg_RstSnap[USBHS_Dbg_RstIdx & 7] = USBHSD->SPEED_TYPE & USBHS_SPEED_TYPE_MASK;
        USBHS_Dbg_RstIdx++;
        USBHS_DevConfig = 0;
        USBHS_DevAddr = 0;
        USBHS_DevSleepStatus = 0;
        USBHS_DevEnumStatus = 0;

        USBHSD->DEV_AD = 0;
        USBHS_Device_Endp_Init();
        USBHSD->INT_FG = USBHS_UIF_BUS_RST;
    }
    else if (intflag & USBHS_UIF_SUSPEND)
    {
        USBHS_Int_Susp++;
        USBHSD->INT_FG = USBHS_UIF_SUSPEND;
        Delay_Us(10);
        if (USBHSD->MIS_ST & USBHS_UMS_SUSPEND)
        {
            USBHS_DevSleepStatus |= 0x02;
            if (USBHS_DevSleepStatus == 0x03)
            {
            }
        }
        else
        {
            USBHS_DevSleepStatus &= ~0x02;
        }
    }
    else if (intflag & USBHS_UIF_DETECT)
    {
        USBHS_Int_Detect++;
        USBHSD->INT_FG = USBHS_UIF_DETECT;
    }
    else
    {
        USBHSD->INT_FG = intflag;
    }
}
#endif /* USBHS_DEBUG_EN */
