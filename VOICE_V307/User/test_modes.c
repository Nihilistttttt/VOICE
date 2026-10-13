/********************************** (C) COPYRIGHT *******************************
* File Name          : test_modes.c
* Description        : 各 TEST_MODE 的完整实现。
*
*                      代码原样来自 User/main.c 的 TEST_MODE 分支（纯搬迁，未改逻辑），
*                      只把「内联在 main() 里的模式体」改成独立函数。
*                      这样 main.c 只做启动和分发，读入口时不用先读完一个功能的实现。
*
*                      每段都被 #if TEST_MODE == n 整段包住 ——
*                      任何一次构建里只有一个模式函数有内容，其余都是空翻译单元，
*                      不存在「编进来但没被调用」的外部函数。
*******************************************************************************/

#include "debug.h"
#include "Config.h"
#include "test_modes.h"

/* ---- 按模式引入各自的依赖头 -------------------------------------------- */
#if TEST_MODE == 1
#include "sdio_test.h"
#elif TEST_MODE == 2
#include "sdio_sd.h"
#include "sw_udisk.h"
#include "ch32v30x_usbfs_device.h"
#include "OLED.h"
#elif TEST_MODE == 3
#include "sdio_sd.h"
#include "ff.h"
#include "OLED.h"
#elif TEST_MODE == 4
#include "usbhs_dev.h"
#include "OLED.h"
#if USBHS_DEBUG_EN == 0
#error "USBHS调试模式需在usbhs_dev.h里将USBHS_DEBUG_EN置1"
#endif
#elif TEST_MODE == 5
#include "ch32v30x_usbfs_device.h"
#include "joystick.h"
#elif TEST_MODE == 0
#include "phone_control.h"
#endif

#if TEST_MODE == 3
static FATFS s_fatfs;
static FIL   s_fil;
static u8    s_buf[1024];
#endif

/*============================================================================
 * TEST_MODE 1 —— SDIO SD 卡初始化 + 读写速率基准
 *============================================================================*/
#if TEST_MODE == 1
void Mode_SdioBench(void)
{
    printf("=== SDIO SD Card Test Mode ===\r\n");
    SDIO_Test_Run();
    while (1);
}
#endif

/*============================================================================
 * TEST_MODE 2 —— USB MSC U 盘：SD 卡经 USBFS Device 暴露给电脑
 *============================================================================*/
#if TEST_MODE == 2
void Mode_Udisk(void)
{
    SD_Error sd_err;

    printf("=== USB MSC U-Disk Mode ===\r\n");

    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "MSC U-Disk");
    OLED_Refresh();

    sd_err = SD_Init();
    if (sd_err != SD_OK) {
        printf("SD_Init failed: %d\r\n", sd_err);
        OLED_ShowString(1, 0, "SD FAIL");
        OLED_Refresh();
        while (1);
    }
    printf("SD_Init OK\r\n");
    printf("Cap: %d MB\r\n", (u32)(SDCardInfo.CardCapacity >> 20));

    Udisk_Capability = SDCardInfo.CardCapacity / 512;
    Udisk_Status |= DEF_UDISK_EN_FLAG;

    OLED_ShowString(1, 0, "SD OK");
    OLED_ShowNum(2, 0, (u32)(SDCardInfo.CardCapacity >> 20), 5);
    OLED_ShowString(2, 6, "MB");
    OLED_Refresh();

    /* U盘模式: SD卡切到POLLING模式, 读写不再依赖SDIO中断
     * (DMA模式的SD_WriteBlock死等TransferEnd标志, 但SDIO中断被禁用
     *  且与USBFS同优先级0无法嵌套, 导致写扇区超时 -> 格式化失败) */
    SD_SetDeviceMode(SD_POLLING_MODE);
    NVIC_DisableIRQ(SDIO_IRQn);
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    printf("USBFS Device ready\r\n");

    OLED_ShowString(3, 0, "USB Ready");
    OLED_Refresh();

    while (1) {
        if (USBFS_DevEnumStatus) {
            OLED_ShowString(0, 10, "ENUM");
        } else {
            OLED_ShowString(0, 10, "    ");
        }
        OLED_Refresh();
        Delay_Ms(200);
    }
}
#endif

