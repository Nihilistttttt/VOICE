/*============================================================================
 * ESP8266 WiFi 调参链路（UART6 ↔ ESP8266 透传 ↔ WiFi UDP）
 *
 * 硬件路径（网表核实 2026-10-16）:
 *   PC0(UART6_TX) --U4通道3--> E_RXD0 ┐ SW3 拨"UART6"位(CTRL0=1)时经
 *   PC1(UART6_RX) --U4通道4--> E_TXD0 ┘ 模拟开关与 ESP8266 接通;
 *   通道 1/2 同时保持 CH340↔USART1 —— 调试打印与 WiFi 共存。
 *
 * 波特率: 2000000(与调试口统一, EEG 家族标准)。ESP8266 侧 ino 同速。
 * 协议(MCU↔手机, ESP8266 为透明管道; 手机经 WiFi UDP:8888 收发):
 *   手机→MCU: 0xFB+id+vH+vL  参数下行(直通 Params_SetById, min/max 钳位)
 *             0xF9+sub        sub=0 保存参数 / sub=1 恢复出厂
 *   MCU→手机: 0xFE+s1(2B)+s2(2B)  压差波形包(3 分频≈60ms, int16 Pa)
 * 兼容性: 与 V2 AOA 字节流协议同构, 旧 App 逻辑可平滑迁移。
 *
 * 诊断(临时): EspLink_Poll 每秒输出 rx 计数与 tx 样本, 用于隔离
 * UART6 与 U4 通道 3/4 的链路故障(2026-10-16)。
 *============================================================================*/

#include "esp_link.h"
#include "Config.h"

#if USE_ESP_LINK

#include "debug.h"
#include "params.h"
#include "params_sd.h"
#include "log.h"
#include <string.h>

/* ---- 常量 ---------------------------------------------------------------*/
#define ESP_BAUD        2000000     /* 与调试口统一 2M(EEG 家族标准) */
#define ESP_RX_RING     512         /* RX 环形缓冲(IRQ 写, 主循环读) */
#define ESP_TX_DIV      3           /* 波形包分频: 每 3 tick ≈ 60ms */
#define ESP_DIAG_DIV    20          /* 诊断行分频: 每 20 tick = 1s */

/* ---- 运行时状态 -----------------------------------------------------------*/
static volatile uint8_t  s_ring[ESP_RX_RING];
static volatile uint16_t s_head = 0;    /* IRQ 写 */
static volatile uint16_t s_tail = 0;    /* 主循环读 */
static volatile uint32_t s_rx_count = 0;/* IRQ 收到字节总数(诊断) */
static uint8_t   s_pstate = 0;          /* RX 状态: 0=头 1=id 2=vH 3=vL 4=F9子命令 */
static uint8_t   s_pid = 0;
static uint8_t   s_pvh = 0;
static uint32_t  s_rx_drop = 0;         /* 环满丢弃计数 */

/**
 * @brief RX 单字节处理（字节流状态机, 与 V2 phone_control 同构）
 */
static void esp_parse_byte(uint8_t b)
{
    switch (s_pstate) {
    case 0:
        if (b == 0xFB)      { s_pstate = 1; }
        else if (b == 0xF9) { s_pstate = 4; }
        break;
    case 1: s_pid = b; s_pstate = 2; break;
    case 2: s_pvh = b; s_pstate = 3; break;
    case 3: {
        int32_t v = ((int32_t)s_pvh << 8) | b;
        if (Params_SetById(s_pid, v) != 0) {
            LOGI("esp", "参数 %u=%ld", s_pid, (long)v);
        } else {
            LOGW("esp", "未知参数 id=%u", s_pid);
        }
        s_pstate = 0; break; }
    case 4:
        if (b == 0) {
            (void)Params_SD_Save();
            LOGI("esp", "参数已保存");
        } else if (b == 1) {
            (void)Params_SD_FactoryReset();
            LOGI("esp", "已恢复出厂默认值");
        } else {
            LOGW("esp", "未知 0xF9 子命令 %u", b);
        }
        s_pstate = 0; break;
    default:
        s_pstate = 0; break;
    }
}

/**
 * @brief UART6 接收中断: 环形缓冲入队(满则丢弃并计数)
 */
