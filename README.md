# VOICE —— 吹气控制手机系统

面向高位截瘫患者的辅助操作设备：**靠吹气操作手机**。

CH32V307 上跑吹气检测与手势识别，通过 **USB AOA** 协议以 Host 身份控制 Android 手机；
手机端是无障碍服务，把识别到的手势落到前台 App 上。整套东西不需要手机 root、不需要装辅助外设。

---

## 目录

| 目录 | 是什么 |
|---|---|
| `VOICE_V307/` | **MCU 固件**（CH32V307，MounRiver / RISC-V GCC） |
| `voice_control/` | **Android App**（`com.voice.control`，compileSdk 34 / minSdk 28） |
| `hardware/` | 硬件设计：原理图与 PCB（`PUB/`）、嘉立创导出的网表（`电路.txt`） |
| `docs/` | 文档：`design/` 架构与设计 · `log/` 项目记录 · `ref/` 芯片资料 |

**第三方参考资料不在本仓库内**：WCH 官方 CH32H417 / CH32V307 例程库与早期试验工程
已移到本机 `D:/Libraries/Refs/` 和 `D:/Libraries/_archive/`（原文件没删，只是移出仓库）。
当时它们占了仓库 94% 的文件数 —— 取舍理由见 `docs/目录重整方案.md`。

---

## 编译固件

```powershell
cd VOICE_V307
.\build.ps1        # 等价于 make -C obj all
```

产物：`VOICE_V307/obj/TEST-INMP441-DOUBLE.hex`

需要 MounRiver Studio 自带的 RISC-V 工具链 —— **脚本里写死了本机路径，换机器要改**。
也可以用 MounRiver Studio 直接打开 `VOICE_V307/` 编译。

## 烧录

把 `obj/TEST-INMP441-DOUBLE.hex` 用 WCHISPTool 或 MounRiver 的下载器烧进 CH32V307。
内存布局见 `VOICE_V307/Ld/Link.ld`。

## 编译 App

```bash
cd voice_control
./gradlew assembleDebug
```

## 选固件模式

`VOICE_V307/Phone/mode.h` 里的 `TEST_MODE` 决定编出哪个固件（当前值 **2**）：

| 值 | 用途 |
|---|---|
| **0** | **正式功能**：吹气采集 + USB AOA 手机控制 |
| 1 | SDIO SD 卡读写速率基准 |
| 2 | USB MSC U 盘（把 SD 卡挂成 U 盘插电脑） |
| 3 | SD 卡 FAT 文件读取（读 `123.txt`） |
| 4 | USBHS Device 枚举调试 |
| 5 | USBFS HID 摇杆鼠标 |

---

## 两端怎么配合

- 设备是 **USB Host**，手机是 **AOA Accessory**（插上就能用，手机不用装驱动）
- MCU 只负责"识别出哪个手势"，然后发一个**单字节命令**（`0x02`~`0x0D`）
- Android 侧负责"这个命令在当前 App 里对应什么操作"

协议细节与数据流见 `ARCHITECTURE.md` 和 `docs/design/`。
