本文件夹用于第3步：单片机加载训练权重后实时测试。

使用方法：
1. 先完成第1步采集和第2步 PyCharm 训练。
2. 将 PyCharm 生成的 models/Direction_Model_4ch_Weights.h 复制到本文件夹，覆盖旧文件。
3. 将本文件夹中的 Signal_Analysis.c、Direction_Classifier.c/h、Direction_Model.c/h、Direction_Model_4ch_Weights.h 加入/覆盖 MounRiver 工程。
4. 此模式下宏配置为：
   DIR_FEATURE_COLLECT_MODE = 0
   DIR_INFER_1S_MODE = 1
   DIR_DECISION_ROWS = 4
   DIR_RESULT_EVERY_ROWS = 1
5. 烧录后串口输出 RESULT，不再输出训练用 DIRCSV。

输出示例：
RESULT,window=0,dt_ms=500,win_rows=4,INTENT=LEFT,S_LEFT=6123,S_RIGHT=3877,CONF=2246,trained=1

含义：
每0.5秒刷新一次结果，但每次使用最近4行特征平均，约2秒判断窗口。
默认模型是 Linear SVM；如需切换 LDA/MLP，修改 Direction_Classifier.h 中的 DIR_CLASSIFIER_MODEL_TYPE。
