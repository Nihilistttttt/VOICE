# VOICE 架构说明（一页纸）

> 只写**边界和决策**，不给函数写注释。想深入看 `docs/design/`。

---

## 系统边界

```
 [两个吹气口]  →  [CH32V307 固件]  ──USB AOA──→  [Android 手机]
                    识别手势            MCU 是 Host       App 是 Accessory
                    发单字节命令          手机不用 root、不用装驱动
```

---

## 一、MCU 固件（`VOICE_V307/`）

### 上电到运行

```
main()                        时钟 / 延时 / 串口就绪
  └─ 开 PB5                    = PWR_CTRL，给手机供电的开关（不是 LED）
     └─ Hardware()             按 TEST_MODE 分发到某一模式
        └─ Phone_Control_Init() + while(1) Phone_Control_Poll()
```

### 一次吹气到手机动作

```
① 采集    INMP441×2 走 I2S2/I2S3 → DMA 双缓冲 → 每 128 帧(8ms)半传输中断算能量
② 判定    mean > 阈值 → (吹 / 不吹)；两路同时吹时取能量高的那路
③ 识别    状态机 IDLE → BLOWING → BLOW_GAP → SHORT_WAIT(2)
          输出：单短 / 两短 / 三短 / 中吹 / 长吹连发
④ 发送    映射成 AOA 单字节 0x02~0x0D 发出去
```

**主循环一轮（8ms）做六件事**：AOA 连接维护 → 串口调试菜单 → 取手势 →
刷 OLED → 收 App 指令 → 发命令与能量包。
节拍由 **I2S DMA 中断**提供，不是定时器 —— 改采样率会连带改所有 tick 参数。

### 目录职责（依赖只能向下）

| 目录 | 职责 | 边界 |
|---|---|---|
| `User/` | 入口 + 中断向量 + 系统时钟 | 不放业务逻辑 |
| `Common/` | 板级配置（引脚/外设宏）+ GPIO / 时钟抽象 | 不放业务逻辑 |
| `Phone/` | 器件驱动 + 业务编排 + WCH 移植代码 + 例程 | 见下方已知包袱 |

### ⚠️ 已知包袱（先记着，暂不修）

1. **`Common/` 这名字没意义** —— 里面其实是 `Config`（板级宏）+ `hal_gpio`（引脚抽象），
   叫 `Hal/` 会清楚得多，但改名要动 MounRiver 的全部构建配置
2. **`Phone/` 把四类东西混在一起**：
   自研驱动（oled / ads1115 / joystick / mic_hum）、
   业务（phone_control / aoa_host）、
   WCH 移植（ch32v30x_usbfs_device / sw_udisk / usb_desc / sdio_sd / ff / diskio）、
   例程（sdio_test）
3. **板级引脚定义散落**：`Config.h`、各驱动 `.c`、还有原理图各一份，靠人肉保持一致
4. **`mode.h` 的一个整数决定整个固件形态**，改它要同步所有 `#if TEST_MODE` 分支

---

## 二、Android App（`voice_control/`）

```
VoiceAccessibilityService    事件入口 + 命令分发 + 各 App 适配器
MainActivity                 设置页 / 校准 / 参数
BlowBarView                  吹气能量可视化
```

- 用**无障碍服务**操作前台 App，不注入、不 hook
- 通信是 **USB AOA**，MCU 主动发命令，App 主要被动收

---

## 三、两者之间的协议

| 方向 | 字节 | 含义 |
|---|---|---|
| MCU→App | `0x02`~`0x0D` | 单字节命令：确定 / 返回 / 播放 / 下一个 / 上一个 / 音量± / 模式 / 左 / 右 |
| MCU→App | `0xFA` + 13 B | 采集包（能量 / 峰值 / 段长 / judge / gap），只用于调试与校准 |
| MCU→App | `0xFE` + 4 B | 能量包，给波形显示用 |
| App→MCU | `0xFD` / `0xFC` | 能量流 开 / 关 |
| App→MCU | `0xFB` + id + vH + vL | 改运行时参数（阈值 / 各档 tick 数） |
| App→MCU | `0xFA` | 进采集模式 |

⚠️ 两条容易踩的：
- **MCU 发 14 字节，App 只收后 13 字节**（头字节 `0xFA` 被用来切状态）
- `0xFA` 这个字节在**两个方向**上含义不同（下行为采集包，上行为"进采集模式"）

---

## 四、更详细的文档

| 文档 | 讲什么 |
|---|---|
| `docs/design/VOICE_架构优化方案.html` | 五维审计 + 6 批迁移路线 + 优先级矩阵 |
| `docs/design/V3_压力传感架构改造方案.html` | 弃用麦克风、改压差传感器的完整方案 |
| `docs/design/H417与V307_底层抽象对比与设计改进建议.html` | 两份实现逐项对比 |
| `docs/design/VOICE_代码导读手册.html` | 13 章逐模块代码导读，适合入门 |
