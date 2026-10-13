PyCharm CSP 训练流程
====================

1. 固件端切到 CSP 采集模式
   文件：V3F/Hardware/Signal_Analysis.c
   默认已经是：
       #define EEG_APP_MODE EEG_APP_MODE_COLLECT_CSP

2. 编译下载
   Clean V3F -> Build V3F -> Clean V5F -> Build V5F -> 下载 V5F

3. 串口保存数据
   DEBUG 串口：2000000 波特率。
   你可以用任意串口助手保存数据。
   只要把保存出来的 .txt / .log / .csv 文件放到：
       python_csp/data/
   训练脚本会自动读取 data 文件夹里的所有数据文件，不需要手动填写文件名。

   可选：如果你想用 PyCharm 直接采串口，也可以运行：
       python serial_capture_csp.py --port COM6
   注意把 COM6 改成自己的串口号。

4. 采集建议
   每个人单独采。至少 LEFT/RIGHT 各 60 个 CSP_BEGIN 窗口，更推荐各 100 个以上。
   固件会自动执行：REST 5秒 -> LEFT -> REST 5秒 -> RIGHT 循环。
   TASK=LEFT 时轻捏/想象左手，TASK=RIGHT 时轻捏/想象右手。

5. 在 PyCharm 安装依赖
       pip install -r requirements.txt

6. 训练并导出头文件
   直接运行：
       python train_csp_lr.py

   默认会自动读取：
       python_csp/data/
   里面所有 .txt / .log / .csv 文件。

   训练完成后会自动覆盖：
       ../Common/DualCore_CSP_Model_4ch_Weights.h

   如果你只想指定某一个文件，也仍然可以用：
       python train_csp_lr.py --log data/你的文件名.txt

7. 切回推理模式
   文件：V3F/Hardware/Signal_Analysis.c
       #define EEG_APP_MODE EEG_APP_MODE_INFER
   重新 Clean/Build V3F 和 V5F，再下载 V5F。

8. 推理输出
   CSP4CH_WEIGHTS_TRAINED=1 后，V5F 会自动优先使用 CSP+SVM。
   如果 header 还是占位训练状态 0，则自动回退到之前的 24维 FFT+SVM 算法。
