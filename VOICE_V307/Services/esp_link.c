/*============================================================================
 * ESP8266 WiFi 调参链路（UART6 ↔ ESP8266 透传 ↔ WiFi UDP）
 *
 * 硬件路径（网表核实 2026-10-16）:
 *   PC0(UART6_TX) --U4通道3--> E_RXD0 ┐ SW3 拨"UART6"位(CTRL0=1)时经
 *   PC1(UART6_RX) --U4通道4--> E_TXD0 ┘ 模拟开关与 ESP8266 接通;
 *   通道 1/2 同时保持 CH340↔USART1 —— 调试打印与 WiFi 共存。
 *
 * 波特率: 2000000(与调试口统一, EEG 家族标准)。ESP8266 侧 ino 同速。
 *
 * 协议 v2 —— 统一二进制成帧(与配置 App/后续所有通信统一):
 *   帧格式: 0xAA | LEN | TYPE | PAYLOAD(LEN B) | CRC8
 *           LEN = TYPE+PAYLOAD 的字节数(不含头/CRC); CRC8 = TYPE 起
 *           逐字节异或(含 TYPE 与 PAYLOAD)。错帧滑动 1 字节重同步。
 *   TYPE: 0x01 PARAM_SET(id+val16 大端)   App→MCU 设置参数
 *         0x02 CMD(sub)                   App→MCU 0=保存 1=恢复出厂
 *         0x03 WAVE(s1_16+s2_16 大端)     MCU→App 压差波形
 *         0x04 PARAM_ECHO(id+val16)       MCU→App 参数应用回执
 * 兼容性: ESP8266 为纯管道无感知; 与旧 V2 字节流(0xFB 裸帧)不兼容,
 *         两端(MCU/App)已同步升级。
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
#define ESP_BAUD        1000000     /* 与调试口统一 2M(EEG 家族标准) */
#define ESP_RX_RING     512         /* RX 环形缓冲(IRQ 写, 主循环读) */
#define ESP_TX_DIV      3           /* 波形包分频: 每 3 tick ≈ 60ms */
#define ESP_DIAG_DIV    100000000          /* 诊断行分频: 每 20 tick = 1s */

#define FRAME_HEAD      0xAA
#define FRAME_MAX_PAY   32
#define TYPE_PARAM_SET  0x01
#define TYPE_CMD        0x02
#define TYPE_WAVE       0x03
#define TYPE_PARAM_ECHO 0x04

/* ---- 运行时状态 -----------------------------------------------------------*/
static volatile uint8_t  s_ring[ESP_RX_RING];
static volatile uint16_t s_head = 0;    /* IRQ 写 */
static volatile uint16_t s_tail = 0;    /* 主循环读 */
static volatile uint32_t s_rx_count = 0;/* IRQ 收到字节总数(诊断) */
static uint32_t  s_rx_drop = 0;         /* 环满丢弃计数 */
static uint32_t  s_frame_ok = 0;        /* CRC 通过帧数(诊断) */
static uint32_t  s_frame_bad = 0;       /* CRC 失败帧数(诊断) */
static uint32_t  s_tx_tmo = 0;          /* UART6 TX 超时次数(诊断) */

/* RX 帧状态机 */
static uint8_t  s_fstate = 0;           /* 0=找头 1=LEN 2=TYPE 3=PAYLOAD 4=CRC */
static uint8_t  s_flen = 0;
static uint8_t  s_ftype = 0;
static uint8_t  s_fcrc = 0;
static uint8_t  s_fpay[FRAME_MAX_PAY];
static uint8_t  s_fidx = 0;

/**
 * @brief CRC8: 从 type 起逐字节异或(含 type 与 payload, 不含 LEN/头)
 */
static uint8_t frame_crc(uint8_t type, const uint8_t *pay, uint8_t len)
{
    uint8_t c = type;
    for (uint8_t i = 0; i < len; i++) {
        c ^= pay[i];
    }
    return c;
}

/**
 * @brief 发送一帧(阻塞式, 5+LEN 字节)
 * ★ TXE 等待加超时保护(1ms): UART6 @2M 发 1 字节 5us, 正常 TXE 置位
 *   远快于超时。超时跳过该字节(数据丢失但不卡死主循环)——防御 ESP8266
 *   端异常导致 UART6 TX 长时间不空的情况(实测单吹超阈值波形卡死场景)
 */
static void esp_send_frame(uint8_t type, const uint8_t *pay, uint8_t len)
{
    uint8_t head[3] = { FRAME_HEAD, (uint8_t)(len + 1), type };
    uint8_t crc = frame_crc(type, pay, len);
    for (int i = 0; i < 3; i++) {
        uint32_t t = 2000;
        while (USART_GetFlagStatus(UART6, USART_FLAG_TXE) == RESET) { if (--t == 0) { s_tx_tmo++; break; } }
        USART_SendData(UART6, head[i]);
    }
    for (uint8_t i = 0; i < len; i++) {
        uint32_t t = 2000;
        while (USART_GetFlagStatus(UART6, USART_FLAG_TXE) == RESET) { if (--t == 0) { s_tx_tmo++; break; } }
        USART_SendData(UART6, pay[i]);
    }
    uint32_t t = 2000;
    while (USART_GetFlagStatus(UART6, USART_FLAG_TXE) == RESET) { if (--t == 0) break; }
    USART_SendData(UART6, crc);
}

/**
 * @brief 全量参数推送: 遍历描述符表逐条发 PARAM_ECHO
 *        (App 连入后主动拉取 / 恢复出厂后同步 —— 确保 App 显示与
 *         固件 SD 存储一致, 不靠 App 侧猜默认值)
 */
