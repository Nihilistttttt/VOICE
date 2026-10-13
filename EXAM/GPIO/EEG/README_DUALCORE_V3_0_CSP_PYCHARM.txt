v3.0 CSP + PyCharm 训练链路
===========================

本版本保留 v2.9 的所有原有算法链路，同时新增 CSP 数据采集与 CSP+SVM 推理入口。

一、固件模式
------------
文件：V3F/Hardware/Signal_Analysis.c

1) CSP 采集模式（默认）
   #define EEG_APP_MODE EEG_APP_MODE_COLLECT_CSP

   DEBUG 串口输出：CSP_BEGIN / CSP / CSP_END
   用于 PyCharm 中 python_csp/train_csp_lr.py 训练 CSP+SVM。

2) 旧 DIRCSV 采集模式
   #define EEG_APP_MODE EEG_APP_MODE_COLLECT

   DEBUG 串口输出：TASK / DIRCSV
   保留原来的 24 维 FFT 特征采集，不丢失旧算法。

3) 推理模式
   #define EEG_APP_MODE EEG_APP_MODE_INFER

   DEBUG 串口输出：
   --------------------------------
   Current : LEFT / RIGHT / UNKNOWN
   Left    : xx.xx %
   Right   : xx.xx %
   --------------------------------

二、CSP训练流程
--------------
1. 用默认 COLLECT_CSP 模式下载固件。
2. DEBUG 串口 2000000 波特率保存输出为：python_csp/data/csp_log.txt。
3. PyCharm 打开 python_csp 文件夹。
4. 安装依赖：pip install -r requirements.txt
5. 运行：python train_csp_lr.py --log data/csp_log.txt
6. 脚本会自动生成/覆盖：Common/DualCore_CSP_Model_4ch_Weights.h
7. 把固件模式改为 EEG_APP_MODE_INFER。
8. Clean V3F -> Build V3F -> Clean V5F -> Build V5F -> 下载 V5F。

三、推理选择
------------
V5F 端会自动判断：
- 如果 Common/DualCore_CSP_Model_4ch_Weights.h 中 CSP4CH_WEIGHTS_TRAINED=1，则优先使用 CSP+SVM。
- 如果还是占位权重 CSP4CH_WEIGHTS_TRAINED=0，则自动回退到原来的 24 维 FFT+SVM。

这样不会破坏原来的 Signal_Analysis.c / Attention / FFT / DIRCSV 算法，只是在已有链路上增加 CSP 路线。
