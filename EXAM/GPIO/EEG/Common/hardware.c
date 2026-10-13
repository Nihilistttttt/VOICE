/********************************** (C) COPYRIGHT *******************************
 * File Name          : hardware.c
 * Description        : Top-level hardware mode selection.
 *                      v2.7: dual-core IPC and V5F EEG backend moved out of
 *                      this file into Common/dualcore_ipc.c.
 *                      GLXSS: SD card firmware upload (V3F) + V5F display drive.
 *******************************************************************************/

#include "hardware.h"

#if defined(Core_V3F)
#include "sd_card.h"
#include "OLED.h"
#include "signal_analysis.h"
#include "ADS1299.h"
#include "Serial.h"
#include "ICM42605.h"
#include "glxss_me.h"
#include "glxss_sd_fw.h"
#include "max98357a.h"
#include "wav_player.h"
#ifdef GLXSS_ENABLED
#include "ipc_log.h"
#include "Message_Parser.h"
#include <string.h>
#endif
#endif


#if defined(Core_V3F) && defined(GLXSS_ENABLED)
#define AA55_HDR0 0xAA
#define AA55_HDR1 0x55
#define AA55_TAIL 0x7E
#define AA55_ESC 0x7D
#define AA55_XOR 0x20


static uint8_t s_aa55_body[256];
static uint16_t s_aa55_body_len;
static uint8_t s_aa55_state;
static uint8_t g_app_mode;

static uint16_t checksum16 (const uint8_t *d, uint16_t n) {
    uint32_t s = 0;
    for (uint16_t i = 0; i < n; i++) s += d[i];
    return (uint16_t)(s & 0xFFFF);
}

