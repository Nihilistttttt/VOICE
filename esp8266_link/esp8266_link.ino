/*
 * VOICE ESP8266 调参链路固件（参照 EEG 项目 CH32_Waveform.ino 架构）
 * ============================================================
 * 角色: TCP client —— 连接热点网关(手机)的 41002 端口, 与配置 App
 *       (手机上运行, TCP server)双向透传 UART↔TCP。
 *
 *         MCU(UART6 帧协议) ⇄ UART0@2M ⇄ ESP8266 ⇄ TCP:41002 ⇄ 配置App
 *
 * 协议在 MCU 与 App 之间(v2 二进制成帧), 本固件只做透传+PING 拦截。
 *
 * 烧录: SW3 拨 CH340↔ESP8266 位 → Arduino IDE(COM8) 上传 → 拨回。
 */

#include <ESP8266WiFi.h>
#include <EEPROM.h>

/* ========== EEPROM WiFi 配置档案(参照 EEG: 5 组 profile) ========== */
/* 调试输出开关: 1=[WIFI] 等状态行经 Serial(TX) 打印;
 * 0=静默。注意: ESP 的 Serial(TX) 与数据线共用(接 MCU UART7 RX),
 * 调试行会混入 MCU 的数据通道(MCU 解析器会忽略文本行, 但会占用
 * 串口带宽)——正式使用且串口接 MCU 时建议置 0。 */
#define ESP_DEBUG 1
#define ESP_PARSE_WAVE 1   /* 1=解析 WAVE 帧提取 s1/s2 用于调试; 0=纯透传 */

#define EEPROM_SIZE 512
#define WIFI_CFG_MAGIC 0xEE01
#define WIFI_CFG_ADDR  0
#define MAX_WIFI_PROFILES 5

typedef struct {
    char ssid[33];
    char password[64];
    uint8_t valid;
    uint8_t is_default;
} WifiProfile;

typedef struct {
    uint16_t magic;
    uint8_t  count;
    uint8_t  active_idx;
    WifiProfile profiles[MAX_WIFI_PROFILES];
} WifiConfig;

static WifiConfig wifiCfg;
static bool wifiCfgDirty = false;

static void loadWifiConfig() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(WIFI_CFG_ADDR, wifiCfg);
    if (wifiCfg.magic != WIFI_CFG_MAGIC || wifiCfg.count > MAX_WIFI_PROFILES) {
        wifiCfg.magic = WIFI_CFG_MAGIC;
        wifiCfg.count = 1;
        wifiCfg.active_idx = 0;
        memset(wifiCfg.profiles, 0, sizeof(wifiCfg.profiles));
        strncpy(wifiCfg.profiles[0].ssid, "testttt", 32);
        strncpy(wifiCfg.profiles[0].password, "12345678", 63);
        wifiCfg.profiles[0].valid = 1;
        wifiCfg.profiles[0].is_default = 1;
        wifiCfgDirty = true;
    }
}

static void saveWifiConfig() {
    if (!wifiCfgDirty) return;
    EEPROM.put(WIFI_CFG_ADDR, wifiCfg);
    EEPROM.commit();
    wifiCfgDirty = false;
}

/* ========== 网络连接: TCP client → 网关:41002 ========== */
const uint16_t SERVER_PORT = 41002;
IPAddress serverIP;
WiFiClient tcpClient;

/* ========== PING 心跳拦截（只拦 "PING\n" 五字节, 其余全透传） ========== */
static uint8_t pingState = 0;
static const uint8_t PING_PATTERN[] = {'P','I','N','G','\n'};

static void checkPingAndForward(uint8_t b) {
    if (b == PING_PATTERN[pingState]) {
        pingState++;
        if (pingState == 5) {
            tcpClient.print("PONG\n");
            pingState = 0;
            return;
        }
    } else {
        if (pingState > 0) {
            for (uint8_t i = 0; i < pingState; i++) {
                Serial.write(PING_PATTERN[i]);
            }
            pingState = 0;
            if (b == PING_PATTERN[0]) {
                pingState = 1;
                return;
            }
        }
        Serial.write(b);
    }
}

/* ========== WAVE 帧状态机解析(逐字节, 跨 readBytes 不丢帧) ========== */
#if ESP_PARSE_WAVE
enum { W_SYNC, W_LEN, W_TYPE, W_S1H, W_S1L, W_S2H, W_S2L, W_CRC };
static uint8_t w_state = W_SYNC;
static uint8_t w_b[4];

