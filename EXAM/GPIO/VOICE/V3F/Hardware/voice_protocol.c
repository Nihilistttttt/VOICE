#include "voice_protocol.h"

static uint8_t  s_aa55_body[AA55_BODY_MAX];
static uint16_t s_aa55_body_len;
static uint8_t  s_aa55_state;

static uint16_t checksum16(const uint8_t *d, uint16_t n)
{
    uint32_t s = 0;
    for (uint16_t i = 0; i < n; i++) s += d[i];
    return (uint16_t)(s & 0xFFFF);
}

void Voice_Protocol_Init(void)
{
    s_aa55_state    = 0;
    s_aa55_body_len = 0;
}

void Voice_Protocol_Feed(uint8_t b)
{
    switch (s_aa55_state) {
    case 0:
        if (b == AA55_HDR0)
            s_aa55_state = 1;
        break;
    case 1:
        if (b == AA55_HDR1) {
            s_aa55_state    = 2;
            s_aa55_body_len = 0;
        } else if (b == AA55_HDR0) {
            /* stay in state 1 */
        } else {
            s_aa55_state = 0;
        }
        break;
    case 2:
        if (b == AA55_ESC) {
            s_aa55_state = 3;
        } else if (b == AA55_TAIL) {
            if (s_aa55_body_len >= 10) {
                uint8_t  cmd      = s_aa55_body[1];
                uint16_t pay_len  = (uint16_t)s_aa55_body[2] | ((uint16_t)s_aa55_body[3] << 8);
                if (s_aa55_body_len >= (uint16_t)(8 + pay_len + 2)) {
                    uint16_t calc = checksum16(s_aa55_body, 8 + pay_len);
                    uint16_t recv = (uint16_t)s_aa55_body[8 + pay_len]
                                  | ((uint16_t)s_aa55_body[8 + pay_len + 1] << 8);
                    if (calc == recv) {
                        const uint8_t *payload = s_aa55_body + 8;
                        Voice_Protocol_OnFrame(cmd, payload, pay_len);
                    }
                }
            }
            s_aa55_state = 0;
        } else {
            if (s_aa55_body_len < sizeof(s_aa55_body)) {
                s_aa55_body[s_aa55_body_len++] = b;
            } else {
                s_aa55_state = 0;
            }
        }
        break;
    case 3:
        if (s_aa55_body_len < sizeof(s_aa55_body)) {
            s_aa55_body[s_aa55_body_len++] = b ^ AA55_XOR;
        }
        s_aa55_state = 2;
        break;
    }
}

void Voice_Protocol_FeedBuffer(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        Voice_Protocol_Feed(buf[i]);
    }
}

void Voice_Protocol_SendFrame(Serial_Port port, uint8_t cmd,
                              const uint8_t *payload, uint16_t len)
{
    uint8_t  body[AA55_BODY_MAX];
    uint16_t body_len = 0;
    uint16_t csum;

    if (len > AA55_PAYLOAD_MAX) len = AA55_PAYLOAD_MAX;

    body[0] = 0;                /* 保留 */
    body[1] = cmd;
    body[2] = (uint8_t)(len & 0xFF);
    body[3] = (uint8_t)(len >> 8);
    body[4] = 0;                /* 保留 */
    body[5] = 0;
    body[6] = 0;
    body[7] = 0;
    if (len > 0 && payload != NULL) {
        memcpy(&body[8], payload, len);
    }
    csum = checksum16(body, 8 + len);
    body[8 + len]     = (uint8_t)(csum & 0xFF);
    body[8 + len + 1] = (uint8_t)(csum >> 8);
    body_len = 8 + len + 2;

    /* 发送帧头 */
    Serial_SendByte(port, AA55_HDR0);
    Serial_SendByte(port, AA55_HDR1);

    /* 发送 body (转义处理) */
    for (uint16_t i = 0; i < body_len; i++) {
        uint8_t b = body[i];
        if (b == AA55_ESC || b == AA55_HDR0 || b == AA55_HDR1 || b == AA55_TAIL) {
            Serial_SendByte(port, AA55_ESC);
            Serial_SendByte(port, b ^ AA55_XOR);
        } else {
            Serial_SendByte(port, b);
        }
    }

    /* 发送帧尾 */
    Serial_SendByte(port, AA55_TAIL);
}

void Voice_Protocol_SendError(Serial_Port port, uint8_t err_code)
{
    Voice_Protocol_SendFrame(port, CMD_ERR, &err_code, 1);
}