static void process_aa55_frame (const uint8_t *body, uint16_t body_len) {
    if (body_len < 10)
        return;
    uint8_t cmd = body[1];
    uint16_t pay_len = (uint16_t)body[2] | ((uint16_t)body[3] << 8);
    if (body_len < 8 + pay_len + 2)
        return;
    uint16_t calc = checksum16 (body, 8 + pay_len);
    uint16_t recv = (uint16_t)body[8 + pay_len] | ((uint16_t)body[8 + pay_len + 1] << 8);
    if (calc != recv)
        return;
    const uint8_t *payload = body + 8;
    uint8_t sub_cmd = (pay_len > 0) ? payload[0] : 0;
    const uint8_t *data = payload + 1;
    uint16_t data_len = (pay_len > 0) ? pay_len - 1 : 0;

    Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] RX cmd=0x%02X sub=0x%02X len=%u\r\n",
                   cmd, sub_cmd, (unsigned)data_len);

    switch (cmd) {
    case CMD_MODE_SET:
        if (data_len >= 1) {
            uint8_t mode = data[0];
            Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] MODE_SET mode=%u (1=collect 2=infer 3=csp)\r\n", (unsigned)mode);
            if (mode == 1) {
                g_app_mode = 1;
                IPC_Cmd_Send_V3F (IPC_CMD_COLLECT, 1);
            } else if (mode == 2) {
                g_app_mode = 2;
                IPC_Cmd_Send_V3F (IPC_CMD_MI_INFER, 0);
            } else if (mode == 3) {
                g_app_mode = 1;
                IPC_Cmd_Send_V3F (IPC_CMD_COLLECT, 3);
            }
            {
                uint8_t resp[2] = {CMD_MODE_SET, mode};
                Pack_Frame (SERIAL_PORT_WIFI, CMD_MODE_SET_OK, resp, 2);
            }
        }
        break;
    case CMD_MODE_TRAIN:
        Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] MODE_TRAIN -> COLLECT\r\n");
        g_app_mode = 1;
        IPC_Cmd_Send_V3F (IPC_CMD_COLLECT, 1);
        {
            uint8_t resp[1] = {0};
            Pack_Frame (SERIAL_PORT_WIFI, CMD_READY_TRAIN, resp, 1);
        }
        break;
    case CMD_MODE_TEST:
        Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] MODE_TEST -> MI_INFER\r\n");
        g_app_mode = 2;
        IPC_Cmd_Send_V3F (IPC_CMD_MI_INFER, 0);
        {
            uint8_t resp[1] = {0};
            Pack_Frame (SERIAL_PORT_WIFI, CMD_READY_TEST, resp, 1);
        }
        break;
    case CMD_TRIAL:
        if (data_len >= 1) {
            uint8_t side = data[0];
            Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] TRIAL side=%u\r\n", (unsigned)side);
            if (g_app_mode == 1) {
                Serial_Printf (SERIAL_PORT_DEBUG, "[V3F]   (collect mode, keep train)\r\n");
            } else {
                IPC_Cmd_Send_V3F (IPC_CMD_ARROW, side);
            }
            {
                uint8_t resp[1] = {side};
                Pack_Frame (SERIAL_PORT_WIFI, CMD_TASK_START, resp, 1);
            }
        }
        break;
    case CMD_STOP:
        Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] STOP\r\n");
        g_app_mode = 0;
        IPC_Ctrl_SetFlags_V3F (0);
        IPC_Cmd_Send_V3F (IPC_CMD_RESET, 0);
        {
            uint8_t resp[1] = {0};
            Pack_Frame (SERIAL_PORT_WIFI, CMD_TASK_STOPPED, resp, 1);
        }
        break;
    case CMD_DISPLAY_CFG:
        if (data_len >= 1) {
            uint8_t display_mode = data[0];
            Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] DISPLAY_CFG mode=%u\r\n", (unsigned)display_mode);
            if (display_mode == 0)
                IPC_Cmd_Send_V3F (IPC_CMD_SSVEP, 0);
            else if (display_mode == 1)
                IPC_Cmd_Send_V3F (IPC_CMD_ARROW, 0);
            else if (display_mode == 2)
                IPC_Cmd_Send_V3F (IPC_CMD_ARROW_TRAIN, 0);
            Pack_Frame (SERIAL_PORT_WIFI, CMD_DISPLAY_CFG, data, data_len);
        }
        break;
    case CMD_SSVEP_START:
        g_app_mode = 0;
        if (data_len >= 1) {
            uint8_t freq_idx = data[0];
            Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] SSVEP_START freq_idx=%u\r\n", (unsigned)freq_idx);
            IPC_Ctrl_SetFlags_V3F (IPC_CTRL_SSVEP_ENABLE);
            IPC_Cmd_Send_V3F (IPC_CMD_SSVEP, freq_idx);
        } else {
            IPC_Ctrl_SetFlags_V3F (IPC_CTRL_SSVEP_ENABLE);
            IPC_Cmd_Send_V3F (IPC_CMD_SSVEP, 0);
        }
        {
            uint8_t resp[1] = {0};
            Pack_Frame (SERIAL_PORT_WIFI, CMD_SSVEP_START, resp, 1);
        }
        wav_player_play ("focus.wav");
        break;
    case CMD_SSVEP_STOP:
        Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] SSVEP_STOP -> RESET\r\n");
        g_app_mode = 0;
        IPC_Ctrl_SetFlags_V3F (0);
        IPC_Cmd_Send_V3F (IPC_CMD_RESET, 0);
        {
            uint8_t resp[1] = {0};
            Pack_Frame (SERIAL_PORT_WIFI, CMD_SSVEP_STOP, resp, 1);
        }
        break;
    default:
        Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] unhandled cmd=0x%02X\r\n", cmd);
        break;
    }
}

static void __attribute__ ((unused)) aa55_feed (uint8_t b) {
    switch (s_aa55_state) {
    case 0:
        if (b == AA55_HDR0)
            s_aa55_state = 1;
        break;
    case 1:
        if (b == AA55_HDR1) {
            s_aa55_state = 2;
            s_aa55_body_len = 0;
        } else if (b == AA55_HDR0) {
        } else
            s_aa55_state = 0;
        break;
    case 2:
        if (b == AA55_ESC)
            s_aa55_state = 3;
        else if (b == AA55_TAIL) {
            if (s_aa55_body_len >= 10)
                process_aa55_frame (s_aa55_body, s_aa55_body_len);
            s_aa55_state = 0;
        } else {
            if (s_aa55_body_len < sizeof (s_aa55_body))
                s_aa55_body[s_aa55_body_len++] = b;
            else
                s_aa55_state = 0;
        }
        break;
    case 3:
        if (s_aa55_body_len < sizeof (s_aa55_body))
            s_aa55_body[s_aa55_body_len++] = b ^ AA55_XOR;
        s_aa55_state = 2;
        break;
    }
}
#endif

