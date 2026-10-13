/* Signal_Analysis.c 接入示例：当前10维 DIRCSV 工程 */

/* 1. 在文件顶部增加： */
#include "Direction_Classifier.h"

/* 2. 初始化阶段增加一次： */
Direction_Classifier_Reset();

/* 3. 在你已经算出 10 维特征，并且原来准备打印 DIRCSV 的位置附近加入： */

/* 假设当前 10维特征是 int32_t dir_feat[10] 或 float dir_feat[10] */
float feat10[10];
for (uint8_t i = 0; i < 10; i++) {
    feat10[i] = (float)dir_feat[i];
}

/* 采集模式：保留原来的 DIRCSV 输出 */
#if DIR_FEATURE_COLLECT_MODE
Serial_Printf(DIR_TEXT_PORT,
              "DIRCSV,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
              current_label,
              (int)dir_feat[0], (int)dir_feat[1], (int)dir_feat[2],
              (int)dir_feat[3], (int)dir_feat[4], (int)dir_feat[5],
              (int)dir_feat[6], (int)dir_feat[7], (int)dir_feat[8],
              (int)dir_feat[9]);
#endif

/* 测试模式：滚动6行平均后，每0.5秒输出一次 RESULT */
#if DIR_INFER_1S_MODE
Direction_Result_t result;
if (Direction_Classifier_PushFeature(feat10, &result)) {
    Serial_Printf(DIR_TEXT_PORT,
                  "RESULT,window=%lu,win_rows=%d,INTENT=%s,S_LEFT=%ld,S_RIGHT=%ld,CONF=%ld,trained=%d\r\n",
                  result.window_index,
                  DIR_DECISION_ROWS,
                  result.intent_name,
                  result.score_left,
                  result.score_right,
                  result.confidence,
                  result.trained);
}
#endif

/*
 * 注意：
 * 1. 不要把这段放进 DRDY 中断或 DMA 中断。
 * 2. 放在 FFT 已完成、10维特征已生成之后。
 * 3. ADS1299.c 不需要改。
 * 4. 当前权重文件是 10维版本，不能和24维四通道模型混用。
 */
