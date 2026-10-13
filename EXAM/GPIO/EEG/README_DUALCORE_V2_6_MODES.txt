braintest2_dualcore_v2_6_modes

本版目标：把串口输出明确分成“采集模式”和“推理模式”，避免之前 DEBUG 串口同时打印 DIRCSV、V5F_DIRCSV、IPC 状态和推理结果。

模式切换位置：
  V3F/Hardware/Signal_Analysis.c

只改这一行：
  #define EEG_APP_MODE                EEG_APP_MODE_COLLECT

可选值：
  EEG_APP_MODE_COLLECT  采集模式
  EEG_APP_MODE_INFER    推理模式

1) 采集模式 COLLECT
  - DEBUG 串口输出：EEG_MODE,COLLECT / TASK / DIRCSV
  - 左右任务自动交替：LEFT -> REST -> RIGHT -> REST -> LEFT ...
  - LEFT 与 RIGHT 中间 REST 间隔约 5 秒
  - 每个 LEFT/RIGHT 采集阶段约 10 秒，其中刚切换后的 8 行特征丢弃，不写 DIRCSV
  - 不打印 V5F_DIRCSV、V5F_RESULT、IPC_V5F_INF 等调试信息
  - WIFI 串口上位机波形仍保留
  - SPI OLED 仍显示 ICM42605 姿态数据

2) 推理模式 INFER
  - DEBUG 串口输出：EEG_MODE,INFER / V5F_RESULT_HEADER / V5F_RESULT
  - 不输出 DIRCSV
  - 不输出 V5F_DIRCSV
  - 不输出 IPC 细节
  - V5F_RESULT 每约 2 秒输出一次 LEFT/RIGHT 判断
  - V3F 负责 ADS1299 + ICM42605 + OLED + 上位机波形
  - V5F 负责 EEG 共享 ring 取帧、预处理、FFT、24维特征、LEFT/RIGHT 推理

默认当前设置：EEG_APP_MODE_COLLECT。
如需演示实时判断，把 EEG_APP_MODE 改为 EEG_APP_MODE_INFER 后重新编译 V3F、V5F，并下载 V5F 合并镜像。

编译下载顺序：
  1. Clean V3F
  2. Build V3F
  3. Clean V5F
  4. Build V5F
  5. 选中 V5F 下载
