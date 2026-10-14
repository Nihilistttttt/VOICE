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
#include "pressure.h"
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
#if USE_JOYSTICK
    int32_t x_sum = 0, y_sum = 0;
    int     x_n = 0, y_n = 0;
    int32_t x_mid, y_mid;
    uint8_t mouse_report[5] = {0, 0, 0, 0, 0};   /* [4]=Consumer: bit0=VolUp bit1=VolDown */
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

#if USE_PRESSURE
    /* 压差传感器与摇杆同走 ADS1115，借本模式一并验证 */
    Pressure_Init();
    printf("[Pressure] zero calibrating (keep ports idle)...\r\n");
    Pressure_ZeroCalibrate(5);
#endif


    uint8_t ms_btn = 0, blow_n = 0, sip_n = 0;   /* 压差按键状态（函数级, 跨循环保持）*/
    uint8_t sip_mode = 0;       /* 0=无 1=按住计时 2=音量模式 */
    uint16_t sip_ms = 0;        /* 吸住时长（约 50ms/循环）*/
    uint8_t rclick_pend = 0;    /* 右键点击: 2=按下待发 1=待释放 */
    uint8_t scroll_mode = 0;    /* 双口滚动: 0=无 1=滚动中 */
    int8_t  scroll_dir = 0;     /* 滚动方向: +1=向下 -1=向上 */
    uint8_t scroll_n = 0;       /* 滚轮限速计数 */
    uint8_t s2_mode = 0;        /* s2 手势: 0=无 1=吹按住 2=吸按住 */
    uint16_t s2_ms = 0;         /* s2 按住时长（约 50ms/循环）*/
    uint8_t b2_n = 0, s2_n2 = 0;
    uint8_t kb_pend = 0;        /* 键盘发送: 1=按下待发 2=释放待发 */
    uint8_t kb_mod = 0, kb_key = 0;   /* 待发修饰键/键值 */
    uint8_t home_seq = 0;       /* 回主屏三连: 1=AltTab下 2=起 3=等待1s 4=右键按下 5=右键释放 6=等待1s 7=Home下 8=Home起 */
    uint8_t home_n = 0;         /* 等待相计数 */
    uint8_t KB_Data_Pack[8] = {0};    /* 键盘 IN 报告（ITF0/EP1）*/
