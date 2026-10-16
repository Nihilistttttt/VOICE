/*
 * VOICE ESP8266 调参链路固件（纯管道桥）
 * ============================================================
 * 角色: ESP8266MOD 板载于 V3 主板, UART0(GPIO1/3) 直连 MCU UART6
 *       (PC0/PC1, 2M)。本固件把 WiFi UDP 与 UART 做透明双向桥:
 *
 *         手机(UDP:8888) ⇄ WiFi ⇄ ESP8266 ⇄ UART0@2M ⇄ MCU(UART6)
 *
 * 协议在 MCU 与手机之间(0xFB 参数/0xF9 存盘/0xFE 波形), 本固件不解析。
 *
 * 烧录本固件(硬件一键下载):
 *   1. 主板 SW3 拨到 CH340↔ESP8266 位 (CTRL0=GND)
 *   2. PC 打开 Arduino IDE(需 esp8266 core), 开发板选
 *      "Generic ESP8266 Module", 端口选本板 CH340
 *   3. 直接点上传 —— RTS/DTR 经 Q5/Q7 自动拉 GPIO0+复位进 boot
 *   4. 烧完把 SW3 拨回 UART6 位(CTRL0=3V3)
 *
 * 运行: 上电自动连 WiFi(testttt), 8000ms 内未连上自动重试;
 *       UART 收到字节 → UDP 发往最近对端(无对端则全网广播);
 *       UDP 收到字节 → UART 发往 MCU。状态经 [WIFI] 文本回告 MCU。
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

static const char* WIFI_SSID = "testttt";
static const char* WIFI_PASS = "12345678";
static const unsigned int UDP_PORT = 8888;

static WiFiUDP Udp;
static IPAddress peer;        /* 最近对端(手机) */
static bool peerKnown = false;
static uint8_t txbuf[256];    /* UART→UDP 攒批缓冲 */
static int txlen = 0;
static unsigned long txBatchMs = 0;

static void wifiStatusToUart()
{
  bool up = (WiFi.status() == WL_CONNECTED);
  static bool last = false;
  static unsigned long lastReport = 0;
  if (up != last || millis() - lastReport > 15000) {
    last = up;
    lastReport = millis();
    /* ASCII 状态行: MCU 字节流解析器会忽略非 0xFB/0xF9 字节 */
    Serial.printf("\r\n[WIFI] %s ip=%s rssi=%d\r\n",
                  up ? "UP" : "DOWN",
                  up ? WiFi.localIP().toString().c_str() : "-",
                  up ? WiFi.RSSI() : 0);
  }
}

static void udpSend(const uint8_t* data, int len)
{
  Udp.beginPacket(peerKnown ? peer : IPAddress(255, 255, 255, 255), UDP_PORT);
  Udp.write(data, len);
  Udp.endPacket();
}

void setup()
{
  Serial.begin(2000000);        /* UART0 ↔ MCU UART6, 与调试口统一 2M */
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setAutoReconnect(true);
  Udp.begin(UDP_PORT);
  Serial.printf("\r\n[WIFI] boot ssid=%s port=%u\r\n", WIFI_SSID, UDP_PORT);
}

void loop()
{
  wifiStatusToUart();

  /* UART → 攒批 → UDP (20ms 或 240B 触发) */
  int avail = Serial.available();
  while (avail > 0 && txlen < (int)sizeof(txbuf)) {
    txbuf[txlen++] = (uint8_t)Serial.read();
    avail--;
    if (txlen >= 240) break;
  }
  unsigned long now = millis();
  if (txlen > 0 && (now - txBatchMs >= 20 || txlen >= 240)) {
    udpSend(txbuf, txlen);
    txlen = 0;
    txBatchMs = now;
  } else if (txlen > 0) {
    /* 等待攒批窗口 */
  }

  /* UDP → UART */
  int n = Udp.parsePacket();
  if (n > 0) {
    peer = Udp.remoteIP();
    peerKnown = true;
    uint8_t b[512];
    int l = Udp.read(b, sizeof(b));
    if (l > 0) Serial.write(b, l);
  }

  delay(1);
}
