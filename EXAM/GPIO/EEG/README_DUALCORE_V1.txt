DUALCORE_V1 修改说明

目标：先跑通“你自己的板子 + 双核启动 + 原 EEG 功能保留”。

本版不拆分 EEG 算法，不改变 ADS1299、SPI、DMA、DRDY、ring_buffer、滤波、FFT、注意力/左右二分类等逻辑。

运行结构：
1. V3F 上电后初始化系统、串口，并唤醒 V5F。
2. V5F 启动后释放 HSEM，打印一次启动信息，然后空转。
3. V3F 收到 HSEM 唤醒后继续进入 Hardware()。
4. 原来的 EEG 主程序 Signal_Analysis_Start() 仍然完整运行在 V3F。

改动文件：
1. Common/hardware.c
   - 将 OLED、ADS1299、Serial、MPU6050、ICM42605、signal_analysis 等 V3F 外设/算法头文件包在 #if defined(Core_V3F) 内。
   - Hardware() 内部按 Core_V3F / Core_V5F 分支编译。
   - V5F 不初始化任何 EEG 外设，避免和 V3F 抢 SPI、DMA、OLED、串口数据流。

2. V3F/User/main.c
   - 双核启动成功后增加打印：EEG run on V3F, V5F idle。
   - 之后仍调用 Hardware()，进入原 EEG 功能。

3. V5F/User/main.c
   - V5F 释放 HSEM 后增加打印：V5F idle, HSEM released。
   - 双核模式下不调用 Hardware()，只空转。

使用步骤：
1. 在 MounRiver 中导入 V3F 和 V5F 两个工程。
2. 先编译 GPIO_Toggle_V3F。
3. 再编译 GPIO_Toggle_V5F。
4. 最后选中 V5F 工程下载合并镜像。
5. 串口先用 115200 看双核启动打印；进入 EEG 后按你原程序的 DEBUG 串口波特率查看数据。

预期启动打印：
V3F SystemCoreClk:...
V5F SystemCoreClk:...
V5F idle, HSEM released
V3F wake up
EEG run on V3F, V5F idle
随后进入你原来的 EEG 输出。
