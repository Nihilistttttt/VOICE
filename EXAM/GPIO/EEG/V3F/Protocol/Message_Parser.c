#include "Message_Parser.h"
#include "Config.h"


extern void Parse_CommandBinary(const uint8_t *payload, uint16_t len, const char *source);

DisplayConfig_t g_display_config;

void DisplayConfig_SetDefaults(DisplayConfig_t *cfg)
{
    uint8_t i;
    for (i = 0; i < DISPLAY_NUM_CH; i++) {
        cfg->wave_ch[i] = i;
        cfg->wave_type[i] = WAVE_TYPE_FILT;
        cfg->spec_type[i] = SPEC_TYPE_NONE;
    }
}

uint8_t DisplayConfig_GetWaveCmd(uint8_t wave_type)
{
    switch (wave_type) {
    case WAVE_TYPE_FILT:     return PROTO_WAVE_FILT;
    case WAVE_TYPE_BASELINE: return PROTO_WAVE_BASELINE;
    default:                 return PROTO_WAVE_RAW;
    }
}

uint8_t DisplayConfig_GetSpectrumCmd(uint8_t spec_type, uint8_t ch)
{
    (void)ch;
    switch (spec_type) {
    case SPEC_TYPE_FREQ_FILTER:  return PROTO_SPEC_FREQ_FILTER;
    case SPEC_TYPE_TIME_FILTER:  return PROTO_SPEC_TIME_FILTER;
    default:                     return PROTO_SPEC_RAW;
    }
}

uint8_t DisplayConfig_NeedsBandpass(uint8_t ch)
{
    uint8_t i;
    for (i = 0; i < DISPLAY_NUM_CH; i++) {
        if (g_display_config.wave_ch[i] == ch) {
            if (g_display_config.wave_type[i] == WAVE_TYPE_FILT) return 1u;
            if (g_display_config.spec_type[i] == SPEC_TYPE_TIME_FILTER) return 1u;
        }
    }
    return 0u;
}

uint8_t DisplayConfig_NeedsRawFFT(uint8_t ch)
{
    uint8_t i;
    for (i = 0; i < DISPLAY_NUM_CH; i++) {
        if (g_display_config.wave_ch[i] == ch) {
            if (g_display_config.spec_type[i] == SPEC_TYPE_RAW) return 1u;
            if (g_display_config.spec_type[i] == SPEC_TYPE_FREQ_FILTER) return 1u;
        }
    }
    return 0u;
}

uint8_t DisplayConfig_NeedsFiltFFT(uint8_t ch)
{
    uint8_t i;
    for (i = 0; i < DISPLAY_NUM_CH; i++) {
        if (g_display_config.wave_ch[i] == ch) {
            if (g_display_config.spec_type[i] == SPEC_TYPE_TIME_FILTER) return 1u;
        }
    }
    return 0u;
}

uint8_t DisplayConfig_IsAllNone(void)
{
    uint8_t i;
    for (i = 0; i < DISPLAY_NUM_CH; i++) {
        if (g_display_config.wave_type[i] != WAVE_TYPE_NONE) return 0u;
        if (g_display_config.spec_type[i] != SPEC_TYPE_NONE) return 0u;
    }
    return 1u;
}

static uint8_t payload_buf_debug[SERIAL_DEBUG_RX_BUF_SIZE];
static uint8_t payload_buf_wifi[SERIAL_WIFI_RX_BUF_SIZE];

typedef struct {
    uint8_t  state;
    uint8_t *payload;
    uint16_t capacity;
    uint16_t len;
} FrameParser;

static FrameParser parser_debug = {
    .state = 0,
    .payload = payload_buf_debug,
    .capacity = SERIAL_DEBUG_RX_BUF_SIZE,
    .len = 0
};
static FrameParser parser_wifi = {
    .state = 0,
    .payload = payload_buf_wifi,
    .capacity = SERIAL_WIFI_RX_BUF_SIZE,
    .len = 0
};

static inline FrameParser* get_parser(Serial_Port port) {
    return (port == SERIAL_PORT_DEBUG) ? &parser_debug : &parser_wifi;
}