void Hardware (void) {
#if defined(Core_V3F)
#ifndef EEG_OLED_DISABLED
    OLED_Init();
    OLED_Clear_All();
#endif

#if (SYSTEM_MODE == MODE_EEG_ANALYSIS)
    /* GLXSS_ENABLED: glasses firmware upload + IPC shared mem init done in
     * V3F main.c BEFORE waking V5F, so V5F can take USB Host cleanly. */
    Signal_Analysis_Start();

#elif (SYSTEM_MODE == MODE_SPI_TEST)
    Serial_Init (SERIAL_PORT_DEBUG);
    OLED_ShowHexNum (0, 0, ADS1299_Init(), 4, SPI);
    uint8_t frame[ADS1299_FRAME_BYTE_NUM];
    uint32_t status;
    int32_t ch_raw[ADS1299_CHANNEL_NUM];

    while (1) {
        if (ring_buffer_get_frame (frame)) {
            ADS1299_ParseRawFrame (frame, &status, ch_raw);
            Serial_Printf (SERIAL_PORT_DEBUG, "ch0_v=%d\r\n", ch_raw[0]);
            Serial_Printf (SERIAL_PORT_DEBUG, "ch1_v=%d\r\n", ch_raw[1]);
        }
    }

#elif (SYSTEM_MODE == MODE_SD_TEST)
    Serial_Printf (SERIAL_PORT_DEBUG, "[SD] start self test\r\n");
    {
        int ret = SD_SelfTest();
        Serial_Printf (SERIAL_PORT_DEBUG, "[SD] self test result: %s (ret=%d)\r\n",
                       ret == 0 ? "PASS" : "FAIL", ret);
    }
    while (1) {
    }

#elif (SYSTEM_MODE == ICM_42605_Mode)
    ICM42605_Read();

#elif (SYSTEM_MODE == MODE_GLXSS_BURN)
    {
        int ret = SD_Init();
        Serial_Init (SERIAL_PORT_DEBUG);
        if (ret != 0) {
            Serial_Printf (SERIAL_PORT_DEBUG, "[GLXSS_BURN] SD init FAIL\r\n");
            while (1);
        }
        glxss_sd_fw_burn();
    }
    while (1) {
    }

#elif (SYSTEM_MODE == MODE_MAX98357A_TEST)
    Serial_Init (SERIAL_PORT_DEBUG);
    Serial_Printf (SERIAL_PORT_DEBUG, "[MAX98357A] selftest start\r\n");
    {
        int ret = max98357a_selftest();
        Serial_Printf (SERIAL_PORT_DEBUG, "[MAX98357A] selftest %s (ret=%d)\r\n",
                       ret == 0 ? "PASS" : "FAIL", ret);
    }
    while (1) {
    }

#elif (SYSTEM_MODE == MODE_WAV_TEST)
    Serial_Init (SERIAL_PORT_DEBUG);
    {
        int ret = SD_Init();
        if (ret != 0) {
            Serial_Printf (SERIAL_PORT_DEBUG, "[WAV_TEST] SD init FAIL\r\n");
            while (1);
        }
        Serial_Printf (SERIAL_PORT_DEBUG, "[WAV_TEST] playing test.wav...\r\n");
        wav_player_play ("test.wav");
        while (wav_player_is_playing()) {
            wav_player_poll();
            Delay_Ms (1);
        }
        Serial_Printf (SERIAL_PORT_DEBUG, "[WAV_TEST] playback finished\r\n");
    }
    while (1) {
    }

#elif (SYSTEM_MODE == MODE_I2C_DEBUG)
    Serial_Init (SERIAL_PORT_DEBUG);
    Serial_Printf (SERIAL_PORT_DEBUG, "[I2C_DEBUG] PD12=SDA PD13=SCL, target 0x10\r\n");
    {
        GPIO_InitTypeDef gi;
        RCC_HB2PeriphClockCmd (RCC_HB2Periph_GPIOD, ENABLE);
        gi.GPIO_Speed = GPIO_Speed_High;

        gi.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13;
        gi.GPIO_Mode = GPIO_Mode_Out_OD;
        GPIO_Init (GPIOD, &gi);
        GPIO_SetBits (GPIOD, GPIO_Pin_12 | GPIO_Pin_13);
        Delay_Us (100);

        uint8_t scl_idle = GPIO_ReadInputDataBit (GPIOD, GPIO_Pin_13);
        uint8_t sda_idle = GPIO_ReadInputDataBit (GPIOD, GPIO_Pin_12);
        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] bus idle: SCL=%d SDA=%d (expect 1/1)\r\n",
            (unsigned)scl_idle, (unsigned)sda_idle);


        uint8_t i2c_sda_read (void) { return GPIO_ReadInputDataBit (GPIOD, GPIO_Pin_12); }
        void i2c_sda_out (void) {
            gi.GPIO_Pin = GPIO_Pin_12;
            gi.GPIO_Mode = GPIO_Mode_Out_OD;
            GPIO_Init (GPIOD, &gi);
        }
        void i2c_sda_in (void) {
            gi.GPIO_Pin = GPIO_Pin_12;
            gi.GPIO_Mode = GPIO_Mode_IPU;
            GPIO_Init (GPIOD, &gi);
        }
        void i2c_sda_hi (void) { GPIO_SetBits (GPIOD, GPIO_Pin_12); }
        void i2c_sda_lo (void) { GPIO_ResetBits (GPIOD, GPIO_Pin_12); }
        void i2c_scl_hi (void) {
            GPIO_SetBits (GPIOD, GPIO_Pin_13);
            Delay_Us (5);
        }
        void i2c_scl_lo (void) {
            GPIO_ResetBits (GPIOD, GPIO_Pin_13);
            Delay_Us (5);
        }

        void i2c_start (void) {
            i2c_sda_out();
            i2c_sda_hi();
            i2c_scl_hi();
            i2c_sda_lo();
            i2c_scl_lo();
            i2c_sda_hi();
        }
        void i2c_stop (void) {
            i2c_sda_out();
            i2c_sda_lo();
            i2c_scl_hi();
            i2c_sda_hi();
            Delay_Us (5);
        }

        uint8_t i2c_tx (uint8_t b) {
            i2c_sda_out();
            for (int i = 7; i >= 0; i--) {
                if (b & (1 << i))
                    i2c_sda_hi();
                else
                    i2c_sda_lo();
                i2c_scl_hi();
                i2c_scl_lo();
            }
            i2c_sda_in();
            i2c_scl_hi();
            uint8_t nak = i2c_sda_read();
            i2c_scl_lo();
            i2c_sda_out();
            i2c_sda_hi();
            return nak;
        }

        uint8_t i2c_rx (uint8_t ack) {
            uint8_t b = 0;
            i2c_sda_in();
            for (int i = 7; i >= 0; i--) {
                i2c_scl_hi();
                if (i2c_sda_read())
                    b |= (1 << i);
                i2c_scl_lo();
            }
            i2c_sda_out();
            if (ack)
                i2c_sda_lo();
            else
                i2c_sda_hi();
            i2c_scl_hi();
            i2c_scl_lo();
            i2c_sda_hi();
            return b;
        }

        uint8_t i2c_read_reg (uint8_t reg) {
            i2c_start();
            if (i2c_tx (0x20)) {
                i2c_stop();
                Serial_Printf (SERIAL_PORT_DEBUG, "  [NAK] addr 0x20 (reg 0x%02X)\r\n", (unsigned)reg);
                return 0xFF;
            }
            if (i2c_tx (reg)) {
                i2c_stop();
                Serial_Printf (SERIAL_PORT_DEBUG, "  [NAK] reg 0x%02X\r\n", (unsigned)reg);
                return 0xFF;
            }
            i2c_start();
            if (i2c_tx (0x21)) {
                i2c_stop();
                Serial_Printf (SERIAL_PORT_DEBUG, "  [NAK] addr 0x21 (reg 0x%02X)\r\n", (unsigned)reg);
                return 0xFF;
            }
            uint8_t v = i2c_rx (0);
            i2c_stop();
            return v;
        }

        void i2c_write_reg (uint8_t reg, uint8_t val) {
            i2c_start();
            if (i2c_tx (0x20)) {
                i2c_stop();
                return;
            }
            if (i2c_tx (reg)) {
                i2c_stop();
                return;
            }
            i2c_tx (val);
            i2c_stop();
        }

        /* Toggle VOUT via REG_RAIL_CTRL bit3 (read-modify-write) */
        void i2c_toggle_vout (void) {
            uint8_t rail = i2c_read_reg (0x11);
            uint8_t on = (rail & 0x08) ? 0 : 1;
            i2c_write_reg (0x11, (uint8_t)(rail ^ 0x08));
            Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] VOUT %s (RAIL_CTRL=0x%02X)\r\n",
                           on ? "ON" : "OFF", (unsigned)(rail ^ 0x08));
        }

        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] scanning 0x03..0x77 for ACK:\r\n");
        for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
            i2c_start();
            uint8_t nak = i2c_tx (addr << 1);
            i2c_stop();
            if (!nak)
                Serial_Printf (SERIAL_PORT_DEBUG, "  0x%02X ACK\r\n", (unsigned)addr);
        }
        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] scan done\r\n");
        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] commands: '1'=toggle VOUT, '2'=shutdown, '3'=rails on, '4'=rails off\r\n");

        for (;;) {
            /* Process UART commands from host */
            if (Serial_IsDataReady (SERIAL_PORT_DEBUG)) {
                uint8_t *rx_buf;
                uint16_t rx_len = Serial_GetDataPacket (SERIAL_PORT_DEBUG, &rx_buf);
                for (uint16_t i = 0; i < rx_len; i++) {
                    char c = (char)rx_buf[i];
                    if (c == '1') {
                        i2c_toggle_vout ();
                    } else if (c == '2') {
                        i2c_write_reg (0x20, 0x01);   /* CMD_SHUTDOWN */
                        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] shutdown sent\r\n");
                    } else if (c == '3') {
                        i2c_write_reg (0x20, 0x02);   /* CMD_RAILS_ON */
                        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] rails-on sent\r\n");
                    } else if (c == '4') {
                        i2c_write_reg (0x20, 0x03);   /* CMD_RAILS_OFF */
                        Serial_Printf (SERIAL_PORT_DEBUG, "[I2C] rails-off sent\r\n");
                    }
                }
            }

            Serial_Printf (SERIAL_PORT_DEBUG, "\r\n[I2C] CH32V003 PMIC dump:\r\n");
            uint8_t chip_id = i2c_read_reg (0x00);
            uint8_t fw_ver = i2c_read_reg (0x01);
            uint8_t state = i2c_read_reg (0x02);
            uint8_t key_st = i2c_read_reg (0x03);
            uint8_t key_l = i2c_read_reg (0x04);
            uint8_t key_h = i2c_read_reg (0x05);
            uint8_t rail_st = i2c_read_reg (0x10);
            uint8_t rail_ctl = i2c_read_reg (0x11);
            uint8_t ven_st = i2c_read_reg (0x12);
            Serial_Printf (SERIAL_PORT_DEBUG, "  CHIP_ID=0x%02X FW_VER=0x%02X STATE=0x%02X\r\n",
                           (unsigned)chip_id, (unsigned)fw_ver, (unsigned)state);
            Serial_Printf (SERIAL_PORT_DEBUG, "  KEY_STATE=0x%02X KEY_HOLD=%u\r\n",
                           (unsigned)key_st, (unsigned)((key_h << 8) | key_l));
            Serial_Printf (SERIAL_PORT_DEBUG, "  RAIL_STATUS=0x%02X RAIL_CTRL=0x%02X VEN=0x%02X\r\n",
                           (unsigned)rail_st, (unsigned)rail_ctl, (unsigned)ven_st);

            /* Flash-stored power-up sequence config (regs 0x30..0x4E) */
            uint8_t cfg[31];
            uint8_t cfg_ok = 1;
            for (int i = 0; i < 31; i++) {
                cfg[i] = i2c_read_reg (0x30 + i);
                if (cfg[i] == 0xFF) cfg_ok = 0;
            }
            if (cfg_ok) {
                uint16_t magic = (uint16_t)(cfg[0] | (cfg[1] << 8));
                uint16_t t_ven = (uint16_t)(cfg[4] | (cfg[5] << 8));
                uint16_t t_off = (uint16_t)(cfg[6] | (cfg[7] << 8));
                uint8_t n_steps = cfg[8];
                Serial_Printf (SERIAL_PORT_DEBUG, "  CFG: magic=0x%04X ver=%u flags=0x%02X t_ven=%ums t_off=%ums n_steps=%u\r\n",
                               (unsigned)magic, (unsigned)cfg[2], (unsigned)cfg[3],
                               (unsigned)t_ven, (unsigned)t_off, (unsigned)n_steps);
                for (int i = 0; i < n_steps && i < 6; i++) {
                    uint8_t mask = cfg[12 + 3 * i];
                    uint16_t delay = (uint16_t)(cfg[13 + 3 * i] | (cfg[14 + 3 * i] << 8));
                    Serial_Printf (SERIAL_PORT_DEBUG, "    step%d mask=0x%02X(5V=%u ISO=%u P2=%u P1=%u VOUT=%u) delay=%ums\r\n",
                                   i, (unsigned)mask,
                                   (unsigned)((mask >> 0) & 1), (unsigned)((mask >> 1) & 1),
                                   (unsigned)((mask >> 2) & 1), (unsigned)((mask >> 3) & 1),
                                   (unsigned)((mask >> 4) & 1), (unsigned)delay);
                }
            } else {
                Serial_Printf (SERIAL_PORT_DEBUG, "  CFG: read failed (0xFF)\r\n");
            }

            Delay_Ms (1000);
        }
    }

