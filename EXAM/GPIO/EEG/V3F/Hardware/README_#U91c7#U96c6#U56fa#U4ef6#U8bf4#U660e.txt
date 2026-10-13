本文件夹用于第1步：单片机采集训练数据。

使用方法：
1. 将本文件夹中的 ADS1299.c/h、Signal_Analysis.c/h 覆盖/加入 MounRiver 工程。
2. 此模式下宏配置为：
   DIR_FEATURE_COLLECT_MODE = 1
   DIR_INFER_1S_MODE = 0
3. 烧录后串口输出 DIRCSV，每0.5秒一行24维特征。
4. 用串口助手保存数据，不需要 Python 直接打开串口。
5. 分别保存为：
   pycharm_train_from_mcu/data/left.txt
   pycharm_train_from_mcu/data/right.txt

注意：ADS1299 的 SPI+DMA、环形缓冲区、中断采集逻辑未改变。
