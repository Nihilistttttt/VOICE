# CLion 开发 CH32V307 —— 环境配置指南（替代 MounRiver）

> 目标: 用 CLion 2024.3.2 完成 VOICE_V307 的 **编译 / 烧录 / 断点调试**,
> 效果对齐 MounRiver Studio 2。CMake 工程已生成并验证(产物与 MounRiver
> 差 0.3%, 见下)。

---

## 〇、已就绪的东西(本仓库已包含, 无需重做)

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 构建定义: 60 个源文件/10 组头文件/编译链接选项, 与 MounRiver 一致; POST_BUILD 自动产出 `VOICE.hex`/`VOICE.bin` |
| `cmake/riscv307.cmake` | 交叉工具链文件: 指向 MounRiver 自带的 `riscv-none-embed-gcc 8.2.0` |
| `cmake-build-clion/` | 已验证的构建输出(VOICE.elf/VOICE.hex), 可删可重建 |

工具链路径(MounRiver 自带, 已验证):
```
GCC/GDB/ObjCopy: D:/InstallData/EmbTools/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC/bin/
OpenOCD(WCH 定制, 支持 LinkE): D:/InstallData/EmbTools/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/OpenOCD/OpenOCD/bin/openocd.exe
烧录配置: VOICE_V307/flash_v307.cfg
```

---

## 一、CLion 打开工程

1. `File → Open` → 选 `D:\Libraries\Projects\VOICE\VOICE_V307` 目录
2. CLion 检测到 `CMakeLists.txt` → 选 **Trust Project / 作为 CMake 项目打开**
3. CLion 会自动用自带的 Ninja + CMake 配置; 交叉编译器由
   `cmake/riscv307.cmake` 指定(CMake Profile 里已有
   `CMAKE_TOOLCHAIN_FILE=cmake/riscv307.cmake` 则全自动, 否则手动填:
   Settings → Build → CMake → CMake options 追加
   `-DCMAKE_TOOLCHAIN_FILE=cmake/riscv307.cmake`)
4. 配置完成后 Build(锤子图标) → 产物在 `cmake-build-clion/`:
   `VOICE.elf / VOICE.hex / VOICE.bin / VOICE.map`

> 代码索引: CLion 用同一交叉编译器做代码分析 —— 跳转/补全/重构全功能
> 可用(依赖解析来自 CMake, 不需要 compile_commands.json)。

---

## 二、烧录(OpenOCD 运行配置)

`Run → Edit Configurations → + → OpenOCD Download & Run`:

| 字段 | 值 |
|---|---|
| **Executable** | `D:/InstallData/EmbTools/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/OpenOCD/OpenOCD/bin/openocd.exe` |
| **Board config file** | `D:/Libraries/Projects/VOICE/VOICE_V307/flash_v307.cfg` |
| **GDB port** | 3333 |
| **Telnet port** | 4444 |
| **Symbol file** | `D:/Libraries/Projects/VOICE/VOICE_V307/cmake-build-clion/VOICE.elf` |
| **Download** | [x] All algorithms(或 Algorithm selection 默认) |
| **Before launch** | 加 Build(确保最新产物) |

点 Run ▶ = 烧录 + 复位运行(等价 MounRiver 的 Download & Run)。

⚠️ **烧录前**: LinkE 调试器 USB 必须插好、SW3/CTRL 开关在调试挡;
否则报 `WLink Open Error`。

---

## 三、断点调试(等价 MounRiver 调试)

同一 OpenOCD 配置里:
- **Debug port(GDB)**: MounRiver 自带的
  `.../RISC-V Embedded GCC/bin/riscv-none-embed-gdb.exe`
- 点 Debug 🐞 = 烧录 + 挂 GDB + 停在 main —— 断点/单步/变量/调用栈/
  外设寄存器(Peripherals 窗口)全部可用。

> 注意: `Hal_GPIO_Init`/`EspLink_Init` 一类初始化函数上设断点即可验证
> 启动流程; 优化等级 -Os 下偶有行序漂移属正常。

---

## 四、日常开发流(替代 MounRiver 的完整工作流)

```
改代码 → Build(Shift+F9) → Run ▶(烧录+复位) → App/串口验证
调试 → Debug 🐞(烧录+断点) → 单步观察
```

命令行等价(CI/脚本用):
```bash
cd VOICE_V307
cmake -G Ninja -B cmake-build-clion -S . \
  -DCMAKE_TOOLCHAIN_FILE=cmake/riscv307.cmake \
  -DCMAKE_MAKE_PROGRAM="D:/InstallData/ProgramTools/CLion 2024.3.2/bin/ninja/win/x64/ninja.exe"
cmake --build cmake-build-clion
# 烧录:
openocd.exe -f flash_v307.cfg -c "program cmake-build-clion/VOICE.hex verify; init; reset run; shutdown"
```

---

## 五、已验证的等价性(2026-10-22)

| | MounRiver (obj/) | CLion (cmake-build-clion/) |
|---|---|---|
| .text | 42,476 B | 42,356 B(差 0.3%, 文件集/探测差异正常) |
| .data | 848 B | 848 B |
| HEX | 121,878 B | 121,547 B |
| 烧录 | ✅ | ✅ 同一 OpenOCD + flash_v307.cfg |

两套构建可并存: MounRiver 用 `obj/`, CLion 用 `cmake-build-clion/`,
互不干扰。**注意切换构建系统后烧录的固件是最后构建的那份**。

---

## 六、已知差异与注意

1. **文件集**: CMake 用 GLOB 递归收集(新增 .c 自动纳入), MounRiver 需改
   subdir.mk —— CLion 侧新增文件**零配置**。
2. **TEST_MODE 门控**: 与 MounRiver 相同(源码 #if 决定), 切 TEST_MODE 后
   两个构建系统都要重新 Build。
3. **`-march=rv32imacxw`** 的 `xw` 是 WCH 扩展(硬件压栈/中断), 只在
   WCH 工具链有效 —— 不要换芯片厂商工具链。
4. **调试器驱动**: LinkE 需 MounRiver 的 WCH 定制 OpenOCD(标准开源
   OpenOCD 无 wch_riscv 驱动) —— CLion 里 Executable 必须指向 MounRiver 的。