static void Parse_Frame_Dispatch(FrameParser *p)
{
    uint8_t *body = p->payload;
    uint16_t body_len = p->len;
    uint16_t payload_len;
    uint16_t full_len;
    uint16_t crc;
    uint16_t calc;

    if (body_len < PROTO_BODY_BASE + PROTO_CRC_LEN) return;

    payload_len = (uint16_t)(body[PROTO_ADDR_LEN + PROTO_CMD_LEN]
                             | (body[PROTO_ADDR_LEN + PROTO_CMD_LEN + 1] << 8));
    full_len = PROTO_BODY_BASE + payload_len + PROTO_CRC_LEN;
    if (body_len != full_len) return;

    crc = (uint16_t)(body[body_len - 2] | (body[body_len - 1] << 8));
    calc = Proto_Checksum16(body, body_len - PROTO_CRC_LEN);
    if (crc != calc) return;

    const char *source = (p == &parser_debug) ? "PY" : "AND";
    Parse_CommandBinary(&body[PROTO_BODY_BASE], payload_len, source);
}

static void Parse_Frame(FrameParser *p, uint8_t byte)
{
    switch (p->state) {
        case 0:
            if (byte == PROTO_FRAME_HEADER0) {
                p->state = 1;
            }
            break;

        case 1:
            if (byte == PROTO_FRAME_HEADER1) {
                p->state = 2;
                p->len = 0;

            } else if (byte == PROTO_FRAME_HEADER0) {
                p->state = 1;
            } else {
                p->state = 0;
            }
            break;

        case 2:
            if (byte == PROTO_ESCAPE_CHAR) {
                p->state = 3;
            } else if (byte == PROTO_FRAME_TAIL) {
                Parse_Frame_Dispatch(p);
                p->state = 0;
                p->len = 0;
            } else {
                if (p->len < p->capacity) {
                    p->payload[p->len++] = byte;
                } else {
                    p->state = 0;
                }
            }
            break;

        case 3:
            {
                uint8_t original = byte ^ PROTO_ESCAPE_XOR;
                if (p->len < p->capacity) {
                    p->payload[p->len++] = original;
                } else {
                    p->state = 0;
                    break;
                }
                p->state = 2;
            }
            break;
    }

}

void Parse_Serial_Data(Serial_Port port) {
    uint8_t *buf;
    uint16_t len;
    if (Serial_IsDataReady(port) == 0) return;
    len = Serial_GetDataPacket(port, &buf);
    FrameParser *p = get_parser(port);
    for (uint16_t i = 0; i < len; i++) {
        Parse_Frame(p, buf[i]);
    }
}

static void Emit_Escaped(uint8_t *frame, uint16_t *idx, uint8_t b)
{
    if (b == PROTO_FRAME_HEADER0 || b == PROTO_FRAME_HEADER1
        || b == PROTO_FRAME_TAIL || b == PROTO_ESCAPE_CHAR) {
        frame[(*idx)++] = PROTO_ESCAPE_CHAR;
        frame[(*idx)++] = b ^ PROTO_ESCAPE_XOR;
    } else {
        frame[(*idx)++] = b;
    }
}