#define HOME_X_CENTER_COUNTS   540    /* 屏宽一半(P20=1080px)，按指针速度实测调整 */
#define HOME_Y_NAV_OFFSET      24     /* 主页键中心距屏幕底边 */

    while (1) {
        if (USBFS_DevEnumStatus) {
            if (home_seq == 5) {
                Delay_Ms(30);   /* 点击保持期: 按下→释放太短怕 EMUI 不识别成点击 */
            } else if (home_seq == 1) {
                /* 冲角段: 主机取走上一帧即发下一帧(EP2 bInterval=1ms)
                 * 满幅冲角怎么飞都出不了屏(边界钳位), 快发只赚不亏 */
                uint16_t w = 0;
                while (USBFS_Endp_Busy[DEF_UEP2] && ++w < 100) Delay_Us(100);
            } else if (home_seq) {
                /* 定位/点击段: 必须 25ms/帧 —— Android 指针加速度随速度放大位移,
                 * 快发会被放大导致飞过头; 慢速下"计数≈距离"的映射才成立 */
                Delay_Ms(25);
            } else {
                Delay_Ms(50);
            }

            /* 手势映射:
             *   短吹 = 左键点击(确定)   吹住 = 左键按住 + 指针跟随 = 拖拽
             *   短吸 = 右键点击(返回)   吸住 = 音量模式(指针冻结, 倾斜=音量)
             * 迟滞: 按下 ±250Pa / 释放 ±150Pa */
            int32_t p1 = Pressure_ReadPa(PRESSURE_SENSOR_1);
            int32_t p2 = Pressure_ReadPa(PRESSURE_SENSOR_2);

            /* s2 手势: 短吸=Home 短吹=Alt+Tab(多任务切换)
             * 按住≥500ms 无动作（预留）；迟滞同 s1（按 ±250 / 释 ±150）*/
            if (s2_mode == 0) {
                if (p2 >=  250) { if (++b2_n >= 2) { s2_mode = 1; s2_ms = 0; b2_n = 2; } }
                else if (p2 <= -250) { if (++s2_n2 >= 2) { s2_mode = 2; s2_ms = 0; s2_n2 = 2; } }
                else { b2_n = 0; s2_n2 = 0; }
            } else if (s2_ms < 500) {                     /* 按住计时 */
                if (s2_mode == 1 && p2 <=  150) {         /* 短吹松口 → Alt+Tab */
                    kb_mod = 0x04; kb_key = 0x2B; kb_pend = 1; s2_mode = 0; b2_n = 0;
                } else if (s2_mode == 2 && p2 >= -150) {  /* 短吸松口 → 主屏（脚本化底部上滑）*/
                    home_seq = 1; s2_mode = 0; s2_n2 = 0;
                } else {
                    s2_ms += 50;
                }
            } else {                                      /* 按住≥500ms: 无动作（预留）*/
                if (s2_mode == 1 && p2 <=  150) s2_mode = 0;
                else if (s2_mode == 2 && p2 >= -150) s2_mode = 0;
            }

            /* 键盘报告两相发送: 按下 → (下一循环)释放 */
            if (kb_pend == 1) {
                KB_Data_Pack[0] = kb_mod;
                KB_Data_Pack[2] = kb_key;
                USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                kb_pend = 2;
            } else if (kb_pend == 2) {
                KB_Data_Pack[0] = 0; KB_Data_Pack[2] = 0;
                USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                kb_pend = 0;
            }

            /* 双口滚动（优先于单口手势）: 双吹=向下滚 双吸=向上滚
             * 进入: 双口 |P|≥150   退出: 双口 |P|<100 */
            if (!scroll_mode) {
                if (p1 >=  150 && p2 >=  150) { scroll_mode = 1; scroll_dir = -1; scroll_n = 0; }   /* 双吹 = 上滚 */
                else if (p1 <= -150 && p2 <= -150) { scroll_mode = 1; scroll_dir =  1; scroll_n = 0; }   /* 双吸 = 下滚 */
            } else {
                int32_t a1 = (p1 < 0) ? -p1 : p1;
                int32_t a2 = (p2 < 0) ? -p2 : p2;
                if (a1 < 100 && a2 < 100) { scroll_mode = 0; scroll_dir = 0; scroll_n = 0; }
            }

            if (!scroll_mode) {
            if (!(ms_btn & 0x01) && p1 >=  250 && p2 > -100 && p2 < 100) {          /* 吹: 过阈值且另一口空闲 */
                if (++blow_n >= 2) { ms_btn |= 0x01; blow_n = 2; }
            } else if ((ms_btn & 0x01) && p1 <=  150) {    /* 吹: 已按下且回落 */
                ms_btn &= (uint8_t)~0x01; blow_n = 0;
            } else if (!(ms_btn & 0x01)) {
                blow_n = 0;
            }

            if (sip_mode == 0) {
                if (!(ms_btn & 0x02) && p1 <= -250 && p2 > -100 && p2 < 100) {      /* 吸: 过阈值且另一口空闲 */
                    if (++sip_n >= 2) { sip_mode = 1; sip_ms = 0; sip_n = 2; }
                } else {
                    sip_n = 0;
                }
            } else if (sip_mode == 1) {                    /* 按住计时 */
                if (p1 >= -150) {                          /* 松口: 短吸=右键点击 */
                    if (sip_ms < 500) rclick_pend = 2;
                    sip_mode = 0; sip_n = 0;
                } else {
                    sip_ms += 50;
                    if (sip_ms >= 500) sip_mode = 2;       /* 吸住 → 音量模式 */
                }
            } else {                                       /* 音量模式 */
                if (p1 >= -150) { sip_mode = 0; sip_n = 0; }      /* 松口退出 */
            }

            /* 右键点击: 按下→释放（两个循环完成, 构成一次点击）*/
            if (rclick_pend == 2) {
                ms_btn |= 0x04; rclick_pend = 1;      /* 中键 = EMUI 返回（华为官方: 右键无返回语义）*/
            } else if (rclick_pend == 1) {
                ms_btn &= (uint8_t)~0x04; rclick_pend = 0;
            }
            } /* 非滚动模式: 单口手势结束 */

            mouse_report[0] = ms_btn;

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

            /* Consumer 字节: 音量位（吸住音量模式: 指针冻结 + 倾斜=音量）*/
            uint8_t cons = 0;
            if (sip_mode == 2) {
                mx = 0; my = 0;
                cons = (y_off < -600) ? 0x01 : (y_off > 600) ? 0x02 : 0;
            }
            mouse_report[4] = cons;

            mouse_report[1] = (uint8_t)mx;
            mouse_report[2] = (uint8_t)my;
            mouse_report[3] = 0;
            if (scroll_mode && ++scroll_n >= 2) { scroll_n = 0; mouse_report[3] = scroll_dir; }   /* 双口滚动: 每 100ms 一格 */

            /* 回主屏（s2 短吸触发）—— 按 HOME_MODE 宏选择实现
             * ★ 序列必须在正常赋值之后: 它最终接管整个报告 */
            if (home_seq) {
#if HOME_MODE == HOME_MODE_KEYBOARD
                switch (home_seq) {
                case 1:   /* Alt+Tab 按下 */
                    KB_Data_Pack[0] = 0x04; KB_Data_Pack[2] = 0x2B;   /* LAlt+Tab */
                    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                    home_seq = 2; break;
                case 2:   /* 释放 → 多任务界面打开 */
                    KB_Data_Pack[0] = 0; KB_Data_Pack[2] = 0;
                    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                    home_seq = 3; home_n = 0; break;
                case 3:   /* 等待 400ms: 多任务界面打开动画 */
                    if (++home_n >= 1) { home_seq = 4; home_n = 0; }
                    break;
                case 4:   /* 鼠标中键按下（EMUI: 中键=返回，关多任务 → 桌面）*/
                    mouse_report[0] = 0x04;
                    home_seq = 5; break;
                case 5:   /* 中键释放 → 桌面 */
                    mouse_report[0] = 0x00;
                    home_seq = 6; home_n = 0; break;
                case 6:   /* 等待 300ms: 桌面稳定 */
                    if (++home_n >= 1) { home_seq = 7; home_n = 0; }
                    break;
                case 7:   /* Home 按下（桌面 → 主页）*/
                    KB_Data_Pack[0] = 0; KB_Data_Pack[2] = 0x4A;
                    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                    home_seq = 8; break;
                case 8:   /* 释放 → 完成 */
                    KB_Data_Pack[0] = 0; KB_Data_Pack[2] = 0;
                    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                    home_seq = 0; break;
                }
#else
                switch (home_seq) {
                case 1:   /* 冲左下角: X 左满幅 + Y 下满幅（边界钳位）*/
                    mouse_report[1] = 0x80;    /* X = -128 */
                    mouse_report[2] = 127;     /* Y = +127 */
                    if (++home_n >= 12) { home_seq = 2; home_n = 0; }
                    break;
                case 2:   /* X 右移到屏幕中心（分 5 报告，每份 108 计数）*/
                    mouse_report[1] = HOME_X_CENTER_COUNTS / 5; mouse_report[2] = 0;
                    if (++home_n >= 5) { home_seq = 3; home_n = 0; }
                    break;
                case 3:   /* Y 微调: 进入主页键中心 */
                    mouse_report[2] = (uint8_t)(-HOME_Y_NAV_OFFSET);
                    home_seq = 4; break;
                case 4:   /* 左键按下 */
                    mouse_report[0] |= 0x01;
                    home_seq = 5; break;
                case 5:   /* 左键释放 → 主页键点击 = 回主页 */
                    mouse_report[0] &= (uint8_t)~0x01;
                    home_seq = 0; break;
                }
#endif
            }

            USBFS_Endp_DataUp(DEF_UEP2, mouse_report, 5, DEF_UEP_CPY_LOAD);

            cnt++;
            if ((cnt % 20) == 0) {
                int32_t p1 = 0, p2 = 0;
                uint8_t sat1 = 0, sat2 = 0;
#if USE_PRESSURE
                p1   = Pressure_ReadPa(PRESSURE_SENSOR_1);
                p2   = Pressure_ReadPa(PRESSURE_SENSOR_2);
                sat1 = Pressure_Saturated(PRESSURE_SENSOR_1);
                sat2 = Pressure_Saturated(PRESSURE_SENSOR_2);
#endif
                printf("X=%d Y=%d mx=%d my=%d btn=%d\r\n",
                       x_raw, y_raw, mx, my, mouse_report[0]);
#if USE_PRESSURE
                printf("[Pressure] s1=%ld Pa%s  s2=%ld Pa%s\r\n",
                       p1, sat1 ? " [SAT]" : "",
                       p2, sat2 ? " [SAT]" : "");
#endif
            }
        } else {
            Delay_Ms(200);
#if HID_REQUIRE_PHONE
            printf("Waiting enum... Enum=%d\r\n", USBFS_DevEnumStatus);
#else
            /* 脱机测试：不等枚举，没主机也持续读传感器打印（含蓝牙广播）*/
            int32_t p1 = 0, p2 = 0;
            uint8_t sat1 = 0, sat2 = 0;
#if USE_PRESSURE
            p1   = Pressure_ReadPa(PRESSURE_SENSOR_1);
            p2   = Pressure_ReadPa(PRESSURE_SENSOR_2);
            sat1 = Pressure_Saturated(PRESSURE_SENSOR_1);
            sat2 = Pressure_Saturated(PRESSURE_SENSOR_2);
#endif
            printf("X=%d Y=%d (standalone)  s1=%ld Pa%s  s2=%ld Pa%s\r\n",
                   Joystick_GetX(), Joystick_GetY(),
                   p1, sat1 ? " [SAT]" : "", p2, sat2 ? " [SAT]" : "");
#endif
        }
    }
#endif /* USE_JOYSTICK */
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
