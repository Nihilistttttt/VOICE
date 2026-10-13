#include "ch32v30x_usbfs_device.h"
#include "sw_udisk.h"
#include "sdio_sd.h"
#include "string.h"

__attribute__ ((aligned(4))) uint8_t UDisk_Down_Buffer[DEF_UDISK_SECTOR_SIZE];
__attribute__ ((aligned(4))) uint8_t UDisk_Sector_Buffer[DEF_UDISK_SECTOR_SIZE];
__attribute__ ((aligned(4))) uint8_t UDisk_Pack_Buffer[DEF_UDISK_PACK_64];

uint8_t UDISK_Inquity_Tab[] =
{
    0x00,
    0x80,
    0x02,
    0x02,
    0x1F,
    0x00,
    0x00,
    0x00,
    'V', 'O', 'I', 'C', 'E', ' ', ' ', ' ',
    'S', 'D', ' ', 'U', 'D', 'i', 's', 'k',
    '1', '.', '0', '0'
};

uint8_t const UDISK_Rd_Format_Capacity[] =
{
    0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00,
    0x02,
    (DEF_CFG_DISK_SEC_SIZE >> 16) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 8) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE) & 0xFF,
};

uint8_t const UDISK_Rd_Capacity[] =
{
    0x00, 0x00, 0x00, 0x00,
    (DEF_CFG_DISK_SEC_SIZE >> 24) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 16) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 8) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE) & 0xFF,
};

uint8_t const UDISK_Mode_Sense_1A[] =
{
    0x0B, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00,
    (DEF_CFG_DISK_SEC_SIZE >> 24) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 16) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 8) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE) & 0xFF,
};

uint8_t const UDISK_Mode_Senese_5A[] =
{
    0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00,
    (DEF_CFG_DISK_SEC_SIZE >> 24) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 16) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE >> 8) & 0xFF,
    (DEF_CFG_DISK_SEC_SIZE) & 0xFF,
};

volatile uint8_t  Udisk_Status = 0x00;
volatile uint8_t  Udisk_Transfer_Status = 0x00;
volatile uint32_t Udisk_Capability = 0x00;
volatile uint8_t  Udisk_CBW_Tag_Save[4];
volatile uint8_t  Udisk_Sense_Key = 0x00;
volatile uint8_t  Udisk_Sense_ASC = 0x00;
volatile uint8_t  Udisk_CSW_Status = 0x00;

volatile uint32_t UDISK_Transfer_DataLen = 0x00;
volatile uint32_t UDISK_Cur_Sec_Lba = 0x00;
volatile uint16_t UDISK_Sec_Pack_Count = 0x00;
volatile uint16_t UDISK_Pack_Size = DEF_UDISK_PACK_64;

BULK_ONLY_CMD mBOC;
uint8_t *pEndp2_Buf;

void UDISK_CMD_Deal_Status(uint8_t key, uint8_t asc, uint8_t status)
{
    Udisk_Sense_Key  = key;
    Udisk_Sense_ASC  = asc;
    Udisk_CSW_Status = status;
}