void UART6_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART6_IRQHandler(void)
{
    if (USART_GetFlagStatus(UART6, USART_FLAG_RXNE) != RESET) {
        uint8_t b = (uint8_t)USART_ReceiveData(UART6);
        uint16_t next = (uint16_t)((s_head + 1) % ESP_RX_RING);
        if (next != s_tail) {
            s_ring[s_head] = b;
            s_head = next;
        } else {
            s_rx_drop++;                /* 环满: 丢弃并计数 */
        }
        s_rx_count++;
    }
}

/**
 * @brief 初始化 UART6(PC0 TX/PC1 RX @2M 8N1) + RXNE 中断
 */
void EspLink_Init(void)
{
    GPIO_ClockEnable(2);                                /* GPIOC */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART6, ENABLE);

    Hal_GPIO_Init(HAL_GPIO_PIN(2, 0), HAL_GPIO_MODE_AF_PP,
                 HAL_GPIO_SPEED_HIGH, 0);               /* PC0 TX */
    Hal_GPIO_Init(HAL_GPIO_PIN(2, 1), HAL_GPIO_MODE_INPUT,
                 HAL_GPIO_SPEED_HIGH, 0);               /* PC1 RX */

    USART_InitTypeDef cfg;
    USART_StructInit(&cfg);
    cfg.USART_BaudRate = ESP_BAUD;
    cfg.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(UART6, &cfg);
    USART_ITConfig(UART6, USART_IT_RXNE, ENABLE);
    USART_Cmd(UART6, ENABLE);
    NVIC_EnableIRQ(UART6_IRQn);

    LOGI("esp", "UART6@2M 就绪(PC0/PC1)");
}

/**
 * @brief 消化接收环 → 协议解析; 周期发送压差波形包; 诊断输出
 */
void EspLink_Poll(int32_t p1, int32_t p2)
{
    /* RX: 排空接收环 */
    while (s_tail != s_head) {
        uint8_t b = s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1) % ESP_RX_RING);
        esp_parse_byte(b);
    }
    if (s_rx_drop != 0) {
        LOGW("esp", "RX 环满丢弃 %lu", (unsigned long)s_rx_drop);
        s_rx_drop = 0;
    }

    /* TX: 波形包 0xFE + s1 + s2 (int16 饱和钳位, 32 位运算防溢出) */
    static uint32_t tx_div = 0;
    static uint8_t  last_tx[5] = {0};
    uint8_t  t[5] = {0};
    uint8_t  tx_now = 0;

    if (++tx_div >= ESP_TX_DIV) {
        tx_div = 0;
        int32_t a = p1, b = p2;
        if (a < -32768) { a = -32768; }
        if (a > 32767)  { a = 32767; }
        if (b < -32768) { b = -32768; }
        if (b > 32767)  { b = 32767; }
        t[0] = 0xFE;
        t[1] = (uint8_t)((uint32_t)a >> 8); t[2] = (uint8_t)a;
        t[3] = (uint8_t)((uint32_t)b >> 8); t[4] = (uint8_t)b;
        for (int i = 0; i < 5; i++) {
            while (USART_GetFlagStatus(UART6, USART_FLAG_TXE) == RESET) { }
            USART_SendData(UART6, t[i]);
        }
        tx_now = 1;
    }

    /* 诊断: 每秒一条 rx 计数与最近 TX 样本(LOGD, log=3 时可见) */
    static uint32_t diag_div = 0;
    if (++diag_div >= ESP_DIAG_DIV) {
        diag_div = 0;
        if (tx_now) {
            memcpy(last_tx, t, 5);
        }
        LOGD("esp", "diag rx=%lu drop=%lu tx=%02X %02X %02X %02X %02X%s",
             (unsigned long)s_rx_count, (unsigned long)s_rx_drop,
             last_tx[0], last_tx[1], last_tx[2], last_tx[3], last_tx[4],
             tx_now ? "" : " (本秒未发)");
    }
}

#else /* !USE_ESP_LINK: 空实现 */

void EspLink_Init(void) { }
void EspLink_Poll(int32_t p1, int32_t p2) { (void)p1; (void)p2; }

#endif /* USE_ESP_LINK */
