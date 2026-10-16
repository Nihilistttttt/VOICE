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
#include "log.h"
#include "serial_cmd.h"
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
#elif TEST_MODE == 0
#include "ch32v30x_usbfs_device.h"
#include "joystick.h"
#include "pressure.h"
#include "gesture_sm.h"
#include "params.h"
#include "params_sd.h"
#include "esp_link.h"
#include "log.h"
#include "serial_cmd.h"
#include "ads1115.h"
#elif TEST_MODE == 5
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

    LOGI("udisk", "U 盘模式启动");

    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "MSC U-Disk");
    OLED_Refresh();

    sd_err = SD_Init();
    if (sd_err != SD_OK) {
        LOGE("udisk", "SD_Init 失败: %d", sd_err);
        OLED_ShowString(1, 0, "SD FAIL");
        OLED_Refresh();
        while (1);
    }
    LOGI("udisk", "SD_Init OK");
    /* U 盘模式把 SD 导出给 USB 主机 —— FatFs/日志后端必须让位(双主保护),
     * 此后 LOGx 仅串口输出, 缓冲数据滞留 RAM 不落盘 */
    Log_SD_Suspend();
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
        Cmd_Poll(0);        /* 串口命令: 仅 log=N/ver 可用(SD 已导出, 禁写) */
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
 * TEST_MODE 0 —— 正式功能：压差手势 + 手机控制
 *============================================================================*/