void Pack_Frame(Serial_Port port, uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    static uint8_t frame[TX_MESSAGE_BUF_SIZE];
    uint16_t idx = 0;
    uint16_t i;
    uint32_t ts = Proto_GetTimestampMs();

    frame[idx++] = PROTO_FRAME_HEADER0;
    frame[idx++] = PROTO_FRAME_HEADER1;

    Emit_Escaped(frame, &idx, PROTO_ADDR_MCU);
    Emit_Escaped(frame, &idx, cmd);
    Emit_Escaped(frame, &idx, (uint8_t)(len & 0xFF));
    Emit_Escaped(frame, &idx, (uint8_t)((len >> 8) & 0xFF));
    Emit_Escaped(frame, &idx, (uint8_t)(ts & 0xFF));
    Emit_Escaped(frame, &idx, (uint8_t)((ts >> 8) & 0xFF));
    Emit_Escaped(frame, &idx, (uint8_t)((ts >> 16) & 0xFF));
    Emit_Escaped(frame, &idx, (uint8_t)((ts >> 24) & 0xFF));

    for (i = 0; i < len && idx < TX_MESSAGE_BUF_SIZE - 8; i++) {
        Emit_Escaped(frame, &idx, payload[i]);
    }

    {
        uint8_t body[PROTO_BODY_BASE];
        uint16_t crc;
        body[0] = PROTO_ADDR_MCU;
        body[1] = cmd;
        body[2] = (uint8_t)(len & 0xFF);
        body[3] = (uint8_t)((len >> 8) & 0xFF);
        body[4] = (uint8_t)(ts & 0xFF);
        body[5] = (uint8_t)((ts >> 8) & 0xFF);
        body[6] = (uint8_t)((ts >> 16) & 0xFF);
        body[7] = (uint8_t)((ts >> 24) & 0xFF);
        crc = Proto_Checksum16(body, sizeof(body));
        for (i = 0; i < len; i++) {
            crc = (uint16_t)(crc + payload[i]);
        }
        Emit_Escaped(frame, &idx, (uint8_t)(crc & 0xFF));
        Emit_Escaped(frame, &idx, (uint8_t)((crc >> 8) & 0xFF));
    }

    frame[idx++] = PROTO_FRAME_TAIL;

    Serial_SendArray_DMA(port, frame, idx);
}

void Send_RespCommand(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    Pack_Frame(SERIAL_PORT_DEBUG, cmd, payload, len);
    Pack_Frame(SERIAL_PORT_WIFI, cmd, payload, len);
}

void Send_WaveformBatch(uint8_t wave_type, const float vals[DISPLAY_MAX_CH])
{
    uint8_t payload[PROTO_WAVE_PAYLOAD];
    uint8_t i;
    payload[0] = wave_type;
    for (i = 0; i < DISPLAY_MAX_CH; i++) {
        memcpy(payload + PROTO_WAVE_HEADER + i * 4, &vals[i], 4);
    }
    Pack_Frame(SERIAL_PORT_WIFI, CMD_WAVE, payload, PROTO_WAVE_PAYLOAD);
}

void Send_Spectrum(uint8_t ch, uint8_t spectrum_type, float *mag, uint8_t frag_idx)
{
    const uint8_t TOTAL_FRAGS = 32;
    const uint16_t FLOAT_PER_FRAG = 128 / TOTAL_FRAGS;
    const uint16_t FRAG_SIZE = FLOAT_PER_FRAG * sizeof(float);

    if (frag_idx >= TOTAL_FRAGS) return;

    uint16_t offset = frag_idx * FLOAT_PER_FRAG;
    uint8_t payload[PROTO_SPECTRUM_HEADER + PROTO_SPECTRUM_FRAG_SIZE];
    payload[0] = ch;
    payload[1] = spectrum_type;
    payload[2] = frag_idx;
    payload[3] = TOTAL_FRAGS;
    memcpy(payload + PROTO_SPECTRUM_HEADER, mag + offset, FRAG_SIZE);

    Pack_Frame(SERIAL_PORT_WIFI, CMD_SPECTRUM, payload, PROTO_SPECTRUM_HEADER + FRAG_SIZE);
}

void Send_Focus(float attn0, float attn1, float ema0, float ema1,
                uint8_t trend_state, uint8_t instant_state) {
    uint8_t payload[PROTO_FOCUS_PAYLOAD];
    uint16_t off = 0;
    memcpy(payload + off, &attn0, 4); off += 4;
    memcpy(payload + off, &attn1, 4); off += 4;
    memcpy(payload + off, &ema0,  4); off += 4;
    memcpy(payload + off, &ema1,  4); off += 4;
    memcpy(payload + off, &trend_state, 1); off += 1;
    memcpy(payload + off, &instant_state, 1);
    Pack_Frame(SERIAL_PORT_WIFI, CMD_FOCUS, payload, PROTO_FOCUS_PAYLOAD);
}
