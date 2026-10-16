#ifndef __ESP_LINK_H__
#define __ESP_LINK_H__

#include <stdint.h>

/*============================================================================
 * ESP8266 WiFi 调参链路（UART6 ↔ ESP8266 透传 ↔ WiFi）
 *
 * 硬件路径（网表核实 2026-10-16）:
 *   PC0(UART6_TX) --U4通道3--> E_RXD0   ┐ SW3 拨到"UART6"位(CTRL0=1)时
 *   PC1(UART6_RX) --U4通道4--> E_TXD0   ┘ 经模拟开关与 ESP8266 接通;
 *   同时通道 1/2 保持 CH340↔USART1 —— 调试打印与 WiFi 通道共存。
 *   (SW3 拨另一位则 CH340 直连 ESP8266, 用于 PC 直接烧录/配置模组)
 *
 * 协议(MCU↔手机, ESP8266 为透明管道):
 *   手机→MCU: 0xFB+id+vH+vL  参数下行(直通 Params_SetById)
 *             0xF9+sub        sub=0 保存参数 / sub=1 恢复出厂
 *   MCU→手机: 0xFE+s1(2B)+s2(2B)  压差波形包(3 分频≈60ms, int16 Pa)
 *
 * 门控: USE_ESP_LINK(Config.h); 关闭时编译为空。
 *============================================================================*/

/**
 * @brief 初始化 UART6(PC0/PC1 @115200 8N1) + RXNE 中断 + 接收环形缓冲
 */
void EspLink_Init(void);

/**
 * @brief 每 tick 轮询: 消化接收环→协议解析; 周期发送压差波形包
 *
 * @param p1 当前 s1 压力 Pa
 * @param p2 当前 s2 压力 Pa
 */
void EspLink_Poll(int32_t p1, int32_t p2);

#endif /* __ESP_LINK_H__ */