/*============================================================================
 * TEST_MODE 3 —— SD 卡 FAT 读文件：读 123.txt，串口打印 + OLED 显示
 *============================================================================*/
#if TEST_MODE == 3
void Mode_Fatfs(void)
{
    FRESULT fr;
    UINT br;
    u32 i;
    u8 row, col;

    printf("=== SD Card FAT Read Mode ===\r\n");

    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "FAT 123.txt");
    OLED_Refresh();

    if (SD_Init() != SD_OK) {
        printf("SD_Init failed\r\n");
        OLED_ShowString(1, 0, "SD FAIL");
        OLED_Refresh();
        while (1);
    }
    printf("SD_Init OK\r\n");

    fr = f_mount(&s_fatfs, "1:", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %d\r\n", fr);
        {
            u8 sec0[512];
            SD_Error err = SD_ReadDisk(sec0, 0, 1);
            printf("Read sec0: err=%d\r\n", err);
            printf("Sector0 dump:\r\n");
            for (i = 0; i < 64; i++) {
                printf("%02X ", sec0[i]);
                if ((i % 16) == 15) printf("\r\n");
            }
            printf("Sig: %02X %02X\r\n", sec0[510], sec0[511]);
        }
        OLED_ShowString(1, 0, "MOUNT FAIL");
        OLED_Refresh();
        while (1);
    }
    printf("Mount OK\r\n");

    fr = f_open(&s_fil, "1:/123.txt", FA_READ);
    if (fr != FR_OK) {
        printf("f_open 123.txt failed: %d\r\n", fr);
        OLED_ShowString(1, 0, "OPEN FAIL");
        OLED_ShowNum(2, 0, fr, 3);
        OLED_Refresh();
        while (1);
    }
    printf("Open OK\r\n");

    fr = f_read(&s_fil, s_buf, sizeof(s_buf) - 1, &br);
    f_close(&s_fil);
    if (fr != FR_OK) {
        printf("f_read failed: %d\r\n", fr);
        OLED_ShowString(1, 0, "READ FAIL");
        OLED_Refresh();
        while (1);
    }
    s_buf[br] = '\0';
    printf("Read %u bytes:\r\n%s\r\n", br, s_buf);

    OLED_ShowNum(1, 0, br, 5);
    OLED_ShowString(1, 6, "bytes");
    row = 2;
    col = 0;
    for (i = 0; i < br && row < 8; i++) {
        u8 c = s_buf[i];
        if (c == '\n') {
            row++;
            col = 0;
        } else if (c != '\r') {
            if (col >= 16) {
                row++;
                col = 0;
                if (row >= 8) break;
            }
            OLED_ShowChar(row, col, c);
            col++;
        }
    }
    OLED_Refresh();

    while (1);
}
#endif

/*============================================================================
 * TEST_MODE 4 —— USBHS Device 枚举链路调试
 *============================================================================*/
#if TEST_MODE == 4
void Mode_UsbhsEnum(void)
{
    u32 i;
    u8 spd, ctl, inten, intfg, hctrl, mist;

    printf("=== USBHS Device Enum Test ===\r\n");

    USBHS_Dev_RCC_Init();
    USBHS_Device_Init(ENABLE);
    printf("USBHS Device ready, insert cable...\r\n");

    while (1) {
        /* 枚举链路计数: Rst=总线复位 Set=SETUP请求 Trs=传输完成
         * Adr=设备地址 Det=插入检测 Sus=挂起 Enum=枚举完成 */
        printf("Rst=%u Set=%u Trs=%u Adr=%u Det=%u Sus=%u Enum=%d\r\n",
            USBHS_Int_Rst, USBHS_Int_Setup, USBHS_Int_Trans,
            USBHS_DevAddr, USBHS_Int_Detect, USBHS_Int_Susp,
            USBHS_DevEnumStatus);
        /* 寄存器快照 */
        spd   = USBHSD->SPEED_TYPE & USBHS_SPEED_TYPE_MASK;
        ctl   = USBHSD->CONTROL;
        inten = USBHSD->INT_EN;
        intfg = USBHSD->INT_FG;
        hctrl = USBHSD->HOST_CTRL;
        mist  = USBHSD->MIS_ST;
        printf("SpeedType=%d(%s) Ctrl=%02X IntEN=%02X IntFG=%02X HostCtrl=%02X MisSt=%02X\r\n",
            spd,
            (spd == USBHS_SPEED_HIGH) ? "HS480M" :
            (spd == USBHS_SPEED_LOW)  ? "LS" : "FS12M",
            ctl, inten, intfg, hctrl, mist);
        /* 最近8次总线复位时PHY协商的速度快照 */
        printf("RstSpeedSnap:");
        for (i = 0; i < 8 && i < USBHS_Dbg_RstIdx; i++) {
            printf(" %d", USBHS_Dbg_RstSnap[i]);
        }
        printf("\r\n");
        Delay_Ms(1000);
    }
}
#endif

