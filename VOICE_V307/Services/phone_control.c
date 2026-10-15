#include "phone_control.h"
#include "Config.h"
#include "aoa_host.h"
#include "mic_hum.h"
#include "OLED.h"
#include "debug.h"
#include "ch32v30x.h"
#include <string.h>

/*============================================================================
 * VOICE Phone Control — CH32V307 USB AOA 版
 *
 * Binary command protocol (MCU -> App, single byte):
 *   2  -> ENTER     确定
 *   3  -> BACK      返回
 *   4  -> PLAY      播放/暂停
 *   5  -> NEXT      下一个
 *   6  -> PREV      上一个
 *   7  -> VOL_UP    音量+
 *   8  -> VOL_DN    音量-
 *   9  -> RETURN    返回选择
 *   10 -> MODE      模式切换
 *   11 -> LEFT      左
 *   12 -> RIGHT     右
 *   13 -> VOL_MODE  音量模式切换
 *
 * 双麦克风命令映射:
 *   MIC_A: 短=ENTER  中/长=NEXT(或VOL_UP)  短短=PLAY  短短短=VOL_MODE
 *   MIC_B: 短=BACK   中/长=PREV(或VOL_DN)  短短=MODE  短短短=VOL_MODE
 *   跨麦: 左短+右短=LEFT  右短+左短=RIGHT
 *   (音量模式下中/长=VOL+/-, 短短短切换)
 *
 * Android -> MCU:
 *   0xFD = 启用能量流  0xFC = 停用能量流
 *   0xFB + id + vH + vL = 设置参数
 *   0xFA = 进入采集模式
 *
 *   0 -> help
 *   1 -> Connect (AOA)
 *   d -> Disconnect
 *============================================================================*/

/* USART1 RX 初始化 (debug.c 只初始化了 TX) */
static void USART1_RX_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART1->CTLR1 |= USART_Mode_Rx;
}

static void Print_Help(void)
{
    printf("\r\n");
    printf("======== VOICE AOA Test Menu ========\r\n");
    printf("  0 - Show this help\r\n");
    printf("  1 - Connect phone (AOA)\r\n");
    printf("  2 - Send ENTER  (0x02)\r\n");
    printf("  3 - Send BACK   (0x03)\r\n");
    printf("  4 - Send PLAY   (0x04)\r\n");
    printf("  5 - Send NEXT   (0x05)\r\n");
    printf("  6 - Send PREV   (0x06)\r\n");
    printf("  7 - Send VOL_UP (0x07)\r\n");
    printf("  8 - Send VOL_DN (0x08)\r\n");
    printf("  9 - Send RETURN (0x09)\r\n");
    printf("  a - Send MODE   (0x0A)\r\n");
    printf("  b - Send LEFT   (0x0B)\r\n");
    printf("  c - Send RIGHT  (0x0C)\r\n");
    printf("  d - Disconnect\r\n");
    printf("=====================================\r\n");
}

static void Handle_Connect(void)
{
    printf("\r\n>>> [1] Connect (AOA) >>>\r\n");
    AOA_Err_t err = AOA_Host_Connect();
    printf("    Result: err=%d state=%u\r\n", (int)err, (unsigned)AOA_Host_GetState());
}

static void Send_CmdByte(uint8_t cmd)
{
    if (AOA_Host_GetState() != AOA_STATE_READY) {
        printf("[AOA] Not connected, auto-reconnect...\r\n");
        AOA_Err_t cerr = AOA_Host_Connect();
        if (cerr != AOA_OK) {
            printf("[AOA] Reconnect failed err=%d\r\n", (int)cerr);
            return;
        }
    }
    AOA_Err_t err = AOA_Host_Send(&cmd, 1);
    printf("    Send 0x%02X err=%d\r\n", (unsigned)cmd, (int)err);
    Delay_Ms(300);
}

static void Handle_Disconnect(void)
{
    printf("\r\n>>> [d] Disconnect >>>\r\n");
    AOA_Err_t err = AOA_Host_Disconnect();
    printf("    Result: err=%d state=%u\r\n", (int)err, (unsigned)AOA_Host_GetState());
}

static uint16_t s_reconnect_cooldown = 0;
static uint8_t  s_was_connected = 0;

static uint8_t  energy_stream_enabled = 0;
static uint16_t energy_call_count = 0;

static uint8_t  param_parse_state = 0;
static uint8_t  param_id = 0;
static uint8_t  param_vH = 0;

static uint8_t  collect_mode = 0;


static uint8_t  oled_div = 0;
static uint8_t  oled_usb_ok = 0;

