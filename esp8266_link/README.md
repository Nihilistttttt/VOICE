# ESP8266 调参链路（esp8266_link）

Arduino sketch（独立文件夹，符合"sketch 名=文件夹名"规则）——把 WiFi UDP 与
UART0 做透明双向桥，MCU 侧协议（0xFB/0xF9/0xFE）与手机 App 之间的传输管道。

```
手机(UDP:8888) ⇄ WiFi(testttt) ⇄ ESP8266MOD(本固件) ⇄ UART0@2M ⇄ MCU(UART6 PC0/PC1)
```

## 构建

1. Arduino IDE → 开发板管理器 → 安装 **esp8266** 核心（by ESP8266 Community）
2. 开发板选 **Generic ESP8266 Module**，Flash Size 按模组选 1MB（ESP8266MOD）
3. 打开 `esp8266_link.ino` → 上传（见下）

## 烧录（硬件一键下载电路，零飞线）

| 步骤 | 操作 |
|---|---|
| 1 | 主板 **SW3 拨到 CH340↔ESP8266 位**（CTRL0 接 GND 侧） |
| 2 | Arduino IDE 选端口（本板 CH340），点上传 |
| 3 | RTS/DTR 经 Q7/Q5 自动拉 GPIO0+复位进 boot（无需手动按键） |
| 4 | 烧完 **SW3 拨回 UART6 位**（CTRL0 接 3V3 侧） |

备用：SW5 = ESP8266 手动复位按键；Q7 基极($1N2180)可外接 GPIO0 强制拉低。

## 运行

- 上电自动连 WiFi（`testttt` / `12345678`），断线自动重连
- 手机发 UDP 到 **ESP 的 IP:8888**（IP 经 UART `[WIFI] UP ip=x.x.x.x` 告知 MCU，
  也会从 com9_log.txt 里看到）
- MCU 侧波形/参数协议：见 `docs/烧录与串口调试规范.md` 第 5 节
