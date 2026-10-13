# VOICE 项目技术手册

> 为高位截瘫患者开发的吹气语音控制系统  
> MCU: CH32V307 | 双INMP441麦克风 | USB AOA协议 | Android无障碍服务  
> 版本: 2026-09-15

---

## 目录

1. [系统概述](#1-系统概述)
2. [硬件架构](#2-硬件架构)
3. [MCU固件](#3-mcu固件)
4. [Android App](#4-android-app)
5. [通信协议](#5-通信协议)
6. [构建与烧录](#6-构建与烧录)
7. [USBHS技术详解](#7-usbhs技术详解)


---

## 1. 系统概述

### 1.1 设计目标

高位截瘫患者无法用手操作手机，但可以吹气。本系统通过双麦克风检测吹气模式（短吹/中吹/长吹/组合），将吹气命令经USB AOA协议发送到Android手机，通过无障碍服务控制B站、微信等App的媒体播放和界面导航。

### 1.2 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    MCU (CH32V307)                        │
│                                                         │
│  INMP441 A ──I2S2──→ DMA1_Ch4 ──→ 能量计算(中断)        │
│  INMP441 B ──I2S3──→ DMA2_Ch+1 ──→ 能量计算(中断)        │
│                                                         │
│  能量值 ──→ 吹气检测状态机 ──→ HUM_CMD_xxx              │
│  HUM_CMD ──→ 命令映射 ──→ AOA_Host_Send(bulk OUT)       │
│  AOA_Host_Recv(bulk IN) ──→ Android指令解析             │
│                                                         │
│  OLED(SPI1) ← 状态/能量值显示                           │
└────────────────────┬────────────────────────────────────┘
                     │ USB AOA
┌────────────────────┴────────────────────────────────────┐
│                  Android Phone                          │
│                                                         │
│  UsbAccessoryManager ← bulk IN 命令                     │
│       │                                                 │
│       ▼                                                 │
│  MainActivity.onCommand()                               │
│       │                                                 │
│       ├── STATE_SELECT: 选择App                          │
│       └── STATE_CONTROL:                                │
│             ├── AppAdapter.handleCommand() (B站/微信)    │
│             └── handleDefaultCommand() (媒体键)          │
│                   │                                     │
│                   ▼                                     │
│            VoiceAccessibilityService                    │
│              ├── 焦点导航 (collectClickable/scrollToVisible) │
│              ├── 手势模拟 (doSwipe/doTap)               │
│              └── 跨App适配 (B站/微信/喜马拉雅)           │
└─────────────────────────────────────────────────────────┘
```

### 1.3 数据流

| 方向 | 路径 | 内容 |
|------|------|------|
| 麦克风→MCU | I2S+DMA | 24bit PCM音频数据 |
| MCU内部 | 中断→状态机 | 能量值→吹气判定→命令 |
| MCU→手机 | USB bulk OUT | 单字节命令 / 5字节能量包 / 14字节采集结果 |
| 手机→MCU | USB bulk IN | 0xFD/0xFC/0xFB/0xFA 控制指令 |

---

## 2. 硬件架构

### 2.1 MCU: CH32V307

- **内核**: RISC-V 32bit, 144MHz
- **USB**: USBHS (USB 2.0 High-Speed Host), 用于AOA连接手机
- **SPI**: SPI1 用于OLED, SPI2/SPI3 用于I2S麦克风
- **DMA**: DMA1_Channel4 (麦克风A), DMA2_Channel1 (麦克风B)

### 2.2 双INMP441麦克风

INMP441是24bit I2S数字MEMS麦克风。数据格式：24bit ADC打包成32bit I2S帧，DMA按HalfWord(16bit)接收，每帧4个HalfWord。

| 麦克风 | I2S外设 | WS | CK | SD | DMA | 采样率 |
|--------|---------|-----|-----|-----|------|--------|
| A | I2S2 | PB12 | PB13 | PB15 | DMA1_Ch4 | 16kHz |
| B | I2S3 | PA15 | PB3 | PB5 | DMA2_Ch1 | 16kHz |

> I2S3需禁用JTAG保留SWD（释放PA15/PB3）：`AFIO->PCFR1 = SWJ_CFG_JTAGDISABLE`

### 2.3 OLED: SSD1306 128x64

| 信号 | 引脚 | 说明 |
|------|------|------|
| CS | PB0 | 片选(GPIO) |
| RES | PC4 | 复位(GPIO) |
| DC | PC5 | 数据/命令(GPIO) |
| SCK | PA5 | SPI1时钟(AF_PP) |
| MOSI | PA7 | SPI1数据(AF_PP) |

SPI1配置: Master, 1Line_Tx, CPOL=High, CPHA=2Edge, Prescaler=16(9MHz), MSB First

### 2.4 USB连接

MCU做USB Host，手机做USB Device。通过USB AOA (Android Open Accessory)协议通信。物理连接: MCU USBHS端口 ↔ 手机USB数据线。

---

## 3. MCU固件

### 3.1 项目结构

```
VOICE_V307/
├── Common/              # 板级配置+GPIO抽象层
│   ├── Config.h         # 引脚/外设宏定义
│   ├── Config.c         # 时钟使能辅助函数
│   ├── hal_gpio.h       # GPIO抽象接口
│   └── hal_gpio.c       # GPIO抽象实现
├── Phone/               # 业务层
│   ├── mic_hum.h/c      # 麦克风采集+吹气检测
│   ├── aoa_host.h/c     # USB AOA Host协议
│   ├── phone_control.h/c # 命令分发+状态机+OLED显示
│   ├── OLED.h/c         # OLED驱动(硬件SPI1)
│   └── OLED_Font.h      # 8x16字库
├── USB_Host/            # USB Host驱动(WCH官方库)
│   ├── ch32v30x_usbhs_host.h/c  # USBHS Host驱动
│   ├── usb_host_hub.h/c         # USB Hub
│   └── usb_host_hid.h/c         # USB HID
├── User/                # 主程序
│   └── main.c           # 入口
├── Peripheral/          # V307外设库
├── Core/                # RISC-V内核
└── obj/                 # 构建输出
    ├── Makefile
    ├── sources.mk
    └── Phone/subdir.mk  # Phone目录编译规则
```

### 3.2 GPIO抽象层 (hal_gpio)

提供引脚编码和统一操作接口，适配V307与H417差异。

**引脚编码**: `HAL_GPIO_PIN(port, pin)` → `(port << 16) | pin`，port: 0=A, 1=B, 2=C...

**核心API**:
```c
void Hal_GPIO_Init(uint32_t pin_enc, Hal_GPIO_Mode_t mode,
                   Hal_GPIO_Speed_t speed, uint8_t af_num);
void Hal_GPIO_Write(uint32_t pin_enc, uint8_t level);
void Hal_GPIO_Set(uint32_t pin_enc);
void Hal_GPIO_Reset(uint32_t pin_enc);
void Hal_GPIO_Toggle(uint32_t pin_enc);
uint8_t Hal_GPIO_Read(uint32_t pin_enc);
```

**重要**: `Hal_GPIO_Init` **不自动使能GPIO端口时钟**，调用者必须先手动调`GPIO_ClockEnable(HAL_GPIO_PORT(pin))`。配置AF_PP复用模式时还须先调`AFIO_ClockEnable()`。

**V307特性**: 无GPIOF(最多GPIOE)、无GPIO_PinAFConfig(AF由Mode直接设置)、GPIO速度10/2/50MHz、EXTI分组: 0~4独立+9_5+15_10。

### 3.3 板级配置 (Config.h)

集中管理所有引脚和外设定义：

```c
// I2S2 麦克风A
#define I2S2_WS_PIN_ENC    HAL_GPIO_PIN(1, 12)   // PB12
#define I2S2_CK_PIN_ENC    HAL_GPIO_PIN(1, 13)   // PB13
#define I2S2_SD_PIN_ENC    HAL_GPIO_PIN(1, 15)   // PB15
#define I2S2_DMA_CHANNEL   DMA1_Channel4

// I2S3 麦克风B
#define I2S3_WS_PIN_ENC    HAL_GPIO_PIN(0, 15)   // PA15
#define I2S3_CK_PIN_ENC    HAL_GPIO_PIN(1, 3)    // PB3
#define I2S3_SD_PIN_ENC    HAL_GPIO_PIN(1, 5)    // PB5
#define I2S3_DMA_CHANNEL   DMA2_Channel1

// OLED SPI1
#define OLED_CS_PIN_ENC    HAL_GPIO_PIN(1, 0)    // PB0
#define OLED_RES_PIN_ENC   HAL_GPIO_PIN(2, 4)   // PC4
#define OLED_DC_PIN_ENC    HAL_GPIO_PIN(2, 5)   // PC5
#define OLED_SCK_PIN_ENC   HAL_GPIO_PIN(0, 5)   // PA5
#define OLED_MOSI_PIN_ENC  HAL_GPIO_PIN(0, 7)   // PA7
#define OLED_SPI_INSTANCE  SPI1
```

### 3.4 I2S协议详解

> **一句话记住：I2S是专门为数字音频设计的同步串行总线，用3根线（SCK+WS+SD）传输左右声道交替的PCM数据，是ADC/DAC芯片和MCU之间传音频的标准接口。**

#### 3.4.1 为什么需要I2S

UART/I2C/SPI是通用总线，传音频有各自的问题：

| 总线 | 传音频的问题 |
|------|-------------|
| UART | 异步无时钟线，双方波特率有偏差会累积，不适合连续高速音频流 |
| I2C | 7位地址寻址+ACK应答，每字节都有协议开销，带宽太低 |
| SPI | 可以传，但没有左右声道区分机制，也没有音频专用的帧对齐 |

I2S（Inter-IC Sound）是飞利浦（Philips）1986年专为**芯片间数字音频传输**设计的总线，解决三个核心需求：
1. **同步时钟** — 发送方生成时钟，接收方跟随，无波特率偏差
2. **声道区分** — WS信号交替标记左/右声道
3. **连续流** — 无寻址/应答开销，适合不间断的音频数据流

#### 3.4.2 I2S信号线

| 信号 | 全称 | 方向 | 说明 |
|------|------|------|------|
| SCK | Serial Clock | 主→从 | 位时钟，每个bit一个边沿 |
| WS | Word Select (LRCLK) | 主→从 | 声道选择，低=左声道，高=右声道 |
| SD | Serial Data | 双向 | 串行PCM数据，MSB first |
| MCLK | Master Clock | 主→从 | 主时钟（可选），通常是采样率×256 |

WS也叫LRCLK（Left/Right Clock），频率等于采样率（如16kHz）。SCK频率 = 采样率 × 位深度 × 声道数，如16kHz×32bit×2ch = 1.024MHz。

```
         ┌──左声道──┐┌──右声道──┐
WS:  ────┘          └┘          └...
SCK: ──┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─...
SD:   D15 D14 ... D0 D15 D14 ... D0
```

#### 3.4.3 Philips标准时序

Philips标准的关键：**WS在数据MSB前一个SCK周期翻转**。

```
SCK   ─┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌┐ ┌
       └─┘└─┘└─┘└─┘└─┘└─┘└─┘└─┘└─┘└─┘└─┘└─
WS  ─────────────┐                       ┌────
                  └───────────────────────┘
SD    X  D15 D14 D13 ... D1 D0  X  D15' D14' ...
      ↑                           ↑
    MSB(左)                     MSB(右)
```

WS下降沿提前一个SCK周期，告诉接收方"下一个MSB是左声道"。数据在SCK上升沿（或下降沿，取决于配置）采样。

#### 3.4.4 数据格式

I2S数据位宽可配置为16/24/32位，但**WS周期（声道槽位）**通常固定为32位（最常见）或16位：

| 格式 | WS周期 | 数据位 | 填充 |
|------|--------|--------|------|
| 16bit | 16bit | 16bit | 无 |
| 24bit in 32bit | 32bit | 24bit | 低8位补零 |
| 32bit | 32bit | 32bit | 无 |

**本项目使用32位格式**：INMP441输出24位PCM，打包在32位声道槽位中，高24位是有效数据，低8位为零。

```
32位声道槽位 (WS低=左声道):
┌─────────────────────────────────┐
│ D31 D30 ... D8 │ D7 ... D0 = 0 │
│  ← 24bit PCM → │  ← 8bit 零 → │
└─────────────────────────────────┘
```

#### 3.4.5 I2S与SPI的关系

很多MCU（包括CH32V307）的I2S不是独立外设，而是**SPI外设的复用模式**。同一个SPI控制器，配为SPI模式传普通数据，配为I2S模式传音频。

| 方面 | SPI模式 | I2S模式 |
|------|---------|---------|
| 时钟 | 连续SCLK | 仅数据期间有SCK |
| 帧同步 | CS片选 | WS声道选择 |
| 数据 | 8/16bit任意 | 16/24/32bit音频 |
| 声道 | 无 | 左右交替 |
| 寄存器 | SPI->DR | 同一个DATAR |

CH32V307的I2S2复用SPI2，I2S3复用SPI3。初始化时调`I2S_Init(SPI2, ...)`，底层操作的是SPI2的寄存器，但工作在I2S时序模式。

#### 3.4.6 CH32V307的I2S配置

```c
// mic_hum.c:494-500
I2S_InitStructure.I2S_Mode       = I2S_Mode_MasterRx;    // 主机接收
I2S_InitStructure.I2S_Standard   = I2S_Standard_Phillips; // Philips标准
I2S_InitStructure.I2S_DataFormat = I2S_DataFormat_32b;    // 32位数据
I2S_InitStructure.I2S_MCLKOutput = I2S_MCLKOutput_Disable; // 不输出MCLK
I2S_InitStructure.I2S_AudioFreq  = I2S_AudioFreq_16k;     // 16kHz采样率
I2S_InitStructure.I2S_CPOL       = I2S_CPOL_Low;          // 空闲低电平
```

| 参数 | 值 | 说明 |
|------|-----|------|
| Mode | MasterRx | MCU做主机，生成SCK/WS，接收SD数据 |
| Standard | Philips | WS提前一个SCK翻转 |
| DataFormat | 32b | 每声道32位槽位 |
| AudioFreq | 16k | 采样率16kHz，WS=16kHz |
| MCLK | Disable | INMP441不需要MCLK |
| CPOL | Low | SCK空闲低电平 |

引脚分配：

| 麦克风 | I2S | WS | CK | SD | 说明 |
|--------|-----|-----|-----|-----|------|
| A | I2S2 | PB12 | PB13 | PB15 | V307官方I2S2引脚 |
| B | I2S3 | PA15 | PB3 | PB5 | V307官方I2S3引脚 |

> I2S3的PA15/PB3默认是JTAG引脚，需`AFIO->PCFR1 = SWJ_CFG_JTAGDISABLE`释放（保留SWD调试）。

#### 3.4.7 DMA与I2S——为什么按HalfWord(16位)传输

CH32V307的SPI/I2S数据寄存器`DATAR`物理上是**16位**宽度。DMA的`PeripheralDataSize`必须匹配外设寄存器宽度。

```
I2S配32位格式 → 每声道32bit数据
DATAR是16位  → DMA每次只能读16位
→ 每声道需要2次DMA传输
→ 立体声帧(L+R) = 4次DMA传输 = 4个HalfWord = 64bit
```

如果DMA配Word(32位)：对16位寄存器做32位读 → 高16位是垃圾/重复 → 数据错乱。

缓冲区结构：

```c
// mic_hum.c:14-19
#define FRAME_CNT    128          // 每次处理128帧
#define HW_PER_FRAME 4            // 每帧4个HalfWord
#define HW_CNT       (128 * 4)    // = 512

static uint16_t dma_buf_a[HW_CNT * 2];  // 1024个uint16_t，双缓冲
```

```
dma_buf_a 在内存中的布局:
┌─帧0─┬─帧1─┬─帧2─┬ ... ┬─帧255─┐
│HW0123│HW0123│HW0123│      │HW0123 │  每帧4个uint16_t
└──────┴──────┴──────┴      ┴───────┘
```

每个帧的4个HalfWord：

| HW | 内容（取决于WS/CK相位对齐） |
|----|------------------------------|
| [0] | 可能是左声道高16位 / 零填充 / 右声道 |
| [1] | 可能是左声道低16位 / 零填充 |
| [2] | 可能是右声道高16位 / 零填充 / 左声道 |
| [3] | 可能是右声道低16位 / 零填充 |

INMP441只在一个声道输出24位PCM，另一个声道是静音。有效数据在4个HW中的**哪一个**，由WS/CK相位对齐决定，上电时不固定。

#### 3.4.8 Auto_Detect_Pos——有效数据位置自动检测

三个因素叠加导致有效数据位置无法硬编码：
1. INMP441单声道输出（另一声道静音）
2. 32位帧拆成4个HalfWord
3. WS/CK相位对齐上电不固定

算法（`mic_hum.c:352-379`）：

```c
static int Auto_Detect_Pos_A(void) {
    int32_t min_amp[4] = {100000, 100000, 100000, 100000};
    // 1. 采集10个DMA周期
    for (int g = 0; g < 10; g++) {
        waitDMAComplete();
        // 2. 对每个候选位置p=0,1,2,3算128帧峰峰值
        for (int p = 0; p < 4; p++) {
            int32_t mn = 32767, mx = -32768;
            for (int i = 0; i < 128; i++) {
                int16_t s = dma_buf_a[i * 4 + p];
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
            int32_t a = mx - mn;           // 峰峰值
            if (a < min_amp[p]) min_amp[p] = a;  // 10次取最小
        }
    }
    // 3. 选幅值最小且>5的位置
    for (int p = 0; p < 4; p++) {
        if (min_amp[p] < best_amp && min_amp[p] > 5)
            valid_pos = p;
    }
}
```

判据解释：

| 条件 | 原因 |
|------|------|
| 10个周期取最小 | 跳过启动瞬态，找最安静稳定的状态 |
| 峰峰值最小 | 安静时真实音频是环境噪声(幅值小但>0)；选最小的有效幅值 |
| 峰峰值>5 | 排除零填充位(恒定0，幅值=0)；5是"真实音频"与"零填充"的门槛 |

4个位置的典型幅值：

| 位置 | 典型幅值 | 判定 |
|------|----------|------|
| 有效音频HW(高16位) | 10~100(环境噪声) | ✅ 选这个 |
| 零填充HW | 0(恒定零) | ❌ 幅值=0，被>5排除 |
| 静音声道HW | 0(恒定零) | ❌ 幅值=0，被>5排除 |
| 低有效位HW(低8位) | 0~5(太小) | ❌ 幅值≤5，排除 |

> **一句话向老师解释**："I2S 32位帧经DMA按16位拆成4个HalfWord，有效音频只占1个，位置由WS/CK相位决定且不固定。安静环境下有效HW有环境噪声(幅值小但>0)，零填充位幅值=0。对4个位置算峰峰值取最小且>5的——最小选最安静的真实信号，>5排除零填充。"

#### 3.4.9 中断驱动+双缓冲采集

DMA配为Circular模式，BufferSize=HW_CNT*2=1024，产生两个中断：

```
dma_buf_a[0]  ........................  [511] | [512] ........................  [1023]
├── 前半段 (HT中断处理) ──────────────┤├── 后半段 (TC中断处理) ─────────────┤
                                       ↑
                               DMA自动回绕到[0]继续写
```

- **HT(Half Transfer)中断** → 处理前半段[0~511]，DMA正在写后半段
- **TC(Transfer Complete)中断** → 处理后半段[512~1023]，DMA正在写前半段

CPU和DMA交替操作不同区域，无需等待，**零拷贝**。

能量计算（中断内，每8ms/tick）：

```c
// 从valid_pos位置取有效HalfWord
int16_t *p = (int16_t *)dma_buf_a + offset + s_valid_pos_a;
for (int i = 0; i < 128; i++) {
    int16_t s = p[i * 4];  // 每帧跳4个HW，取valid_pos那个
    if (s < mn) mn = s;
    if (s > mx) mx = s;
    acc += abs(s);
}
amp = mx - mn;       // 峰峰值(全零检测用)
mean = acc / 128;    // 绝对值平均能量(吹气判定用)
```

连续12个tick `amp_a==0 && amp_b==0` → I2S时序漂移 → 重新初始化I2S+DMA+AutoDetect。

#### 3.4.10 I2S vs其他音频接口

| 接口 | 线数 | 时钟 | 典型用途 | 本项目 |
|------|------|------|----------|--------|
| I2S | 3(SCK+WS+SD) | 同步 | 芯片间音频 | ✅ INMP441 |
| PCM | 3(SCK+SYNC+DIN) | 同步 | 语音通话 | ✗ |
| TDM | 3(SCK+FS+DIN) | 同步 | 多声道(8/16ch) | ✗ |
| PDM | 2(CLK+DATA) | 同步 | 数字MEMS麦克风 | ✗ |
| USB Audio | 2(D+/D-) | 从数据恢复 | USB声卡 | ✗ |

I2S是最常见的芯片间数字音频接口，INMP441就是I2S输出，与MCU的I2S外设直接对接，无需编解码。

### 3.5 麦克风采集 (mic_hum.c)

#### 3.5.1 初始化流程

```
Mic_Hum_Init()
  ├── GPIO_ClockEnable (GPIOA/B/C)
  ├── AFIO_ClockEnable + JTAG禁用 (释放PA15/PB3)
  ├── SPI_ClockEnable (SPI2/SPI3)
  ├── Hal_GPIO_Init (WS/CK=AF_PP, SD=AF_INPUT)
  ├── I2S_Init (MasterRx, Philips, 32bit, 16kHz)
  ├── DMA双缓冲 (BufferSize=HW_CNT*2, Circular, VeryHigh)
  ├── Auto_Detect_Pos_A/B (检测有效数据位置)
  ├── NVIC使能 (DMA1_Ch4_IRQn, DMA2_Ch1_IRQn)
  └── DMA_Cmd ENABLE
```

#### 3.5.2 INMP441数据格式

32bit I2S帧占4个HalfWord(HW)。有效PCM数据在4个HW中的位置不固定（取决于I2S时序对齐）。`Auto_Detect_Pos`在初始化时采集10个DMA周期，对4个候选位置各算128帧峰峰值，取幅值最小且>5的位置作为`valid_pos`。

#### 3.5.3 中断驱动+双缓冲

```
DMA缓冲区: dma_buf_a[HW_CNT*2]  (1024 HW = 256帧 × 2半区)
           dma_buf_b[HW_CNT*2]

DMA1_Channel4_IRQHandler (麦克风A):
  HT中断 → calc_energy_a(0)      // 处理前半段(0~511)
  TC中断 → calc_energy_a(HW_CNT) // 处理后半段(512~1023)

DMA2_Channel1_IRQHandler (麦克风B):
  HT中断 → calc_energy_b(0)
  TC中断 → calc_energy_b(HW_CNT)
```

**能量计算** (中断内, 每8ms/tick):
```
for (i=0; i<128; i++) {
    s = dma_buf[offset + valid_pos + i*4];  // 取有效HW
    if (s < mn) mn = s;
    if (s > mx) mx = s;
    acc += abs(s);
}
amp = mx - mn;        // 峰峰值(用于全零检测)
mean = acc / 128;     // 绝对值平均能量(用于吹气判定)
s_irq_mean = mean;
s_irq_amp = amp;
s_ready = 1;
```

#### 3.5.4 全零重启

连续12个tick `amp_a==0 && amp_b==0` → I2S_Restart()重新初始化I2S+DMA+AutoDetect，防止I2S时序漂移导致数据全零。

### 3.6 吹气检测算法

#### 3.6.1 可调参数

| 参数 | 默认值 | 含义 |
|------|--------|------|
| `g_blow_threshold` | 2500 | 吹气触发能量阈值 |
| `g_short_max_tick` | 40 | 短吹上限(≤40 tick ≈ 320ms) |
| `g_mid_max_tick` | 80 | 中吹上限(41~80 tick ≈ 640ms) |
| `g_combo_wait_tick` | 80 | 组合等待上限(640ms) |
| `g_long_repeat_tick` | 10 | 长吹重复触发间隔(80ms) |
| `g_min_gap_tick` | 6 | 判定吹气结束的最小间隙(48ms) |

#### 3.6.2 状态机

```
mic_process(m, blowing, energy, cmd_short, cmd_long, cmd_ss, cmd_sss)
```

| 状态 | 行为 |
|------|------|
| **MIC_IDLE** | 检测到blowing(energy≥threshold) → 进入MIC_BLOWING |
| **MIC_BLOWING** | 持续吹: ticks++, 更新peak/sum。ticks>mid_max且距上次长触发≥long_repeat → 立即返回cmd_long(长吹即时触发)。停止吹 → MIC_BLOW_GAP |
| **MIC_BLOW_GAP** | gap≥min_gap时判定结束: ticks≤short_max → 短吹, 进入MIC_SHORT_WAIT/MIC_SHORT_WAIT2; 否则中/长吹返回cmd_long |
| **MIC_SHORT_WAIT** | 等第2段短吹: min_gap内再吹 → 合并回MIC_BLOWING; 超过combo_wait → 返回cmd_short(单短吹) |
| **MIC_SHORT_WAIT2** | 等第3段短吹: 超时 → 返回cmd_ss(双短吹); 第3段来 → 返回cmd_sss(短短短) |

#### 3.6.3 双麦协同

- **冲突判决**: 两麦同时吹，只取能量高者
- **跨麦组合**: A在MIC_SHORT_WAIT同时B也在 → `HUM_CMD_LEFT`; 反之 → `HUM_CMD_RIGHT`
- **音量模式**: 收到VOL_MODE → 切换g_volume_mode, 长吹含义在NEXT/PREV与VOL_UP/VOL_DN间切换

#### 3.6.4 命令映射

| 麦克风 | 短吹 | 中/长吹 | 双短 | 短短短 |
|--------|------|---------|------|--------|
| A | ENTER(2) | NEXT(5) / VOL_UP(7) | PLAY(4) | VOL_MODE(13) |
| B | BACK(3) | PREV(6) / VOL_DN(8) | MODE(10) | VOL_MODE(13) |
| 跨麦 | A+B=LEFT(11) | B+A=RIGHT(12) | - | - |

### 3.7 USB AOA Host (aoa_host.c)

#### 3.7.1 AOA协议概述

Android Open Accessory协议让MCU(无USB Device能力)能作为Host控制手机。流程: MCU检测手机 → 发送AOA识别字符串 → START_ACCESSORY → 手机切换到AOA模式(VID=0x2D00/0x2D01) → bulk传输。

#### 3.7.2 连接流程

```
AOA_Host_Connect()
  │
  ├── Phase 0: 强制USB Host重置
  │   USBHS_Host_Init(DISABLE) → 500ms → USBHS_Host_Init(ENABLE)
  │   (清除Android端AOA框架残留状态)
  │
  ├── 主循环(无超时):
  │   ├── 等待设备插入
  │   ├── AOA_Enumerate → 判断是否已是AOA设备
  │   │   ├── 是 → 等3s让App打开 → AOA_STATE_READY ✓
  │   │   └── 否 → AOA_CheckSupport (GET_PROTOCOL)
  │   │       ├── 不支持 → 等2s重试
  │   │       └── 支持 → 发送识别字符串 → START_ACCESSORY
  │   │
  │   ├── Phase 1: 等待手机断开(5s超时)
  │   └── Phase 2: 等待手机以AOA设备重连(30s)
  │       ├── 重连+枚举为AOA → 成功 ✓
  │       └── 重连但非AOA → 重发START(最多3次)
  │
  └── 失败: connect_fail≥3 → 返回ERR_PROTOCOL
```

#### 3.7.3 识别字符串

| Index | 值 |
|-------|-----|
| 0 | "VoiceCtrl" (Manufacturer) |
| 1 | "CH32V307" (Model) |
| 2 | "1.0" (Version) |
| 3 | "" (URI) |
| 4 | "" (Serial) |
| 5 | "Voice Control for Paraplegic Patients" (Description) |

#### 3.7.4 Bulk传输

- **MCU→手机** (bulk OUT, `AOA_Host_Send`): 3次重试, 失败复位toggle
- **手机→MCU** (bulk IN, `AOA_Host_Recv`): 非阻塞读取
- **断线检测** (`AOA_Host_Poll`): READY状态下三重去抖检测端口失联

### 3.8 命令分发 (phone_control.c)

#### 3.8.1 初始化

```
Phone_Control_Init()
  ├── USART1_RX_Init (串口命令调试)
  ├── OLED_Init + 显示"USB Waiting..."
  ├── Mic_Hum_Init (麦克风采集)
  ├── AOA_Host_Init + AOA_Host_Connect
  └── Print_Help (串口菜单)
```

#### 3.8.2 主循环

```
Phone_Control_Poll()
  ├── AOA连接状态维护 (断线5s冷却后自动重连)
  ├── 串口命令处理 (0-9/a-d调试命令)
  ├── 吹气指令处理: Mic_Hum_Poll → mean_a/mean_b + hum_cmd
  ├── OLED显示更新: OLED_UpdateDisplay(mean_a, mean_b)
  ├── 接收Android命令: 0xFD/0xFC/0xFB/0xFA
  ├── 能量包发送: (energy_stream || collect_mode) && mean>0 → 每3次发1包
  └── 命令处理:
      ├── collect_mode → 发送14字节采集结果
      └── 命令模式 → HUM_CMD映射AOA命令字节 → Send_CmdByte
```

#### 3.8.3 Android指令解析

| 指令 | 含义 | 动作 |
|------|------|------|
| 0xFD | 启用能量流 | energy_stream=1, collect_mode=0 |
| 0xFC | 停用能量流 | energy_stream=0, collect_mode=0 |
| 0xFB+id+vH+vL | 设置参数 | id: 0=阈值 1=短吹 2=组合 3=长吹 4=中吹 5=间隔 |
| 0xFA | 进入采集模式 | collect_mode=1 |

### 3.9 OLED显示 (OLED.c)

#### 3.9.1 硬件SPI1驱动

- 显存方案: `oled_gram[8][128]` (8页×128列)
- `OLED_SetPixel(x, y, on)`: 修改显存
- `OLED_Refresh()`: 显存→屏幕, 每页设CS/DC后连续发送128字节
- `SPI_SendByte`: 硬件SPI轮询 (等TXE→SendData→等BSY)

#### 3.9.2 显示布局

```
Line 1 (y=0~15):  "V307  USB:OK" / "USB:Wait"
Line 2 (y=16~31): "A:xxxx B:xxxx"  (瞬时能量值)
y=32~63:          [A柱状图 | B柱状图]
                  x=2~60    x=66~124
                  scale=500, 32像素高, 从底部往上
```

每4次Poll刷新一次(约32ms), 避免频繁刷新阻塞主循环。

---

## 4. Android App

### 4.1 项目结构

```
voice_control/app/src/main/
├── java/com/voice/control/
│   ├── MainActivity.java          # 主界面+命令分发
│   ├── UsbAccessoryManager.java   # USB AOA管理
│   ├── VoiceAccessibilityService.java # 无障碍服务(2208行)
│   ├── BilibiliAdapter.java       # B站适配器
│   ├── WechatAdapter.java         # 微信适配器
│   ├── XimalayaAdapter.java       # 喜马拉雅适配器
│   ├── AppAdapter.java            # 适配器基类
│   ├── AppConstants.java          # 包名常量
│   ├── GestureHelper.java         # 手势模拟
│   ├── WechatCallController.java  # 微信视频通话控制
│   ├── WechatIds.java             # 微信节点ID管理
│   ├── NodeHelper.java            # 节点操作工具
│   └── BlowBarView.java           # 波形显示View
├── res/
│   ├── layout/                    # 布局文件
│   ├── xml/
│   │   ├── accessory_filter.xml   # AOA设备过滤
│   │   └── accessibility_service_config.xml # 无障碍配置
│   └── AndroidManifest.xml
```

### 4.2 USB AOA管理 (UsbAccessoryManager)

#### 4.2.1 核心接口

```java
public class UsbAccessoryManager {
    interface Callback {
        void onConnected();
        void onDisconnected();
        void onCommandReceived(int command);
        void onEnergyReceived(int meanA, int meanB);
        void onCollectResult(int j1, int t1, int p1, int a1,
                             int j2, int t2, int p2, int a2, int gap);
    }
}
```

#### 4.2.2 连接流程

```
start()
  ├── 注册BroadcastReceiver (ATTACHED/DETACHED/USB_PERMISSION)
  ├── 启动读线程 readLoop
  └── tryConnect()
      ├── getAccessoryList[0]
      ├── openAccessory(acc)  // 先尝试直接打开(无对话框)
      └── 失败 → requestPermission(acc)  // 弹系统对话框
```

#### 4.2.3 数据解析 (readLoop)

```
state 0 (等待):
  0xFE → state 1 (能量帧, 4字节)
  0xFA → state 2 (采集帧, 13字节)
  其他 → onCommandReceived(cmd)

state 1: 读4字节 → meanA, meanB → onEnergyReceived
state 2: 读13字节 → j1/t1/p1/a1/j2/t2/p2/a2/gap → onCollectResult
```

#### 4.2.4 连接持久化

- `callback`和`context`为volatile非final, 支持跨Activity重建
- `setCallback(ctx, cb)`: Activity销毁后仍可接收USB事件
- `start()`可重入: unregisterReceiver防重复注册, readThread不重复创建
- `isRunning()`: 返回running状态供外部查询

### 4.3 主界面 (MainActivity)

#### 4.3.1 双页面结构

- **控制页 (position 0)**: 应用选择列表 + 状态显示
- **设置页 (position 1)**: 参数SeekBar + 采集按钮 + 波形显示

ViewPager2翻页时发送0xFD(设置页)/0xFC(控制页)给MCU, 控制能量流开关。

#### 4.3.2 命令分发

```java
onCommand(cmd)
  ├── STATE_SELECT:
  │   ├── PLAY/BACK/VOL_UP/DN → tryAutoControl (前台匹配则进入CONTROL)
  │   ├── NEXT/PREV → 切换selectedApp (循环)
  │   └── ENTER → openApp(slot)
  │
  └── STATE_CONTROL:
      ├── autoSwitchByForeground (前台变化时更新currentPkg)
      ├── getAdapter(currentPkg).handleCommand(cmd)
      │   ├── B站 → BilibiliAdapter
      │   ├── 微信 → WechatAdapter
      │   ├── 喜马拉雅 → XimalayaAdapter
      │   └── null → handleDefaultCommand
      └── handleDefaultCommand:
          ├── PLAY/NEXT/PREV/ENTER → sendMediaKey (KEYCODE_MEDIA_xxx)
          ├── BACK/RETURN → returnToSelect
          ├── VOL_UP/DN → audioManager.adjustVolume
          ├── LEFT → doSwipe(上翻页)
          ├── RIGHT → doSwipe(下翻页)
          └── VOL_MODE → 切换volumeMode
```

#### 4.3.3 应用列表

14个预定义应用: 网易云/QQ音乐/酷狗/酷狗概念版/百度音乐/小米音乐/B站/爱奇艺/腾讯视频/优酷/喜马拉雅/微信/番茄/起点。按分类(音乐/视频/小说/适配)排序, 适配类用橙色标记。

#### 4.3.4 校准系统

4条横线(topLine/botLine/centerTopLine/centerBotLine) + 2条竖线(leftLine/rightLine), 可拖动调整。保存为全局(`calib_top`)或按App(`calib_top_<pkg>`)。影响节点收集边界和滚动目标中心。

### 4.4 无障碍服务 (VoiceAccessibilityService)

#### 4.4.0 Android无障碍节点与View ID

**无障碍节点（AccessibilityNodeInfo）是什么？**

Android的无障碍框架会在当前屏幕的View树上为每个View生成一个`AccessibilityNodeInfo`对象。它不是View对象本身，而是View的**只读快照**，包含View的属性和操作能力。无障碍服务通过`getRootInActiveWindow()`获取根节点，再通过`getChild()`遍历整棵树。

```
View树 (应用进程内)          AccessibilityNodeInfo树 (无障碍服务进程内)
┌─DecorView                 ┌─AccessibilityNodeInfo (root)
│ └─FrameLayout             │ └─AccessibilityNodeInfo
│    └─RecyclerView         │    └─AccessibilityNodeInfo (isScrollable=true)
│       └─CardView          │       └─AccessibilityNodeInfo (isClickable=true)
│          └─TextView       │          └─AccessibilityNodeInfo (text="标题")
```

每个`AccessibilityNodeInfo`提供的关键属性：

| 属性 | 方法 | 说明 |
|------|------|------|
| 文本 | `getText()` | View显示的文字 |
| 描述 | `getContentDescription()` | android:contentDescription属性 |
| View ID | `getViewIdResourceName()` | android:id属性的完整资源名 |
| 类名 | `getClassName()` | View的Java类名(如android.widget.TextView) |
| 屏幕坐标 | `getBoundsInScreen(Rect)` | View在屏幕上的位置和大小 |
| 可点击 | `isClickable()` | 是否可点击 |
| 可滚动 | `isScrollable()` | 是否可滚动 |
| 子节点数 | `getChildCount()` / `getChild(i)` | 遍历子节点 |

**View ID是什么？是XML里的android:id吗？**

**是的，就是XML里`android:id`指定的资源ID**，但返回的是**完整资源名**，格式为 `包名:id/ID名`。

例如B站布局XML中：
```xml
<androidx.recyclerview.widget.RecyclerView
    android:id="@+id/recycler_feed"  ← 这个id
    ... />
```

无障碍服务读到的`getViewIdResourceName()`返回值：
```
tv.danmaku.bili:id/recycler_feed
```

拆解：
- `tv.danmaku.bili` — 应用的包名（资源所属的package）
- `:id/` — 固定分隔符，表示这是ID资源
- `recycler_feed` — XML中`@+id/`后面的名字

**如果View没有设`android:id`**，`getViewIdResourceName()`返回`null`。

**本项目如何使用View ID：**

1. **B站等标准App** — ID名稳定可读，直接用字符串匹配：
```java
// VoiceAccessibilityService.java:1803
viewId.contains("card")  // 匹配所有含"card"的ID，如"card_item"、"video_card"
```

2. **微信** — ID被混淆为无意义短串（如`bth`、`cj1`），且每个版本不同：
```java
// WechatIds.java:21-30 — 存储混淆ID的默认值
DEFAULT_CHAT_ITEM_NEW = "bth"   // 微信8.x聊天列表项
DEFAULT_CHAT_ITEM_OLD = "cj1"   // 旧版聊天列表项
DEFAULT_NAV_TAB = "kd_"         // 底部导航栏
DEFAULT_MOMENTS_CARD = "nq_"    // 朋友圈卡片
DEFAULT_SCROLL_LIST = "gkp"     // 滚动列表容器
```

完整匹配方式（`WechatIds.java:67-69`）：
```java
public static boolean isChatItem(String viewId) {
    return viewId.contains(":id/" + chatItemNew())    // "com.tencent.mm:id/bth"
        || viewId.contains(":id/" + chatItemOld());   // "com.tencent.mm:id/cj1"
}
```

3. **微信ID自动提取**（`scanNodesForIds`, line 638-694）— 微信更新后混淆ID会变，项目通过启发式算法自动提取新ID：
   - 遍历所有节点，提取`viewId`中`:id/`后面的部分
   - **字母索引**：text是单个大写字母(A-Z)的节点 → `letterIndexIds`
   - **聊天项**：可点击+出现次数多+高度一致的节点 → `chatItem`
   - **联系人名**：叶子节点+text长度2~50+非标题文字 → `contactName`
   - **滚动容器**：className含RecyclerView/ListView → `scrollList`
   - **根FrameLayout**：宽高>屏幕90% → `rootFrame`
   - 提取后存入SharedPreferences，下次直接使用

4. **ID查找**（`NodeHelper.java:78-87`）— 通过ID在节点树中定位元素：
```java
public static AccessibilityNodeInfo findNodeById(AccessibilityNodeInfo node, String idPart, int depth) {
    String vid = node.getViewIdResourceName();  // "com.tencent.mm:id/gkp"
    if (vid.contains(":id/" + idPart)) return node;  // 匹配":id/gkp"
    for (int i = 0; i < node.getChildCount(); i++) {
        AccessibilityNodeInfo result = findNodeById(node.getChild(i), idPart, depth + 1);
        if (result != null) return result;
    }
    return null;
}
```

**为什么用View ID而不是text/desc？**

| 定位方式 | 优点 | 缺点 | 适用场景 |
|----------|------|------|----------|
| View ID | 稳定(不随内容变) | 微信会混淆, 需版本适配 | 结构性元素(列表项/导航栏) |
| text | 直观 | 随语言/内容变 | 临时查找("播放"/"确定") |
| desc | 语义明确 | 很多View没设desc | 带描述的按钮(点赞/收藏) |
| 坐标 | 通用 | 分辨率依赖 | 校准线/红框定位 |

本项目混合使用：B站用desc关键词+viewId含"card"，微信用混淆ID+自动提取，通用回退用text查找。

#### 4.4.1 配置

```xml
accessibilityEventTypes: typeViewClicked|typeWindowStateChanged|typeWindowContentChanged
accessibilityFlags: flagReportViewIds|flagRetrieveInteractiveWindows
canRetrieveWindowContent: true
canPerformGestures: true  <!-- doSwipe/doTap依赖此项 -->
```

#### 4.4.2 焦点导航流程

```
focusNext() → focusStep(isNext=true)
  ├── refreshFocusableNodes()
  │   ├── 读取校准线 (按pkg覆盖全局)
  │   ├── 分流收集:
  │   │   ├── 微信导航模式 → collectWechatNavTabs
  │   │   ├── 微信朋友圈 → collectWechatMomentsCards
  │   │   ├── 微信通讯录 → collectWechatContactsItems
  │   │   ├── 微信直播 → collectWechatLiveCards
  │   │   ├── 微信通用 → collectWechatGenericItems
  │   │   └── 其他App → collectClickable
  │   ├── 排序 (先top后left)
  │   └── 统计avgCardHeight (微信阈值100, 其他200)
  │
  ├── 计算下一个焦点 (当前索引±1)
  ├── 越界 → swipeForNewCards (翻页)
  └── focusCurrent() → scrollToVisible() → showHighlight()
```

#### 4.4.3 节点收集 (collectClickable)

筛选条件:
- **desc关键词**: 观看/看过/弹幕/点赞/投币/收藏/人气值/UP主/主播/专辑
- **viewId含"card"**
- **微信chatItem**
- **子节点含main_sound_name**

边界检查:
- **微信**: 完全包含 `r.top >= topLine && r.bottom <= botLine`
- **其他App**: 交集 `r.bottom > topLine && r.top < botLine` (部分露出也收集)

#### 4.4.4 滚动到可见区 (scrollToVisible)

```
不滑动条件 (按顺序判断):
  1. depth >= 10 (深度上限)
  2. cardTooTall && inCenter:
     - 微信: 巨长卡片中心在中心区 → 不滑动(防振荡)
     - 其他: |offset| < avgCardHeight/3 → 已对齐不滑动
  3. 完全在中心区 [centerTopLine, centerBotLine] → 不滑动
  4. wechatNoScroll || (inBounds && heightOk && inCenter) → 不滑动
  5. 滚动停滞 (top/bottom未变) → 不滑动

滑动:
  offset = centerCard - targetCenter
  moveDist = |offset| / scrollRatio (自适应比例)
  doSwipe → waitForStable → 比例校准(0.7旧+0.3新) → 递归
```

#### 4.4.5 翻页 (swipeForNewCards)

```
  1. 记录翻页前卡片desc列表 beforeCards
  2. PREV优先用 ACTION_SCROLL_BACKWARD
  3. NEXT: doSwipe(0.7→0.3)  PREV: doSwipe(0.3→0.7)
  4. waitForScrollStable (基于TYPE_VIEW_SCROLLED事件)
  5. 对比新卡片列表, 选离中心最近的
```

#### 4.4.6 边界变量

| 变量 | 默认值 | 含义 |
|------|--------|------|
| topLine | 240 | 上边界(卡片此线以下才收集) |
| botLine | 1504 | 下边界 |
| centerTopLine | 600 | 中心区上界(滚动目标) |
| centerBotLine | 1000 | 中心区下界 |
| avgCardHeight | 425 | 平均卡片高度(用于heightOk判断) |

#### 4.4.7 红框高亮

`showHighlightClipped(r)`: 将Rect裁剪到[topLine, botLine]×[0, screenW]再显示, 防止红框超出校准线范围。用`GradientDrawable`+`TYPE_APPLICATION_OVERLAY`窗口绘制。

### 4.5 App适配器

#### 4.5.1 B站 (BilibiliAdapter)

| 命令 | 全屏 | 直播间 | 竖屏视频 | 普通 |
|------|------|--------|----------|------|
| PLAY | 双击播放/暂停 | - | - | - |
| NEXT | 唤起控制栏+MEDIA_NEXT | 开关弹幕 | 上滑 | focusNext |
| PREV | 退出全屏 | 全屏切换 | 下滑 | focusPrev |
| ENTER | - | - | - | clickFocused |
| BACK | removeHighlight+performBack | - | - | 非B站则returnToSelect |

全屏检测: `bbplayer_fullscreen`节点或横屏。竖屏视频: `story_view_pager`/`feed_videos`。直播间: `live_room_close`/`live_send_danmaku`。

#### 4.5.2 微信 (WechatAdapter)

- **导航栏模式**: PLAY切换, NEXT/PREV在4个tab间循环
- **朋友圈**: 卡片导航, 广告识别(含"广告"desc)
- **通讯录**: 字母索引+联系人卡片
- **视频通话**: 来电检测+控制栏跟随+接听重试
- **ID自动提取**: collectXxx返回空时autoExtractByPage重新提取

#### 4.5.3 喜马拉雅 (XimalayaAdapter)

- 详情页"立即播放"/"下一集播放"切换
- 老年模式`elderly_vg_play_btn`检测

---

## 5. 通信协议

### 5.1 MCU → App (bulk OUT)

#### 单字节命令

| 值 | 命令 | 说明 |
|----|------|------|
| 0x02 | ENTER | 确定/点击 |
| 0x03 | BACK | 返回 |
| 0x04 | PLAY | 播放/暂停 |
| 0x05 | NEXT | 下一个 |
| 0x06 | PREV | 上一个 |
| 0x07 | VOL_UP | 音量+ |
| 0x08 | VOL_DN | 音量- |
| 0x09 | RETURN | 返回选择 |
| 0x0A | MODE | 模式切换 |
| 0x0B | LEFT | 左 |
| 0x0C | RIGHT | 右 |
| 0x0D | VOL_MODE | 音量模式切换 |

#### 能量包 (5字节)

```
[0xFE] [meanA_H] [meanA_L] [meanB_H] [meanB_L]
```
发送条件: `(energy_stream_enabled || collect_mode) && mean > 0`, 每3次Poll发1包。

#### 采集结果 (14字节)

```
[0xFA] [j1] [t1] [p1_H] [p1_L] [a1_H] [a1_L] [j2] [t2] [p2_H] [p2_L] [a2_H] [a2_L] [gap]
```
吹气命令触发时发送(collect_mode下)。j=判定(1短/2中/3长), t=tick数, p=峰值, a=平均值, gap=跨麦间隙。

### 5.2 App → MCU (bulk IN)

| 指令 | 格式 | 说明 |
|------|------|------|
| 0xFD | 单字节 | 启用能量流(进入设置页) |
| 0xFC | 单字节 | 停用能量流(离开设置页) |
| 0xFA | 单字节 | 进入采集模式 |
| 0xFB | 4字节: 0xFB+id+vH+vL | 设置参数 |

参数ID: 0=阈值 1=短吹上限 2=组合等待 3=长吹间隔 4=中吹上限 5=最小间隔

---

## 6. 构建与烧录

### 6.1 MCU固件编译

**工具链**: MounRiver Studio 2 (RISC-V Embedded GCC)

```
工具链路径:
  D:\InstallData\EmbTools\MounRiver\MounRiver_Studio2\resources\app\
    resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin

Make路径:
  D:\InstallData\EmbTools\MounRiver\MounRiver_Studio2\resources\app\
    resources\win32\others\Build_Tools\Make\bin\make.exe
```

**编译命令** (使用build.ps1脚本):
```powershell
$env:PATH = "<toolchain>;<make>;" + $env:PATH
Set-Location "D:\Libraries\Projects\VOICE\VOICE_V307"
make -C obj all
```

**输出**: `obj/TEST-INMP441-DOUBLE.elf` / `.hex` / `.bin`

### 6.2 Android App编译

**要求**: JDK 17

```powershell
$env:JAVA_HOME = "D:\InstallData\ProgrammingTools\Environment\Java\jdk17"
.\gradlew.bat assembleDebug
```

**输出**: `app/build/outputs/apk/debug/app-debug.apk`

### 6.3 烧录

- **MCU**: WCH-LinkE调试器 + MounRiver Studio烧录, 或WCH ISP工具
- **Android**: `adb install app-debug.apk` 或直接安装

### 6.4 首次使用设置

1. 安装Android App
2. 开启无障碍服务 (设置→无障碍→声音控制)
3. 连接USB: MCU ↔ 手机数据线
4. 手机弹"打开声音控制"对话框 → 点"允许" + 勾"始终使用"
5. App显示"USB已连接" → 系统就绪

---

## 7. USBHS技术详解

> **一句话记住：UART、I2C、SPI 是"板内小总线"，USB 是"外部设备总线系统"。USBHS 就是 USB 2.0 的 High Speed 高速档：理论 480 Mbps，实际批量传输大概几十 MB/s。**

### 7.1 从板内总线到USB

| 总线 | 线 | 拓扑 | 谁主动 | 时钟 | 速度 | 典型用途 |
|------|-----|------|--------|------|------|----------|
| UART | TX/RX/GND | 点对点 | 双方异步 | 无时钟线 | 几k~几M | 调试、模块通信 |
| I2C | SCL/SDA | 一主多从 | 主机发起 | 同步 | 100k~3.4M | 传感器、EEPROM |
| SPI | SCLK/MOSI/MISO/CS | 一主多从 | 主机发起 | 同步 | 几M~几十M | Flash、屏幕、ADC |
| USB | D+/D-/VBUS/GND | 星型+Hub | 主机轮询 | 从数据恢复 | FS 12M / HS 480M | 键鼠、U盘、采集 |

通俗比喻：
- **UART**：两人对讲机，约好语速，喊一句停一下
- **I2C**：老师点名，学生共用两根线答到，每个学生有地址
- **SPI**：老师给每个学生单独拉一根片选，再打拍子，大家同时收发
- **USB**：公司网络。主机是老板，Hub是部门经理，设备是员工。每个员工有工号（设备地址）和多个信箱（端点）。老板按计划问"工号3，1号信箱有数据吗？"员工才能回答。新员工入职要登记（枚举）

USB不是"更快的串口"，它是带供电、热插拔、地址、端点、协议栈和操作系统驱动的**总线系统**。

### 7.2 USB核心概念

**主机是老大**：USB设备不能主动发数据。键盘鼠标也要等主机周期性问"你有数据吗？"才能回。这叫**主机轮询**。

**地址+端点**：USB设备有设备地址，设备内部有端点（信箱）：
- EP0：控制端点，必须存在，用于枚举和命令
- IN端点：设备→主机
- OUT端点：主机→设备
- 每个端点有类型、最大包大小、轮询间隔

**包和事务**：USB底层不是字节流，是包。一次事务 = 令牌包（寻址）+ 数据包 + 握手包（ACK/NAK/STALL）。

**四种传输类型**：

| 类型 | 特点 | 用途 | 本项目使用 |
|------|------|------|-----------|
| 控制传输 | 枚举/命令, EP0 | 设备配置 | ✅ AOA握手 |
| 中断传输 | 小数据, 周期, 可靠 | 键盘鼠标 | ✗ |
| 批量传输 | 大数据, 可靠, 不保证周期 | U盘 | ✅ AOA命令/能量 |
| 同步传输 | 实时, 允许丢包 | 音频视频 | ✗ |

**枚举流程**：检测设备 → 复位 → 地址0 → 读描述符(设备/配置/接口/端点) → 分配新地址 → 选配置 → 加载驱动。

### 7.3 USB 2.0速度档位与USBHS

| 项目 | Low Speed | Full Speed | High Speed (USBHS) |
|------|-----------|------------|---------------------|
| 速度 | 1.5 Mbps | 12 Mbps | 480 Mbps |
| 帧 | 1ms | 1ms | 1ms, 分8个125us微帧 |
| 批量最大包 | 8字节 | 64字节 | 512字节 |
| PHY | — | 全速PHY | 高速PHY(ULPI/UTMI) |
| 识别 | D-上拉 | D+上拉 | 先FS, 再chirp切HS |
| 实际带宽 | — | ~1MB/s | ~几十MB/s |

USBHS关键点：
- 仍然是**半双工**，D+/D-一对差分线
- USB 3.x才是全双工(5Gbps+)，线也更多
- HS下1ms分成8个**125us微帧**
- 高速设备插入时先像FS设备，通过**chirp握手**切到HS
- HS需要高速PHY，MCU常见接口是ULPI或UTMI
- HS批量端点最大包通常512字节

### 7.4 CH32V307 USBHS硬件架构

CH32V307的USBHS是USB 2.0高速控制器，同时支持Host和Device模式。寄存器基址`AHBPERIPH_BASE + 0x3400`，挂载在AHB总线。内置高速PHY，无需外接ULPI。

#### 7.4.1 寄存器概览

两套寄存器视图（通过`CONTROL`寄存器的`HOST_MODE`位切换）：

```
USBHSD_TypeDef (Device模式): 16个端点(EP0~EP15), 每个独立DMA/长度/控制
USBHSH_TypeDef (Host模式):   单组共享收发DMA, 用HOST_EP_PID临时选端点
```

关键寄存器一览：

| 寄存器 | 宽度 | 模式 | 功能 |
|--------|------|------|------|
| CONTROL | 8 | 共用 | 模式选择/速度/复位/DMA使能 |
| HOST_CTRL | 8 | Host | SOF使能/总线复位/挂起 |
| INT_EN / INT_FG | 8 | 共用 | 中断使能/标志 |
| INT_ST | 8 | 共用 | 中断状态(Token/响应/Toggle) |
| DEV_AD | 8 | 共用 | USB设备地址 |
| SPEED_TYPE | 8 | Host | 对端速度类型 |
| MIS_ST | 8 | Host | 设备连接/SIE状态 |
| RX_LEN | 16 | Host | 接收数据长度 |
| FRAME_NO | 16 | Host | SOF帧号 |

#### 7.4.2 CONTROL寄存器

```
Bit 7: HOST_MODE    1=Host, 0=Device
Bit 6:5: SPEED_TYPE  High(0x20)/Full(0x00)/Low(0x40)
Bit 3: INT_BUSY     SIE忙时自动暂停CPU
Bit 2: RESET_SIE    复位协议引擎
Bit 1: CLR_ALL      清除所有USB状态
Bit 0: DMA_EN       DMA使能
```

本项目Host初始化（`ch32v30x_usbhs_host.c:62`）：
```c
USBHSH->CONTROL = USBHS_UC_HOST_MODE | USBHS_UC_SPEED_HIGH
                | USBHS_UC_INT_BUSY | USBHS_UC_DMA_EN;
```

#### 7.4.3 Device模式端点

16个独立端点，每个有完整硬件资源：

**ENDP_CONFIG（32位）** — 端点使能：
```
Bit 31~16: R_EN (EP15~EP0 接收使能)
Bit 15~0:  T_EN (EP15~EP0 发送使能)
```

**ENDP_TYPE（32位）** — 端点类型：0=Bulk, 1=Interrupt, 2=Isochronous, 3=Control

**BUF_MODE（32位）** — 双缓冲使能：低16位Bulk/Interrupt双缓冲，高16位等时双缓冲

每个端点的独立寄存器组：

| 寄存器 | 宽度 | 功能 |
|--------|------|------|
| UEPn_RX_DMA | 32 | 接收缓冲区地址 |
| UEPn_TX_DMA | 32 | 发送缓冲区地址 |
| UEPn_MAX_LEN | 16 | 最大包长(0~2047) |
| UEPn_TX_LEN | 16 | 发送长度(写0=零长度包) |
| UEPn_TX_CTRL | 8 | 发送控制(Toggle+响应) |
| UEPn_RX_CTRL | 8 | 接收控制(Toggle+响应) |

> EP0特殊：只有`UEP0_DMA`（共用收发），因为EP0是控制端点，SETUP/IN/OUT分时复用。

#### 7.4.4 Host模式端点

Host模式**只有一组共享收发通道**，通过`HOST_EP_PID`临时指定端点号和Token：

```
HOST_EP_PID (8位):
  Bit 7~4: Token PID   Bit 3~0: Endpoint Number
    0x0=OUT  0x1=SOF  0x2=IN  0x3=SETUP
```

Host共享缓冲区：
```
HOST_TX_DMA → USBHS_TX_Buf[64]  (发送, 4字节对齐)
HOST_RX_DMA → USBHS_RX_Buf[64]  (接收, 4字节对齐)
HOST_RX_MAX_LEN = 64
```

要和不同端点通信，只需改`HOST_EP_PID`低4位，收发缓冲区不变。Host是主动方，一次只和一个端点通信。

#### 7.4.5 端点控制寄存器（TX_CTRL / RX_CTRL）

```
Bit 6:   DATA_NO  (仅Host) 1=不检查Toggle匹配
Bit 5:   TOG_AUTO  1=硬件自动翻转DATA0/DATA1
Bit 4:3: TOG_MASK  手动Toggle: 00=DATA0 01=DATA1 10=DATA2 11=MDATA
Bit 2:   RES_NO  (仅Host) 1=不检查设备响应
Bit 1:0: RES_MASK 响应: 00=ACK 01=NYET 10=NAK 11=STALL
```

**数据同步Toggle机制**：DATA0/DATA1交替传输，防止丢包/重复：

```
主机                          设备
  │  SETUP (DATA0)              │
  │ ──────────────────────►     │
  │  ACK                        │
  │ ◄──────────────────────     │
  │  DATA1 (第一包)             │
  │ ◄──────────────────────     │
  │  ACK                        │
  │  DATA0 (第二包)             │
  │ ◄──────────────────────     │
```

Toggle不匹配时`INT_ST & USBHS_UIS_TOG_OK = 0`，触发重传。

- **自动Toggle**（`TOG_AUTO=1`）：硬件自动翻转，Bulk/Interrupt端点常用
- **手动Toggle**（`TOG_AUTO=0`）：控制传输EP0必须手动管理

#### 7.4.6 中断系统

| 位 | 宏 | 含义 |
|----|-----|------|
| 7 | UIE_DEV_NAK | 设备NAK |
| 6 | UIE_ISO_ACT | 等时传输完成 |
| 5 | UIE_SETUP_ACT | SETUP完成 |
| 4 | UIE_FIFO_OV | FIFO溢出 |
| 3 | UIE_SOF_ACT | SOF帧 |
| 2 | UIE_SUSPEND | 总线挂起 |
| 1 | UIE_TRANSFER | 传输完成 |
| 0 | UIE_DETECT | 设备插拔检测 |

INT_ST中的响应码：
- Bit 7: `IS_NAK` — NAK标志
- Bit 6: `TOG_OK` — Toggle匹配
- Bit 5:4: `TOKEN_MASK` — OUT(0x00)/SOF(0x10)/IN(0x20)/SETUP(0x30)
- Bit 3:0: `H_RES_MASK` — ACK/NAK/STALL

#### 7.4.7 时钟配置

```c
// ch32v30x_usbhs_host.c:32-40
RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);  // 48M时钟←内部PHY
RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);    // PLL←外部晶振
RCC_USBHSConfig(RCC_USBPLL_Div2);                     // USBHS=PLL/2
RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);  // PLL参考=4MHz
RCC_USBHSPHYPLLALIVEcmd(ENABLE);                     // PLL Alive
RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);   // AHB时钟使能
```

### 7.5 本项目中的USBHS使用

本项目MCU做**USB Host**，手机做**USB Device（AOA Accessory模式）**。

#### 7.5.1 端点分配

| 端点 | 方向 | 类型 | 用途 |
|------|------|------|------|
| EP0 | 双向 | Control | AOA握手(SEND_PROTOCOL/START_ACCESSORY/枚举) |
| EP1 OUT | 主机→手机 | Bulk | 发送命令包/能量包/采集结果 |
| EP1 IN | 手机→主机 | Bulk | 接收Android指令(0xFD/0xFC/0xFB/0xFA) |

> `USBHS_MAX_PACKET_SIZE = 64`（`ch32v30x_usbhs_host.h:35`），对应**全速Bulk**端点最大包长。虽然USBHS硬件支持高速480Mbps，但Android AOA通常协商为全速12Mbps，所以bulk包长64字节。

#### 7.5.2 Host初始化流程

```
USBHS_RCC_Init()          // 时钟配置
USBHS_Host_Init(ENABLE):
  1. CLR_ALL + RESET_SIE  → 复位USB模块
  2. CONTROL = HOST_MODE | SPEED_HIGH | INT_BUSY | DMA_EN
  3. HOST_EP_CONFIG = EP_TX_EN | EP_RX_EN  → 使能收发
  4. HOST_RX_DMA = &USBHS_RX_Buf  → 设置接收缓冲区
  5. HOST_TX_DMA = &USBHS_TX_Buf  → 设置发送缓冲区
  6. HOST_RX_MAX_LEN = 64  → 最大包长
  7. HOST_CTRL = PHY_SUSPENDM  // PHY不挂起
```

#### 7.5.3 事务传输（USBHSH_Transact）

```
1. 设置 HOST_TX_CTRL/HOST_RX_CTRL = toggle (DATA0/DATA1)
2. HOST_EP_PID = (PID << 4) | endpoint  → 启动事务
3. INT_FG = UIF_TRANSFER  → 清标志, 允许传输
4. 轮询等待 INT_FG & UIF_TRANSFER  → 传输完成
5. HOST_EP_PID = 0x00  → 停止传输
6. 检查响应:
   - IN事务: TOG_OK=1 → 成功
   - OUT/SETUP: ACK/NYET → 成功
   - STALL → 返回错误
   - NAK → 重试(最多10次, timeout递减)
7. 传输中检测到设备断开 → 返回ERR_USB_CONNECT
```

#### 7.5.4 控制传输（USBHSH_CtrlTransfer）

用于EP0的AOA握手，三阶段：

```
SETUP阶段: 发8字节Setup请求 (HOST_TX_LEN=8, PID=SETUP)
DATA阶段:
  IN方向(设备→主机): 循环接收, 每次≤ep0_size字节, 存入pbuf
  OUT方向(主机→设备): 循环发送, 每次≤ep0_size字节
  每次成功后翻转Toggle: HOST_RX_CTRL ^= R_TOG_DATA1
STATUS阶段: 反方向零长度包确认
```

#### 7.5.5 Bulk传输（AOA命令/能量/采集）

**发送（MCU→手机）** — `USBHSH_SendEndpData`:
```c
// 拷贝数据到USBHS_TX_Buf
// HOST_TX_LEN = len
// USBHSH_Transact((USB_PID_OUT << 4) | endpoint, toggle, timeout)
// 成功后翻转toggle
```

**接收（手机→MCU）** — `USBHSH_GetEndpData`:
```c
// USBHSH_Transact((USB_PID_IN << 4) | endpoint, toggle, timeout)
// 从USBHS_RX_Buf拷贝数据
// RX_LEN获取实际接收长度
```

本项目AOA层封装（`aoa_host.c`）：
- `AOA_Host_Send`: 3次重试, 失败复位toggle
- `AOA_Host_Recv`: 非阻塞读取, NAK视为无数据

#### 7.5.6 DMA与缓冲区

USBHS控制器内置DMA，与通用DMA（DMA1/DMA2）无关：

```
Host模式:
  HOST_TX_DMA → USBHS_TX_Buf[64]  (4字节对齐, __attribute__((aligned(4))))
  HOST_RX_DMA → USBHS_RX_Buf[64]  (4字节对齐)

  发送: CPU写TX_Buf → 设HOST_TX_LEN → 硬件自动DMA到USB线
  接收: 硬件从USB线DMA到RX_Buf → 设INT_FG UIF_TRANSFER → CPU读RX_Buf
```

> 注意：USBHS的DMA是控制器**内部DMA**，不需要配置DMA1/DMA2通道。与I2S麦克风采集使用的外设DMA完全独立。

#### 7.5.7 速度协商

`USBHS_Host_Init`固定设`SPEED_HIGH`，但实际速度取决于对端设备：

```c
// USBHSH_ResetRootHubPort中:
USBHSH_SetSelfSpeed(USB_HIGH_SPEED);  // 初始期望高速

// USBHSH_CheckRootHubPortSpeed检查实际速度:
speed = USBHSH->SPEED_TYPE & USBHS_USB_SPEED_TYPE;
// 返回 USB_LOW_SPEED / USB_FULL_SPEED / USB_HIGH_SPEED
```

Android手机AOA模式通常为**全速(12Mbps)**，虽然Host请求高速，但设备端PHY只支持全速，最终协商为全速。这就是`USBHS_MAX_PACKET_SIZE=64`（全速Bulk最大包）而非512（高速Bulk最大包）的原因。

#### 7.5.8 断连重连处理

本项目对USBHS断连的特殊处理（`aoa_host.c`）：

```
AOA_Host_Connect Phase 0:
  USBHS_Host_Init(DISABLE)  → 完全关闭USBHS
  Delay 500ms
  USBHS_Host_Init(ENABLE)   → 重新初始化
  (目的: 清除Android端AOA框架残留状态, 让手机重新走AOA协商)
```

`USBHSH_Transact`内部也有断连检测（`ch32v30x_usbhs_host.c:352`）：
```c
if (USBHSH->INT_FG & USBHS_UIF_DETECT) {
    if (USBHSH_CheckRootHubPortEnable() == 0x00)
        return ERR_USB_CONNECT;  // 设备断开
}
```

---


## 附录A: 关键时序

| 事件 | 时间 |
|------|------|
| 系统启动到USB就绪 | ~25s (5s delay + 20s AOA协商) |
| DMA中断周期 | 8ms/tick |
| 能量包发送频率 | ~24ms (每3次Poll, 每次Poll~8ms) |
| OLED刷新频率 | ~32ms (每4次Poll) |
| 长吹重复触发间隔 | 80ms (10 tick) |
| 短吹判定上限 | 320ms (40 tick) |
| 中吹判定上限 | 640ms (80 tick) |
| AOA断线重连冷却 | 5s |
| 滚动比例自适应 | 0.7旧+0.3新 |