static void wave_parse_byte(uint8_t b, int16_t *s1, int16_t *s2) {
    switch (w_state) {
    case W_SYNC: if (b == 0xAA) w_state = W_LEN; break;
    case W_LEN:  w_state = (b == 0x05) ? W_TYPE : (b == 0xAA ? W_LEN : W_SYNC); break;
    case W_TYPE: w_state = (b == 0x03) ? W_S1H : (b == 0xAA ? W_LEN : W_SYNC); break;
    case W_S1H:  w_b[0] = b; w_state = W_S1L; break;
    case W_S1L:  w_b[1] = b; w_state = W_S2H; break;
    case W_S2H:  w_b[2] = b; w_state = W_S2L; break;
    case W_S2L:  w_b[3] = b; w_state = W_CRC; break;
    case W_CRC:
        *s1 = (int16_t)(((uint16_t)w_b[0] << 8) | w_b[1]);
        *s2 = (int16_t)(((uint16_t)w_b[2] << 8) | w_b[3]);
        Serial.printf("WAVE s1=%d s2=%d\n", *s1, *s2);
        w_state = W_SYNC;
        break;
    }
}
#endif

void connectToWiFi() {
    loadWifiConfig();
    saveWifiConfig();

    int idx = wifiCfg.active_idx;
    if (idx >= MAX_WIFI_PROFILES || !wifiCfg.profiles[idx].valid) idx = 0;

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiCfg.profiles[idx].ssid, wifiCfg.profiles[idx].password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        serverIP = WiFi.gatewayIP();      /* 服务端(配置App)恒在网关=手机 */
#if ESP_DEBUG
        Serial.printf("\r\n[WIFI] UP ip=%s gw=%s\r\n",
                      WiFi.localIP().toString().c_str(),
                      serverIP.toString().c_str());
#endif
    } else {
#if ESP_DEBUG
        Serial.printf("\r\n[WIFI] DOWN\r\n");
#endif
    }
}

bool connectToServer() {
    if (!tcpClient.connected()) {
        tcpClient.setNoDelay(true);
        if (tcpClient.connect(serverIP, SERVER_PORT)) return true;
        delay(2000);
        return false;
    }
    return true;
}

void setup()
{
    Serial.begin(1000000);            /* UART0 ↔ MCU UART6 */
    Serial.setRxBufferSize(4096);
    Serial.setTimeout(50);            /* readBytes 超时 50ms, 避免长时间阻塞 loop */
    connectToWiFi();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        connectToWiFi();
        Serial.printf("\r\n[WIFI] WAITING...\r\n");
        return;
    }

    if (!connectToServer()) return;

    /* 1. TCP 下行: 透传到 MCU 串口（只拦截 PING 心跳）
     * ★ 原 while 循环在 voice_config 发大量下行时会占满 CPU, 导致
     *   yield() 不执行 → WiFi 协议栈不处理 → tcpClient.write() 死锁。
     *   改 if: 每次 loop() 只处理一批, 确保 yield() 定期调用 */
    if (tcpClient.available()) {
        uint8_t buf[128];
        size_t len = tcpClient.available();
        if (len > sizeof(buf)) len = sizeof(buf);
        len = tcpClient.readBytes(buf, len);
        for (size_t i = 0; i < len; i++) {
            checkPingAndForward(buf[i]);
        }
    }

    /* 2. 串口→TCP 上行透传（MCU 的 v2 帧原样转发）
     * ★ while 一次消化所有 Serial 积压, 避免延迟和重复值
     *   限制最多 4 轮(4*256=1024B) 避免饿死 yield() */
    static uint32_t dbg_last = 0;
    static uint32_t rx_cnt = 0, tx_cnt = 0, drop_cnt = 0;
    static int16_t last_s1 = 0, last_s2 = 0;
    int read_rounds = 0;
    while (Serial.available() && read_rounds < 4) {
        read_rounds++;
        int avail = tcpClient.availableForWrite();
        if (avail > 0) {
            size_t len = Serial.available();
            if (len > 256) len = 256;
            if ((int)len > avail) len = avail;
            if (len > 0) {
                uint8_t buf[256];
                len = Serial.readBytes(buf, len);
                rx_cnt += len;
#if ESP_PARSE_WAVE
                for (size_t i = 0; i < len; i++) {
                    wave_parse_byte(buf[i], &last_s1, &last_s2);
                }
#endif
                size_t w = tcpClient.write(buf, len);
                tx_cnt += w;
            }
        } else {
            drop_cnt++;
            break;
        }
    }

    if (millis() - dbg_last >= 1000) {
        dbg_last = millis();
        int avail = tcpClient.availableForWrite();
        Serial.printf("STAT rx=%lu tx=%lu drop=%lu av=%d\n",
            (unsigned long)rx_cnt, (unsigned long)tx_cnt,
            (unsigned long)drop_cnt, avail);
    }

    yield();

    if (!tcpClient.connected()) delay(100);
}