/*============================================================================
 * TEST_MODE 5 —— USBFS HID 摇杆鼠标：像鼠标一样控制手机
 *============================================================================*/
#if TEST_MODE == 5
void Mode_HidMouse(void)
{
    int32_t x_sum = 0, y_sum = 0;
    int     x_n = 0, y_n = 0;
    int32_t x_mid, y_mid;
    uint8_t mouse_report[4] = {0, 0, 0, 0};
    u32     cnt = 0;

    printf("=== USBFS HID Joystick Mouse Mode ===\r\n");

    Joystick_Init();
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    printf("USBFS HID ready, calibrating joystick...\r\n");

    Delay_Ms(100);
    Joystick_GetX(); Joystick_GetY();
    Delay_Ms(50);

    for (int i = 0; i < 32; i++) {
        int16_t xv = Joystick_GetX();
        int16_t yv = Joystick_GetY();
        if (xv > 1000 && xv < 16000) { x_sum += xv; x_n++; }
        if (yv > 1000 && yv < 16000) { y_sum += yv; y_n++; }
        Delay_Ms(20);
    }
    x_mid = (x_n > 0) ? x_sum / x_n : 8800;
    y_mid = (y_n > 0) ? y_sum / y_n : 8800;
    printf("Calibrated: Xmid=%ld Ymid=%ld (%d/%d samples)\r\n", x_mid, y_mid, x_n, y_n);

    while (1) {
        if (USBFS_DevEnumStatus) {
            Delay_Ms(50);

            int16_t x_raw = Joystick_GetX();
            int16_t y_raw = Joystick_GetY();
            int32_t x_off = (int32_t)x_raw - x_mid;
            int32_t y_off = (int32_t)y_raw - y_mid;

            if (x_off > -500 && x_off < 500) x_off = 0;
            if (y_off > -500 && y_off < 500) y_off = 0;

            int8_t mx = (int8_t)(-y_off / 128);
            int8_t my = (int8_t)(-x_off / 128);

            if (mx > 64) mx = 64;
            if (mx < -64) mx = -64;
            if (my > 64) my = 64;
            if (my < -64) my = -64;

            mouse_report[0] = Joystick_GetButton() ? 0 : 0x01;
            mouse_report[1] = (uint8_t)mx;
            mouse_report[2] = (uint8_t)my;
            mouse_report[3] = 0;

            USBFS_Endp_DataUp(DEF_UEP2, mouse_report, 4, DEF_UEP_CPY_LOAD);

            cnt++;
            if ((cnt % 20) == 0) {
                printf("X=%d Y=%d mx=%d my=%d btn=%d\r\n",
                       x_raw, y_raw, mx, my, mouse_report[0]);
            }
        } else {
            Delay_Ms(200);
            printf("Waiting enum... Enum=%d\r\n", USBFS_DevEnumStatus);
        }
    }
}
#endif

/*============================================================================
 * TEST_MODE 0 —— 正式功能：吹气采集 + USB AOA 手机控制
 *============================================================================*/
#if TEST_MODE == 0
void Mode_PhoneControl(void)
{
    Phone_Control_Init();
    while (1) {
        Phone_Control_Poll();
    }
}
#endif