static void OLED_UpdateDisplay(uint16_t mean_a, uint16_t mean_b)
{
    oled_div++;
    if (oled_div < 4) return;
    oled_div = 0;

    uint8_t usb_ok = (AOA_Host_GetState() == AOA_STATE_READY);
    OLED_Clear();
    OLED_ShowString(1, 0, "V307");
    OLED_ShowString(1, 6, usb_ok ? "USB:OK" : "USB:Wait");
    OLED_ShowString(2, 0, "A:");
    OLED_ShowNum(2, 2, mean_a, 4);
    OLED_ShowString(2, 8, "B:");
    OLED_ShowNum(2, 10, mean_b, 4);

    uint8_t ha = (mean_a > 500) ? 32 : (uint8_t)(mean_a * 32 / 500);
    for (uint8_t x = 2; x < 61; x++) {
        for (uint8_t y = 0; y < ha; y++) {
            OLED_SetPixel(x, 63 - y, 1);
        }
    }

    uint8_t hb = (mean_b > 500) ? 32 : (uint8_t)(mean_b * 32 / 500);
    for (uint8_t x = 66; x < 125; x++) {
        for (uint8_t y = 0; y < hb; y++) {
            OLED_SetPixel(x, 63 - y, 1);
        }
    }

    for (uint8_t y = 32; y < 64; y++) {
        OLED_SetPixel(63, y, 1);
    }

    OLED_Refresh();
    oled_usb_ok = usb_ok;
}

void Phone_Control_Init(void)
{
    USART1_RX_Init();

    printf("[OLED] Init...\r\n");
    OLED_Init();
    OLED_ShowString(1, 0, "V307");
    OLED_ShowString(2, 0, "USB Waiting...");
    OLED_Refresh();

#if USE_MIC_HUM
    printf("[MIC] Init INMP441...\r\n");
    Mic_Hum_Init();
#endif

    AOA_Host_Init();
    printf("[AOA] Auto-connecting...\r\n");
    AOA_Host_Connect();
    if (AOA_Host_GetState() == AOA_STATE_READY) {
        s_was_connected = 1;
    }
    Print_Help();
}

void Phone_Control_Poll(void)
{
    /* AOA 连接状态维护 */
    if (AOA_Host_GetState() != AOA_STATE_READY) {
        if (s_was_connected) {
            printf("[AOA] Connection lost, cooldown 5s...\r\n");
            s_reconnect_cooldown = 500;
            s_was_connected = 0;
        }
        if (s_reconnect_cooldown > 0) {
            s_reconnect_cooldown--;
            Delay_Ms(10);
        } else {
            printf("[AOA] Auto-reconnect...\r\n");
            AOA_Err_t err = AOA_Host_Connect();
            if (err == AOA_OK) {
                s_was_connected = 1;
            } else {
                printf("[AOA] Reconnect err=%d, retry in 3s\r\n", (int)err);
                s_reconnect_cooldown = 300;
            }
        }
    }

    /* 串口命令处理 (USART1 RX 轮询) */
    if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
        uint8_t ch = USART_ReceiveData(USART1);
        switch (ch) {
        case '0': Print_Help();         break;
        case '1': Handle_Connect();     break;
        case '2': Send_CmdByte(0x02);   break;
        case '3': Send_CmdByte(0x03);   break;
        case '4': Send_CmdByte(0x04);   break;
        case '5': Send_CmdByte(0x05);   break;
        case '6': Send_CmdByte(0x06);   break;
        case '7': Send_CmdByte(0x07);   break;
        case '8': Send_CmdByte(0x08);   break;
        case '9': Send_CmdByte(0x09);   break;
        case 'a': case 'A': Send_CmdByte(0x0A); break;
        case 'b': case 'B': Send_CmdByte(0x0B); break;
        case 'c': case 'C': Send_CmdByte(0x0C); break;
        case 'd': case 'D': Handle_Disconnect();  break;
        case '\r':
        case '\n':
        case ' ':
            break;
        default:
            printf("[?] Unknown cmd '%c', press 0 for help\r\n",
                  (ch >= 32 && ch < 127) ? (char)ch : '?');
            break;
        }
    }

    /* 吹气指令处理 */
    uint16_t mean_a = 0, mean_b = 0;
#if USE_MIC_HUM
    int hum_cmd = Mic_Hum_Poll(&mean_a, &mean_b);
#else
    /* V3: 压差传感器采集，后续实现 */