void UDISK_CMD_Deal_Fail(void)
{
    if (Udisk_Transfer_Status & DEF_UDISK_BLUCK_UP_FLAG)
    {
        USBFSD->UEP2_TX_CTRL = (USBFSD->UEP2_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_STALL;
        Udisk_Transfer_Status &= ~DEF_UDISK_BLUCK_UP_FLAG;
    }
    if (Udisk_Transfer_Status & DEF_UDISK_BLUCK_DOWN_FLAG)
    {
        USBFSD->UEP3_RX_CTRL = (USBFSD->UEP3_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_STALL;
        Udisk_Transfer_Status &= ~DEF_UDISK_BLUCK_DOWN_FLAG;
    }
}

void CMD_RD_WR_Deal_Pre(void)
{
    UDISK_Cur_Sec_Lba = (uint32_t)mBOC.mCBW.mCBW_CB_Buf[2] << 24;
    UDISK_Cur_Sec_Lba += ((uint32_t)mBOC.mCBW.mCBW_CB_Buf[3] << 16);
    UDISK_Cur_Sec_Lba += ((uint32_t)mBOC.mCBW.mCBW_CB_Buf[4] << 8);
    UDISK_Cur_Sec_Lba += ((uint32_t)mBOC.mCBW.mCBW_CB_Buf[5]);

    UDISK_Transfer_DataLen = ((uint32_t)mBOC.mCBW.mCBW_CB_Buf[7] << 8);
    UDISK_Transfer_DataLen += ((uint32_t)mBOC.mCBW.mCBW_CB_Buf[8]);
    UDISK_Transfer_DataLen *= DEF_UDISK_SECTOR_SIZE;

    UDISK_Sec_Pack_Count = 0x00;
    UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
}

void UDISK_SCSI_CMD_Deal(void)
{
    uint8_t i;

    if ((mBOC.mCBW.mCBW_Sig[0] == 'U') && (mBOC.mCBW.mCBW_Sig[1] == 'S')
      &&(mBOC.mCBW.mCBW_Sig[2] == 'B') && (mBOC.mCBW.mCBW_Sig[3] == 'C'))
    {
        Udisk_CBW_Tag_Save[0] = mBOC.mCBW.mCBW_Tag[0];
        Udisk_CBW_Tag_Save[1] = mBOC.mCBW.mCBW_Tag[1];
        Udisk_CBW_Tag_Save[2] = mBOC.mCBW.mCBW_Tag[2];
        Udisk_CBW_Tag_Save[3] = mBOC.mCBW.mCBW_Tag[3];

        UDISK_Transfer_DataLen = (uint32_t)mBOC.mCBW.mCBW_DataLen[3] << 24;
        UDISK_Transfer_DataLen += ((uint32_t)mBOC.mCBW.mCBW_DataLen[2] << 16);
        UDISK_Transfer_DataLen += ((uint32_t)mBOC.mCBW.mCBW_DataLen[1] << 8);
        UDISK_Transfer_DataLen += ((uint32_t)mBOC.mCBW.mCBW_DataLen[0]);

        if (UDISK_Transfer_DataLen)
        {
            if (mBOC.mCBW.mCBW_Flag & 0x80)
            {
                Udisk_Transfer_Status |= DEF_UDISK_BLUCK_UP_FLAG;
            }
            else
            {
                Udisk_Transfer_Status |= DEF_UDISK_BLUCK_DOWN_FLAG;
            }
        }
        Udisk_Transfer_Status |= DEF_UDISK_CSW_UP_FLAG;

        switch (mBOC.mCBW.mCBW_CB_Buf[0])
        {
            case CMD_U_INQUIRY:
                if (UDISK_Transfer_DataLen > 0x24)
                {
                    UDISK_Transfer_DataLen = 0x24;
                }
                UDISK_Inquity_Tab[0] = 0x00;
                pEndp2_Buf = (uint8_t *)UDISK_Inquity_Tab;
                UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                break;

            case CMD_U_READ_FORMAT_CAPACITY:
                if ((Udisk_Status & DEF_UDISK_EN_FLAG))
                {
                    if (UDISK_Transfer_DataLen > 0x0C)
                    {
                        UDISK_Transfer_DataLen = 0x0C;
                    }
                    for (i = 0x00; i < UDISK_Transfer_DataLen; i++)
                    {
                        mBOC.buf[i] = UDISK_Rd_Format_Capacity[i];
                    }
                    mBOC.buf[4] = ((Udisk_Capability >> 24) & 0xFF);
                    mBOC.buf[5] = ((Udisk_Capability >> 16) & 0xFF);
                    mBOC.buf[6] = ((Udisk_Capability >> 8) & 0xFF);
                    mBOC.buf[7] = ((Udisk_Capability) & 0xFF);
                    pEndp2_Buf = mBOC.buf;
                    UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x02, 0x3A, 0x01);
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_READ_CAPACITY:
                if ((Udisk_Status & DEF_UDISK_EN_FLAG))
                {
                    if (UDISK_Transfer_DataLen > 0x08)
                    {
                        UDISK_Transfer_DataLen = 0x08;
                    }
                    for (i = 0x00; i < UDISK_Transfer_DataLen; i++)
                    {
                        mBOC.buf[i] = UDISK_Rd_Capacity[i];
                    }
                    mBOC.buf[0] = ((Udisk_Capability - 1) >> 24) & 0xFF;
                    mBOC.buf[1] = ((Udisk_Capability - 1) >> 16) & 0xFF;
                    mBOC.buf[2] = ((Udisk_Capability - 1) >> 8) & 0xFF;
                    mBOC.buf[3] = ((Udisk_Capability - 1)) & 0xFF;
                    pEndp2_Buf = mBOC.buf;
                    UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x02, 0x3A, 0x01);
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_READ10:
                if ((Udisk_Status & DEF_UDISK_EN_FLAG))
                {
                    CMD_RD_WR_Deal_Pre();
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x02, 0x3A, 0x01);
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_WR_VERIFY10:
            case CMD_U_WRITE10:
                if (Udisk_Status & DEF_UDISK_EN_FLAG)
                {
                    CMD_RD_WR_Deal_Pre();
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x02, 0x3A, 0x01);
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_MODE_SENSE:
                if ((Udisk_Status & DEF_UDISK_EN_FLAG))
                {
                    if (UDISK_Transfer_DataLen > 0x0C)
                    {
                        UDISK_Transfer_DataLen = 0x0C;
                    }
                    for (i = 0x00; i < UDISK_Transfer_DataLen; i++)
                    {
                        mBOC.buf[i] = UDISK_Mode_Sense_1A[i];
                    }
                    mBOC.buf[4] = (Udisk_Capability >> 24) & 0xFF;
                    mBOC.buf[5] = (Udisk_Capability >> 16) & 0xFF;
                    mBOC.buf[6] = (Udisk_Capability >> 8) & 0xFF;
                    mBOC.buf[7] = (Udisk_Capability) & 0xFF;
                    pEndp2_Buf = mBOC.buf;
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x02, 0x3A, 0x01);
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_MODE_SENSE2:
                if (mBOC.mCBW.mCBW_CB_Buf[2] == 0x3F)
                {
                    if (UDISK_Transfer_DataLen > 0x10)
                    {
                        UDISK_Transfer_DataLen = 0x10;
                    }
                    for (i = 0x00; i < UDISK_Transfer_DataLen; i++)
                    {
                        mBOC.buf[i] = UDISK_Mode_Senese_5A[i];
                    }
                    mBOC.buf[8]  = (Udisk_Capability >> 24) & 0xFF;
                    mBOC.buf[9]  = (Udisk_Capability >> 16) & 0xFF;
                    mBOC.buf[10] = (Udisk_Capability >> 8) & 0xFF;
                    mBOC.buf[11] = (Udisk_Capability) & 0xFF;
                    pEndp2_Buf = mBOC.buf;
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x05, 0x20, 0x01);
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_REQUEST_SENSE:
                mBOC.ReqSense.ErrorCode = 0x70;
                mBOC.ReqSense.Reserved1 = 0x00;
                mBOC.ReqSense.SenseKey  = Udisk_Sense_Key;
                mBOC.ReqSense.Information[0] = 0x00;
                mBOC.ReqSense.Information[1] = 0x00;
                mBOC.ReqSense.Information[2] = 0x00;
                mBOC.ReqSense.Information[3] = 0x00;
                mBOC.ReqSense.SenseLength = 0x0A;
                mBOC.ReqSense.Reserved2[0] = 0x00;
                mBOC.ReqSense.Reserved2[1] = 0x00;
                mBOC.ReqSense.Reserved2[2] = 0x00;
                mBOC.ReqSense.Reserved2[3] = 0x00;
                mBOC.ReqSense.SenseCode = Udisk_Sense_ASC;
                mBOC.ReqSense.SenseCodeQua = 0x00;
                mBOC.ReqSense.Reserved3[0] = 0x00;
                mBOC.ReqSense.Reserved3[1] = 0x00;
                mBOC.ReqSense.Reserved3[2] = 0x00;
                mBOC.ReqSense.Reserved3[3] = 0x00;
                pEndp2_Buf = mBOC.buf;
                Udisk_CSW_Status = 0x00;
                break;

            case CMD_U_TEST_READY:
                if (Udisk_Status & DEF_UDISK_EN_FLAG)
                {
                    UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                }
                else
                {
                    UDISK_CMD_Deal_Status(0x02, 0x3A, 0x01);
                    Udisk_Transfer_Status |= DEF_UDISK_BLUCK_UP_FLAG;
                    UDISK_CMD_Deal_Fail();
                }
                break;

            case CMD_U_PREVT_REMOVE:
                UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                break;

            case CMD_U_VERIFY10:
                UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                break;

            case CMD_U_START_STOP:
                UDISK_CMD_Deal_Status(0x00, 0x00, 0x00);
                break;

            default:
                UDISK_CMD_Deal_Status(0x05, 0x20, 0x01);
                Udisk_Transfer_Status |= DEF_UDISK_BLUCK_UP_FLAG;
                UDISK_CMD_Deal_Fail();
                break;
        }
    }
    else
    {
        UDISK_CMD_Deal_Status(0x05, 0x20, 0x02);
        Udisk_Transfer_Status |= DEF_UDISK_BLUCK_UP_FLAG;
        Udisk_Transfer_Status |= DEF_UDISK_BLUCK_DOWN_FLAG;
        UDISK_CMD_Deal_Fail();
    }
}

void UDISK_In_EP_Deal(void)
{
    if (Udisk_Transfer_Status & DEF_UDISK_BLUCK_UP_FLAG)
    {
        if (mBOC.mCBW.mCBW_CB_Buf[0] == CMD_U_READ10)
        {
            UDISK_Up_OnePack();
        }
        else
        {
            UDISK_Bulk_UpData();
        }
    }
    else if (Udisk_Transfer_Status & DEF_UDISK_CSW_UP_FLAG)
    {
        UDISK_Up_CSW();
    }
}

void UDISK_Out_EP_Deal(uint8_t *pbuf, uint16_t packlen)
{
    uint32_t i;
    if (Udisk_Transfer_Status & DEF_UDISK_BLUCK_DOWN_FLAG)
    {
        UDISK_Down_OnePack(pbuf, packlen);
    }
    else
    {
        if (packlen == 0x1F)
        {
            for (i = 0; i < packlen; i++)
            {
                mBOC.buf[i] = *pbuf++;
            }
            UDISK_SCSI_CMD_Deal();
            if ((Udisk_Transfer_Status & DEF_UDISK_BLUCK_DOWN_FLAG) == 0x00)
            {
                if (Udisk_Transfer_Status & DEF_UDISK_BLUCK_UP_FLAG)
                {
                    if (mBOC.mCBW.mCBW_CB_Buf[0] == CMD_U_READ10)
                    {
                        UDISK_Up_OnePack();
                    }
                    else
                    {
                        UDISK_Bulk_UpData();
                    }
                }
                else if (Udisk_CSW_Status == 0x00)
                {
                    UDISK_Up_CSW();
                }
            }
        }
    }
}

void UDISK_Bulk_UpData(void)
{
    uint32_t len;

    if (UDISK_Transfer_DataLen > UDISK_Pack_Size)
    {
        len = UDISK_Pack_Size;
        UDISK_Transfer_DataLen -= UDISK_Pack_Size;
    }
    else
    {
        len = UDISK_Transfer_DataLen;
        UDISK_Transfer_DataLen = 0x00;
        Udisk_Transfer_Status &= ~DEF_UDISK_BLUCK_UP_FLAG;
    }

    USBFS_Endp_DataUp(DEF_UEP2, pEndp2_Buf, len, DEF_UEP_CPY_LOAD);
}

void UDISK_Up_CSW(void)
{
    Udisk_Transfer_Status = 0x00;

    mBOC.mCSW.mCSW_Sig[0] = 'U';
    mBOC.mCSW.mCSW_Sig[1] = 'S';
    mBOC.mCSW.mCSW_Sig[2] = 'B';
    mBOC.mCSW.mCSW_Sig[3] = 'S';
    mBOC.mCSW.mCSW_Tag[0] = Udisk_CBW_Tag_Save[0];
    mBOC.mCSW.mCSW_Tag[1] = Udisk_CBW_Tag_Save[1];
    mBOC.mCSW.mCSW_Tag[2] = Udisk_CBW_Tag_Save[2];
    mBOC.mCSW.mCSW_Tag[3] = Udisk_CBW_Tag_Save[3];
    mBOC.mCSW.mCSW_Residue[0] = 0x00;
    mBOC.mCSW.mCSW_Residue[1] = 0x00;
    mBOC.mCSW.mCSW_Residue[2] = 0x00;
    mBOC.mCSW.mCSW_Residue[3] = 0x00;
    mBOC.mCSW.mCSW_Status = Udisk_CSW_Status;

    USBFS_Endp_DataUp(DEF_UEP2, (uint8_t *)mBOC.buf, 0x0D, DEF_UEP_CPY_LOAD);
}

void UDISK_Up_OnePack(void)
{
    uint8_t *pbuf = NULL;

    if (UDISK_Sec_Pack_Count == 0x00)
    {
        if (SD_ReadDisk(UDisk_Sector_Buffer, UDISK_Cur_Sec_Lba, 1) != SD_OK)
        {
            UDISK_CMD_Deal_Status(0x03, 0x11, 0x01);
        }
    }
    pbuf = UDisk_Sector_Buffer + (uint32_t)UDISK_Sec_Pack_Count * UDISK_Pack_Size;

    USBFS_Endp_DataUp(DEF_UEP2, pbuf, UDISK_Pack_Size, DEF_UEP_CPY_LOAD);

    UDISK_Sec_Pack_Count++;
    UDISK_Transfer_DataLen -= UDISK_Pack_Size;

    if (UDISK_Sec_Pack_Count == (DEF_UDISK_SECTOR_SIZE / UDISK_Pack_Size))
    {
        UDISK_Sec_Pack_Count = 0x00;
        UDISK_Cur_Sec_Lba++;
    }
    if (UDISK_Transfer_DataLen == 0x00)
    {
        Udisk_Transfer_Status &= ~DEF_UDISK_BLUCK_UP_FLAG;
    }
}

void UDISK_Down_OnePack(uint8_t *pbuf, uint16_t packlen)
{
    memcpy(UDisk_Down_Buffer + (uint32_t)UDISK_Sec_Pack_Count * UDISK_Pack_Size, pbuf, UDISK_Pack_Size);
    UDISK_Sec_Pack_Count++;
    UDISK_Transfer_DataLen -= UDISK_Pack_Size;

    if (UDISK_Sec_Pack_Count == (DEF_UDISK_SECTOR_SIZE / UDISK_Pack_Size))
    {
        if (SD_WriteDisk(UDisk_Down_Buffer, UDISK_Cur_Sec_Lba, 1) != SD_OK)
        {
            UDISK_CMD_Deal_Status(0x03, 0x03, 0x01);
        }

        if (UDISK_Transfer_DataLen == 0x00)
        {
            Udisk_Transfer_Status &= ~DEF_UDISK_BLUCK_DOWN_FLAG;
            UDISK_Up_CSW();
        }
        UDISK_Sec_Pack_Count = 0x00;
        UDISK_Cur_Sec_Lba++;
    }
}