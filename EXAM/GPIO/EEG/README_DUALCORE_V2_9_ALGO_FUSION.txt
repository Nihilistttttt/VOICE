双核 EEG 工程 v2.9：旧算法保留 + 推理稳定融合版

本版目标：不删除原 Signal_Analysis.c 里的采集、滤波、FFT、注意力、DIRCSV、上位机波形、ICM42605/OLED 等逻辑，只在 V5F 推理结果层增加在线稳定策略。

相对 v2.8 的变化：
1. V5F 仍使用原来的 24 维 FFT 特征 + 4 通道 Linear SVM 权重。
2. 新增 4 窗口滚动平均 + EMA 平滑。
3. 新增低置信度防抖：当左右概率接近 50% 时，不立即反复切换 LEFT/RIGHT。
4. DEBUG 推理输出格式仍保持简洁：Current / Left / Right。
5. 采集模式不变：LEFT/REST 5s/RIGHT/REST 5s 循环输出 DIRCSV。

调参位置：Common/dualcore_ipc.c
- DUALCORE_V5F_FUSION_ROWS：参与投票的窗口数，默认 4。
- DUALCORE_V5F_FUSION_EMA_ALPHA：EMA 平滑强度，默认 0.60。
- DUALCORE_V5F_KEEP_LAST_CONF：低置信度防抖阈值，默认 650，即 6.5%。

注意：真正 CSP 需要原始 4 通道时域 trial 数据训练 CSP 矩阵，不能只靠现有 DIRCSV 文件生成。本版先把不破坏旧算法、能直接上板测试的在线稳定策略并入 V5F。后续如果要做真正 CSP，需要增加 raw-window 采集格式和 Python CSP 训练导出。
