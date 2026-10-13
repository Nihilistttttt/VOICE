#ifndef __VOICE_PROTOCOL_H
#define __VOICE_PROTOCOL_H

#include <stdint.h>
#include "Serial.h"

/*============================================================================
 * AA55 二进制帧协议 (参考 EEG 工程)
 *
 * 帧格式: 0xAA 0x55  body[...]  0x7E
 *   body 内若出现 0x7D(ESC) 则下一字节 XOR 0x20 还原
 *
 * body 结构 (最少 10 字节):
 *   body[0]      : 保留 (帧序号)
 *   body[1]      : cmd  命令码
 *   body[2..3]   : pay_len (小端)  payload 长度
 *   body[4..7]   : 保留 (4 字节)
 *   body[8..8+pay_len-1]        : payload
 *   body[8+pay_len..8+pay_len+1]: checksum16 (小端, 对 body[0..8+pay_len-1] 求和)
 *============================================================================*/

#define AA55_HDR0  0xAA
#define AA55_HDR1  0x55
#define AA55_TAIL  0x7E
#define AA55_ESC   0x7D
#define AA55_XOR   0x20

#define AA55_BODY_MAX   256
#define AA55_PAYLOAD_MAX (AA55_BODY_MAX - 10)

/*============================================================================
 * 命令码定义
 *============================================================================*/
#define CMD_PING           0x01
#define CMD_PING_OK        0x02

#define CMD_CONNECT        0x10
#define CMD_CONNECT_OK     0x11
#define CMD_DISCONNECT     0x12
#define CMD_DISCONNECT_OK  0x13

#define CMD_TAP            0x20
#define CMD_TAP_OK         0x21
#define CMD_SWIPE          0x22
#define CMD_SWIPE_OK       0x23
#define CMD_KEY            0x24
#define CMD_KEY_OK         0x25
#define CMD_TEXT           0x26
#define CMD_TEXT_OK        0x27

#define CMD_SCREENSHOT     0x30
#define CMD_GET_INFO       0x31
#define CMD_GET_BATTERY    0x32

#define CMD_GPIO_SET       0x40
#define CMD_GPIO_RESET     0x41
#define CMD_GPIO_TOGGLE    0x42
#define CMD_GPIO_READ      0x43

#define CMD_ERR            0xFF

/*============================================================================
 * 协议接口
 *============================================================================*/
void Voice_Protocol_Init(void);
void Voice_Protocol_Feed(uint8_t b);
void Voice_Protocol_FeedBuffer(const uint8_t *buf, uint16_t len);

void Voice_Protocol_SendFrame(Serial_Port port, uint8_t cmd,
                              const uint8_t *payload, uint16_t len);
void Voice_Protocol_SendError(Serial_Port port, uint8_t err_code);

/*============================================================================
 * 命令处理回调 (由 phone_control.c 实现)
 *  cmd        : 命令码
 *  payload    : 负载指针
 *  payload_len: 负载长度
 *============================================================================*/
void Voice_Protocol_OnFrame(uint8_t cmd, const uint8_t *payload, uint16_t payload_len);

#endif /* __VOICE_PROTOCOL_H */