static void esp_push_all_params(void)
{
    for (uint8_t id = 0; id < PARAM_ID_NUM; id++) {
        const char *name = 0;
        int32_t v = Params_GetByIndex(id, &name);
        if (name == 0) { continue; }    /* 废弃空槽(如 id 8)不推 */
        uint8_t echo[3] = { id, (uint8_t)((uint32_t)v >> 8), (uint8_t)v };
        esp_send_frame(TYPE_PARAM_ECHO, echo, 3);
    }
}

/**
 * @brief 完整帧处理(CRC 已通过)
 */
static void esp_process_frame(uint8_t type, const uint8_t *pay, uint8_t len)
{
    s_frame_ok++;
    switch (type) {
    case TYPE_PARAM_SET: {
        if (len != 3) return;
        uint8_t id = pay[0];
        int32_t v = ((int32_t)pay[1] << 8) | pay[2];
        if (Params_SetById(id, v) != 0) {
            LOGI("esp", "参数 %u=%ld", id, (long)v);
            uint8_t echo[3] = { id, pay[1], pay[2] };
            esp_send_frame(TYPE_PARAM_ECHO, echo, 3);   /* 回执 */
        } else {
            LOGW("esp", "未知参数 id=%u", id);
        }
        break; }
    case TYPE_CMD: {
        if (len != 1) return;
        if (pay[0] == 0) {
            (void)Params_SD_Save();
            LOGI("esp", "参数已保存");
        } else if (pay[0] == 1) {
            (void)Params_SD_FactoryReset();
            LOGI("esp", "已恢复出厂默认值");
            esp_push_all_params();          /* 出厂值同步推送 */
        } else if (pay[0] == 2) {
            LOGI("esp", "App 请求全量参数");
            esp_push_all_params();          /* App 连入/重连后拉取 */
        } else {
            LOGW("esp", "未知 CMD 子命令 %u", pay[0]);
        }
        break; }
    default:
        break;                          /* 未知类型静默(前向兼容) */
    }
}

/**
 * @brief RX 字节 → 帧状态机(错帧滑动 1 字节重同步)
 */
static void esp_parse_byte(uint8_t b)
{
    switch (s_fstate) {
    case 0:                             /* 找头 */
        if (b == FRAME_HEAD) { s_fstate = 1; }
        break;
    case 1:                             /* LEN = TYPE+PAYLOAD */
        if (b >= 1 && b <= FRAME_MAX_PAY + 1) {
            s_flen = (uint8_t)(b - 1);
            s_fstate = 2;
        } else {
            s_fstate = 0;               /* 非法长度, 重找头 */
        }
        break;
    case 2:                             /* TYPE */
        s_ftype = b; s_fcrc = b; s_fidx = 0;
        s_fstate = (s_flen == 0) ? 4 : 3;
        break;
    case 3:                             /* PAYLOAD */
        if (s_fidx < FRAME_MAX_PAY) { s_fpay[s_fidx++] = b; }
        s_fcrc ^= b;
        if (--s_flen == 0) { s_fstate = 4; }
        break;
    case 4:                             /* CRC */
        if (b == s_fcrc) {
            esp_process_frame(s_ftype, s_fpay, s_fidx);
        } else {
            s_frame_bad++;              /* 错帧: 丢弃 */
        }
        s_fstate = 0;
        break;
    default:
        s_fstate = 0; break;
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

    LOGI("esp", "UART6@2M 就绪, 协议 v2 成帧");
}

/**
 * @brief 消化接收环 → 帧解析; 周期发送波形帧; 诊断输出
 */
void EspLink_Poll(void)
{
    /* RX: 排<空接收环 → 帧状态机 */
    while (s_tail != s_head) {
        uint8_t b = s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1) % ESP_RX_RING);
        esp_parse_byte(b);
    }
    if (s_rx_drop != 0) {
        //LOGW("esp", "RX 环满丢弃 %lu", (unsigned long)s_rx_drop);
        s_rx_drop = 0;
    }
}

void EspLink_SendWave(int32_t p1, int32_t p2)
{
    /* TX: 波形帧 WAVE(s1,s2) — 传感器读一次调一次, 不重复发 */
    uint8_t t[4] = {0};
    int32_t a = p1, b = p2;
    if (a < -32768) { a = -32768; }
    if (a > 32767)  { a = 32767; }
    if (b < -32768) { b = -32768; }
    if (b > 32767)  { b = 32767; }
    t[0] = (uint8_t)((uint32_t)a >> 8); t[1] = (uint8_t)a;
    t[2] = (uint8_t)((uint32_t)b >> 8); t[3] = (uint8_t)b;
    esp_send_frame(TYPE_WAVE, t, 4);

    /* 诊断: 每秒一条(字节/帧 计数与最近波形样本, LOGD) */
    static uint32_t diag_div = 0;
    if (++diag_div >= ESP_DIAG_DIV) {
        diag_div = 0;
        LOGD("esp", "diag rx=%lu ok=%lu bad=%lu tmo=%lu wave=%02X%02X %02X%02X",
             (unsigned long)s_rx_count, (unsigned long)s_frame_ok,
             (unsigned long)s_frame_bad, (unsigned long)s_tx_tmo, t[0], t[1], t[2], t[3]);
    }
}

#else /* !USE_ESP_LINK: 空实现 */

void EspLink_Init(void) { }
void EspLink_Poll(void) { }
void EspLink_SendWave(int32_t p1, int32_t p2) { (void)p1; (void)p2; }

#endif /* USE_ESP_LINK */