#elif (SYSTEM_MODE == MODE_GLXSS)
    Serial_Init (SERIAL_PORT_DEBUG);

#ifdef GLXSS_ENABLED
    IPC_Log_Init_V3F();
    Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] IPC log bridge ready\r\n");
    Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] V5F running, waiting for AA55 commands...\r\n");

    Serial_Init (SERIAL_PORT_WIFI);

    s_aa55_body_len = 0;
    s_aa55_state = 0;
    g_app_mode = 0;

    {
        uint32_t last_feat_seq = 0;
        int last_pred = -1;
        for (;;) {
            if (Serial_IsDataReady (SERIAL_PORT_DEBUG)) {
                uint8_t *rx_buf;
                uint16_t rx_len = Serial_GetDataPacket (SERIAL_PORT_DEBUG, &rx_buf);
                for (uint16_t i = 0; i < rx_len; i++) {
                    aa55_feed (rx_buf[i]);
                }
            }
            if (Serial_IsDataReady (SERIAL_PORT_WIFI)) {
                uint8_t *rx_buf;
                uint16_t rx_len = Serial_GetDataPacket (SERIAL_PORT_WIFI, &rx_buf);
                for (uint16_t i = 0; i < rx_len; i++) {
                    aa55_feed (rx_buf[i]);
                }
            }
            IPC_Log_Poll_V3F();
            Delay_Ms (1);

            uint32_t feat_seq = IPC_GetFeatureSeq_V3F();
            if (IPC_GetFeatureValid_V3F() && feat_seq != last_feat_seq) {
                last_feat_seq = feat_seq;
                const volatile int32_t *f = IPC_GetFeatureV3F();
                Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] FEAT#%lu %ld %ld %ld %ld %ld %ld %ld %ld (f0..7), smp=%lu\r\n",
                               (unsigned long)feat_seq,
                               (long)f[0], (long)f[1], (long)f[2], (long)f[3],
                               (long)f[4], (long)f[5], (long)f[6], (long)f[7],
                               (unsigned long)IPC_GetSampleCount_V3F());
            }

            int pred = IPC_GetV5FPred_V3F();
            if (pred != last_pred) {
                last_pred = pred;
                const char *pn[] = {"LEFT", "RIGHT", "UNK"};
                Serial_Printf (SERIAL_PORT_DEBUG, "[V3F] MI: %s conf=%ld sl=%ld sr=%ld\r\n",
                               pn[pred], (long)IPC_GetV5FConfidence_V3F(),
                               (long)IPC_GetV5FScoreLeft_V3F(), (long)IPC_GetV5FScoreRight_V3F());
            }
        }
    }
#else
    while (1) {
    }
#endif
#endif

#elif defined(Core_V5F)
#if defined(V5F_MODE_GLXSS)
    /* GLXSS: V5F runs its own USB Host + display loop in main.c */
#elif defined(GLXSS_ENABLED)
    /* EEG+GLXSS merged: V5F init done in main(), return immediately */
#else
    while (1) {
        /* V5F is intentionally idle outside IPC_CH0 interrupt. */
    }
#endif
#endif
}