#endif

    OLED_UpdateDisplay(mean_a, mean_b);

    /* 接收Android命令:
     * 0xFD=启用能量流 0xFC=停用能量流
     * 0xFB+id+vH+vL=设置参数 (0=阈值 1=短吹上限 2=组合等待 3=长吹间隔 4=中吹上限 5=最小间隔)
     * 0xFA=进入采集模式 */
    if (AOA_Host_GetState() == AOA_STATE_READY) {
        uint8_t rx_buf[16];
        uint16_t rx_len = 0;
        AOA_Err_t rx_err = AOA_Host_Recv(rx_buf, sizeof(rx_buf), &rx_len);
        if (rx_err == AOA_OK && rx_len > 0) {
            for (uint16_t i = 0; i < rx_len; i++) {
                uint8_t b = rx_buf[i];
                if (param_parse_state == 0) {
                    if (b == 0xFD) {
                        energy_stream_enabled = 1;
                        collect_mode = 0;
                        energy_call_count = 0;
                        printf("[AOA] Energy stream ENABLED\r\n");
                    } else if (b == 0xFC) {
                        energy_stream_enabled = 0;
                        collect_mode = 0;
                        printf("[AOA] Energy stream DISABLED\r\n");
                    } else if (b == 0xFB) {
                        param_parse_state = 1;
                    } else if (b == 0xFA) {
                        collect_mode = 1;
                        printf("[AOA] Collect mode ENABLED\r\n");
                    }
                } else if (param_parse_state == 1) {
                    param_id = b;
                    param_parse_state = 2;
                } else if (param_parse_state == 2) {
                    param_vH = b;
                    param_parse_state = 3;
                } else {
                    uint16_t val = ((uint16_t)param_vH << 8) | b;
#if USE_MIC_HUM
                    switch (param_id) {
                    case 0: g_blow_threshold  = val; break;
                    case 1: g_short_max_tick  = val; break;
                    case 2: g_combo_wait_tick = val; break;
                    case 3: g_long_repeat_tick = val; break;
                    case 4: g_mid_max_tick    = val; break;
                    case 5: g_min_gap_tick    = val; break;
                    default: break;     /* 未知参数号: 静默忽略（协议 0xFB 只定义 0~5）*/
                    }
#endif
                    printf("[AOA] Param %d = %d\r\n", param_id, val);
                    param_parse_state = 0;
                }
            }
        }
    }

    /* 能量包: 能量流模式或采集模式下发送 */
    if ((energy_stream_enabled || collect_mode) && (mean_a > 0 || mean_b > 0)) {
        energy_call_count++;
        if (energy_call_count >= 3) {
            energy_call_count = 0;
            if (AOA_Host_GetState() == AOA_STATE_READY) {
                uint8_t buf[5] = {0xFE,
                    (uint8_t)(mean_a >> 8), (uint8_t)(mean_a & 0xFF),
                    (uint8_t)(mean_b >> 8), (uint8_t)(mean_b & 0xFF)};
                AOA_Host_Send(buf, 5);
            }
        }
    }

    /* 命令处理: 命令模式或采集模式下都处理 */
#if USE_MIC_HUM
    if (hum_cmd != HUM_CMD_NONE) {
        if (collect_mode) {
            collect_mode = 0;
            uint8_t buf[14] = {0xFA,
                (uint8_t)g_col_judge1, (uint8_t)g_col_ticks1,
                (uint8_t)(g_col_peak1 >> 8), (uint8_t)(g_col_peak1 & 0xFF),
                (uint8_t)(g_col_avg1 >> 8), (uint8_t)(g_col_avg1 & 0xFF),
                (uint8_t)g_col_judge2, (uint8_t)g_col_ticks2,
                (uint8_t)(g_col_peak2 >> 8), (uint8_t)(g_col_peak2 & 0xFF),
                (uint8_t)(g_col_avg2 >> 8), (uint8_t)(g_col_avg2 & 0xFF),
                (uint8_t)g_col_gap};
            printf("[COLLECT] j1=%d t1=%d p1=%d j2=%d t2=%d gap=%d\r\n",
                g_col_judge1, g_col_ticks1, g_col_peak1, g_col_judge2, g_col_ticks2, g_col_gap);
            AOA_Host_Send(buf, 14);
        } else {
            uint8_t aoa_cmd = 0;
            switch (hum_cmd) {
            case HUM_CMD_ENTER:   aoa_cmd = 0x02; break;
            case HUM_CMD_BACK:    aoa_cmd = 0x03; break;
            case HUM_CMD_PLAY:    aoa_cmd = 0x04; break;
            case HUM_CMD_NEXT:    aoa_cmd = 0x05; break;
            case HUM_CMD_PREV:    aoa_cmd = 0x06; break;
            case HUM_CMD_VOL_UP:  aoa_cmd = 0x07; break;
            case HUM_CMD_VOL_DN:  aoa_cmd = 0x08; break;
            case HUM_CMD_RETURN:  aoa_cmd = 0x09; break;
            case HUM_CMD_MODE:    aoa_cmd = 0x0A; break;
            case HUM_CMD_LEFT:    aoa_cmd = 0x0B; break;
            case HUM_CMD_RIGHT:   aoa_cmd = 0x0C; break;
            case HUM_CMD_VOL_MODE: aoa_cmd = 0x0D; break;
            default: break;
            }
            if (aoa_cmd != 0) {
                printf("[HUM] cmd=%d -> AOA 0x%02X\r\n", hum_cmd, aoa_cmd);
                Send_CmdByte(aoa_cmd);
            }
        }
    }
#endif

    AOA_Host_Poll();
}