#if TEST_MODE == 0
void Mode_HidMouse(void)
{
#if USE_JOYSTICK
    int32_t x_sum = 0, y_sum = 0;
    int     x_n = 0, y_n = 0;
    int32_t x_mid, y_mid;
    uint8_t mouse_report[5] = {0, 0, 0, 0, 0};   /* [4]=Consumer: bit0=VolUp bit1=VolDown */
    u32     cnt = 0;

    LOGI("hid", "USBFS HID 手势模式启动");

#if USE_SDCARD
    /* 参数/SD 最先初始化 —— SD 的自愈写要赶在 USBFS 启动之前完成:
     * USBFS 中断与 SDIO 同优先级 0, SD 在 USBFS 活跃时读写会超时
     * (MODE=2 的实测教训, 已在 Params_SD_Init 内切轮询模式双保险) */
    Params_SD_Init();
    Log_SD_Attach();    /* 挂载成功 → 日志文件生效, 此后 LOGx 全部落 SD */
#endif
#if USE_ESP_LINK
    EspLink_Init();     /* UART6@2M ↔ ESP8266(透传) ↔ WiFi UDP:8888 调参链路 */
#endif

    Joystick_Init();
    USBFS_RCC_Init();
    USBFS_Device_Init(ENABLE);
    LOGI("hid", "摇杆校准中...");

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
    LOGI("hid", "校准: Xmid=%ld Ymid=%ld (%d/%d)", x_mid, y_mid, x_n, y_n);

#if USE_PRESSURE
    /* 压差传感器与摇杆同走 ADS1115，借本模式一并验证 */
    Pressure_Init();
    printf("[Pressure] zero calibrating (keep ports idle)...\r\n");
    Pressure_ZeroCalibrate(5);
#endif


    /* ---- 手势状态机（纯逻辑: Common/gesture_sm.c; 宿主单测: hosttest/）----
     * 自原内联实现等价抽取, 行为不变（唯一例外: 断连恢复, 见循环尾部）*/
    s1_sm_t     s1;  s1_sm_init(&s1);      /* 单口: 吹=左键/拖拽 吸=中键/音量 */
    scroll_sm_t scr; scroll_sm_init(&scr); /* 双口滚动 */
    s2_sm_t     s2;  s2_sm_init(&s2);      /* 第二口: 短吹=Alt+Tab 短吸=回主页 */
    kb_sm_t     kb;  kb_sm_init(&kb);      /* 键盘两相发送 */
    home_sm_t   hom; home_sm_init(&hom);   /* 回主页序列(CLICKNAV) */
    uint8_t KB_Data_Pack[8] = {0};    /* 键盘 IN 报告（ITF0/EP1）*/
#if HOME_MODE == HOME_MODE_KEYBOARD
    uint8_t home_seq = 0;       /* 键盘三连: 1=AltTab下 2=起 3=等待 4=中键下 5=中键起 6=等待 7=Home下 8=Home起 */
    uint8_t home_n = 0;         /* 等待相计数 */
/* 键盘三连的固定等待拍数: 原实现写成 ++home_n>=1 恒真(审计 S8), 语义就是
 * 等 1 拍, 提为命名常量消除恒真并保住可调性 */
#define HOME_KB_WAIT_TICKS  1
#endif

    while (1) {
        if (USBFS_DevEnumStatus) {
            Log_Service();      /* 日志限频刷 SD(缓冲有积压且超时时才真正写) */
            Cmd_Poll(1);        /* 调试串口命令(mode/save/factory/log) */
            /* 节奏策略: 回主页序列按相选择, 其余 50ms/圈
             * ★ Android 指针加速度把"发多快"耦合进"走多远": 冲角段可快发
             *   (钳位兜底), X/Y 定位段必须 25ms/帧, 点击保持 30ms */
            if (hom.phase != HOME_IDLE) {
                uint16_t pms = home_sm_pacing_ms(hom.phase);
                if (pms == 0) {
                    /* 冲角段: 主机取走上一帧即发下一帧(EP2 bInterval=1ms) */
                    uint16_t w = 0;
                    while (USBFS_Endp_Busy[DEF_UEP2] && ++w < 100) Delay_Us(100);
                } else {
                    Delay_Ms(pms);
                }
            } else {
                Delay_Ms(50);
            }

            /* 手势映射（状态机实现见 Common/gesture_sm.c, 阈值/行为与原内联版一致）:
             *   短吹 = 左键点击(确定)   吹住 = 左键按住 + 指针跟随 = 拖拽
             *   短吸 = 中键点击(返回)   吸住 = 音量模式(指针冻结, 倾斜=音量) */
            int32_t p1 = Pressure_ReadPa(PRESSURE_SENSOR_1);
            int32_t p2 = Pressure_ReadPa(PRESSURE_SENSOR_2);

            /* WiFi 调参链路: 参数下行直通参数表, 压差波形上行(UDP:8888) */
            EspLink_Poll(p1, p2);

            /* s2 手势: 短吹=Alt+Tab(多任务切换) 短吸=回主页 */
            s2_cmd_t cmd = s2_sm_step(&s2, p2);
            if (cmd == S2_ALTTAB)      kb_sm_press(&kb, 0x04, 0x2B);   /* LAlt+Tab */
            else if (cmd == S2_HOME)   home_sm_start(&hom);

            /* 键盘报告两相发送: 按下 → (下一循环)释放 */
            kb_out_t ko;
            kb_sm_step(&kb, &ko);
            if (ko.evt == 1) {
                KB_Data_Pack[0] = ko.mod; KB_Data_Pack[2] = ko.key;
                USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
            } else if (ko.evt == 2) {
                KB_Data_Pack[0] = 0; KB_Data_Pack[2] = 0;
                USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
            }

            /* 双口滚动（优先于单口手势）: 双吹=上滚 双吸=下滚, 100ms 一格 */
            int8_t wheel = scroll_sm_step(&scr, p1, p2);

            /* 摇杆 → 指针增量 */
            int16_t x_raw = Joystick_GetX();
            int16_t y_raw = Joystick_GetY();
            int32_t x_off = (int32_t)x_raw - x_mid;
            int32_t y_off = (int32_t)y_raw - y_mid;

            if (x_off > -g_params.joy_deadzone && x_off < g_params.joy_deadzone) { x_off = 0; }
            if (y_off > -g_params.joy_deadzone && y_off < g_params.joy_deadzone) { y_off = 0; }

            int8_t mx = (int8_t)(-y_off / g_params.joy_step_div);
            int8_t my = (int8_t)(-x_off / g_params.joy_step_div);

            if (mx > g_params.joy_clamp)  { mx = g_params.joy_clamp; }
            if (mx < -g_params.joy_clamp) { mx = -g_params.joy_clamp; }
            if (my > g_params.joy_clamp)  { my = g_params.joy_clamp; }
            if (my < -g_params.joy_clamp) { my = -g_params.joy_clamp; }

            /* s1 单口手势（滚动期间状态冻结不推进, 与原门控一致）*/
            s1_out_t so;
            if (!scr.active) {
                s1_sm_step(&s1, p1, p2, &so);
            } else {
                so.btn = s1.btn;
                so.volume_active = (s1.state == S1_SIP_VOLUME);
            }

            /* Consumer 字节: 音量位（吸住音量模式: 指针冻结 + 倾斜=音量）
             * 注: 与原实现一致, 音量输出不受滚动门控 */
            uint8_t cons = 0;
            if (so.volume_active) {
                mx = 0; my = 0;
                cons = (y_off < -g_params.vol_tilt) ? 0x01 : (y_off > g_params.vol_tilt) ? 0x02 : 0;
            }

            mouse_report[0] = so.btn;
            mouse_report[1] = (uint8_t)mx;
            mouse_report[2] = (uint8_t)my;
            mouse_report[3] = (uint8_t)wheel;   /* 0 或 ±1, 滚动机内部已限速 */
            mouse_report[4] = cons;

            /* 回主屏（s2 短吸触发）—— 按 HOME_MODE 宏选择实现
             * ★ 序列必须在正常赋值之后: 它最终接管整个报告 */
#if HOME_MODE == HOME_MODE_KEYBOARD
            if (home_seq) {
                switch (home_seq) {
                case 1:   /* Alt+Tab 按下 */
                    KB_Data_Pack[0] = 0x04; KB_Data_Pack[2] = 0x2B;   /* LAlt+Tab */
                    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                    home_seq = 2; break;
                case 2:   /* 释放 → 多任务界面打开 */
                    KB_Data_Pack[0] = 0; KB_Data_Pack[2] = 0;
                    USBFS_Endp_DataUp(DEF_UEP1, KB_Data_Pack, 8, DEF_UEP_CPY_LOAD);
                    home_seq = 3; home_n = 0; break;
                case 3:   /* 等待 HOME_KB_WAIT_TICKS 拍: 多任务界面打开动画 */
                    if (++home_n >= HOME_KB_WAIT_TICKS) { home_seq = 4; home_n = 0; }
                    break;
                case 4:   /* 鼠标中键按下（EMUI: 中键=返回，关多任务 → 桌面）*/
                    mouse_report[0] = 0x04;
                    home_seq = 5; break;
                case 5:   /* 中键释放 → 桌面 */
                    mouse_report[0] = 0x00;
                    home_seq = 6; home_n = 0; break;
                case 6:   /* 等待 HOME_KB_WAIT_TICKS 拍: 桌面稳定 */
                    if (++home_n >= HOME_KB_WAIT_TICKS) { home_seq = 7; home_n = 0; }
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
            }
#else
            if (hom.phase != HOME_IDLE) {
                home_out_t ho;
                home_sm_step(&hom, &ho);
                if (ho.move) {                    /* 移动相: 接管 X/Y */
                    mouse_report[1] = (uint8_t)ho.dx;
                    mouse_report[2] = (uint8_t)ho.dy;
                }
                if (ho.left_btn == 1) {           /* 点击相: 接管左键 */
                    mouse_report[0] |= 0x01;
                } else if (ho.left_btn == 0) {
                    mouse_report[0] &= (uint8_t)~0x01;
                }
            }
#endif

            USBFS_Endp_DataUp(DEF_UEP2, mouse_report, 5, DEF_UEP_CPY_LOAD);

            cnt++;
            if ((cnt % 20) == 0) {
                /* 诊断打印专用副本(dbg_ 前缀): 不遮蔽外层的手势输入 p1/p2
                 * (审计 S4/C2——同名遮蔽曾让维护者难以判断读写的是哪份) */
                int32_t dbg_p1 = 0, dbg_p2 = 0;
                uint8_t sat1 = 0, sat2 = 0;
#if USE_PRESSURE
                dbg_p1 = Pressure_ReadPa(PRESSURE_SENSOR_1);
                dbg_p2 = Pressure_ReadPa(PRESSURE_SENSOR_2);
                sat1   = Pressure_Saturated(PRESSURE_SENSOR_1);
                sat2   = Pressure_Saturated(PRESSURE_SENSOR_2);
#endif
                printf("X=%d Y=%d mx=%d my=%d btn=%d\r\n",
                       x_raw, y_raw, mx, my, mouse_report[0]);
#if USE_PRESSURE
                printf("[Pressure] s1=%ld Pa%s  s2=%ld Pa%s\r\n",
                       dbg_p1, sat1 ? " [SAT]" : "",
                       dbg_p2, sat2 ? " [SAT]" : "");
#endif
            }
        } else {
            Delay_Ms(200);
            /* 断连恢复(审计 F3): 未完成的回主页序列作废。
             * 键盘无需外部干预: SMF 状态驻留在 kb 结构体内, 若断连时悬在
             * PRESSED 相（已发按下待释放）, 重连后第一次 kb_sm_step 会照常
             * 补发释放帧——外部裸改镜像字段(审计 M1)既无效也不必要 */
            home_sm_abort(&hom);
#if HOME_MODE == HOME_MODE_KEYBOARD
            home_seq = 0;
#endif
#if HID_REQUIRE_PHONE
            LOGD("hid", "等待枚举 Enum=%d", USBFS_DevEnumStatus);
#else
            /* 脱机测试：不等枚举，没主机也持续读传感器打印（含蓝牙广播）*/
            int32_t dbg_p1 = 0, dbg_p2 = 0;
            uint8_t sat1 = 0, sat2 = 0;
#if USE_PRESSURE
            dbg_p1 = Pressure_ReadPa(PRESSURE_SENSOR_1);
            dbg_p2 = Pressure_ReadPa(PRESSURE_SENSOR_2);
            sat1   = Pressure_Saturated(PRESSURE_SENSOR_1);
            sat2   = Pressure_Saturated(PRESSURE_SENSOR_2);
#endif
            printf("X=%d Y=%d (standalone)  s1=%ld Pa%s  s2=%ld Pa%s\r\n",
                   Joystick_GetX(), Joystick_GetY(),
                   dbg_p1, sat1 ? " [SAT]" : "", dbg_p2, sat2 ? " [SAT]" : "");
#endif
        }
    }
#endif /* USE_JOYSTICK */
}
#endif

/*============================================================================
 * TEST_MODE 5 —— 旧版V2：麦克风吹气 + AOA
 *============================================================================*/
#if TEST_MODE == 5
void Mode_PhoneControl(void)
{
    Phone_Control_Init();
    while (1) {
        Phone_Control_Poll();
    }
}
#endif
