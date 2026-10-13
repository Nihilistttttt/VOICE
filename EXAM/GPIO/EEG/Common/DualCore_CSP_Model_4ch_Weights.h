#ifndef __DUALCORE_CSP_MODEL_4CH_WEIGHTS_H
#define __DUALCORE_CSP_MODEL_4CH_WEIGHTS_H

/*
 * CSP占位权重。
 * 运行 python_csp/train_csp_lr.py 后，本文件会被自动备份并覆盖。
 * 通道顺序：CP3、CP4、C3、C4；固件标签：LEFT=2、RIGHT=3。
 * CSP4CH_WEIGHTS_TRAINED=0 时，V5F自动回退到原24维FFT+SVM算法。
 */
#define CSP4CH_INPUT_CH          4
#define CSP4CH_FEATURE_DIM       4
#define CSP4CH_WEIGHTS_TRAINED   0

static const float csp4ch_matrix[4][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};

static const float csp4ch_mean[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static const float csp4ch_scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static const float csp4ch_svm_weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static const float csp4ch_svm_bias = 0.0f;

#endif /* __DUALCORE_CSP_MODEL_4CH_WEIGHTS_H */
