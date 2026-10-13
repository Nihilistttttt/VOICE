#include "Direction_Classifier.h"
#include "Direction_Model_4ch_Weights.h"
#include <math.h>

static float dir_relu(float x)
{
    return (x > 0.0f) ? x : 0.0f;
}

static float dir_sigmoid(float x)
{
    if (x > 30.0f) return 1.0f;
    if (x < -30.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-x));
}

static float dir_std_feature(const float feature[DIR_CLASSIFIER_INPUT_DIM], int i)
{
    float scale = dir4ch_scale[i];
    if (scale < 1.0e-6f && scale > -1.0e-6f) {
        scale = 1.0f;
    }
    return (feature[i] - dir4ch_mean[i]) / scale;
}

static float dir_linear_raw_score(const float feature[DIR_CLASSIFIER_INPUT_DIM],
                                  const float *w,
                                  float b)
{
    float s = b;
    for (int i = 0; i < DIR_CLASSIFIER_INPUT_DIM; i++) {
        s += w[i] * dir_std_feature(feature, i);
    }
    return s;
}

static float dir_svm_right_probability(const float feature[DIR_CLASSIFIER_INPUT_DIM])
{
    float margin = dir_linear_raw_score(feature, dir4ch_svm_weight, dir4ch_svm_bias);
    return dir_sigmoid(margin * DIR_CLASSIFIER_LINEAR_SCORE_GAIN);
}

static float dir_lda_right_probability(const float feature[DIR_CLASSIFIER_INPUT_DIM]) __attribute__((unused));
static float dir_lda_right_probability(const float feature[DIR_CLASSIFIER_INPUT_DIM])
{
    float margin = dir_linear_raw_score(feature, dir4ch_lda_weight, dir4ch_lda_bias);
    return dir_sigmoid(margin * DIR_CLASSIFIER_LINEAR_SCORE_GAIN);
}

static float dir_mlp_right_probability(const float feature[DIR_CLASSIFIER_INPUT_DIM]) __attribute__((unused));
static float dir_mlp_right_probability(const float feature[DIR_CLASSIFIER_INPUT_DIM])
{
    float h[DIR4CH_MLP_H1_DIM];
    float z;

    for (int j = 0; j < DIR4CH_MLP_H1_DIM; j++) {
        float s = dir4ch_mlp_b0[j];
        for (int i = 0; i < DIR_CLASSIFIER_INPUT_DIM; i++) {
            s += dir_std_feature(feature, i) * dir4ch_mlp_w0[i * DIR4CH_MLP_H1_DIM + j];
        }
        h[j] = dir_relu(s);
    }

    z = dir4ch_mlp_b1[0];
    for (int j = 0; j < DIR4CH_MLP_H1_DIM; j++) {
        z += h[j] * dir4ch_mlp_w1[j];
    }
    return dir_sigmoid(z);
}

DirectionClassifierIntent_t Direction_Classifier_Predict(const float feature[DIR_CLASSIFIER_INPUT_DIM],
                                                          DirectionClassifierResult_t *result)
{
    float p_right;
    int32_t sr;
    int32_t sl;
    int32_t conf;
    DirectionClassifierIntent_t intent;

#if DIR_CLASSIFIER_MODEL_TYPE == DIR_CLASSIFIER_MODEL_LDA
    p_right = dir_lda_right_probability(feature);
#elif DIR_CLASSIFIER_MODEL_TYPE == DIR_CLASSIFIER_MODEL_MLP
    p_right = dir_mlp_right_probability(feature);
#else
    p_right = dir_svm_right_probability(feature);
#endif

    sr = (int32_t)(p_right * 10000.0f + 0.5f);
    if (sr < 0) sr = 0;
    if (sr > 10000) sr = 10000;
    sl = 10000 - sr;
    conf = sl - sr;
    if (conf < 0) conf = -conf;

    if (p_right >= 0.5f) {
        intent = DIR_CLS_RIGHT;
    } else {
        intent = DIR_CLS_LEFT;
    }

#if DIR_CLASSIFIER_ENABLE_UNKNOWN
    if (conf < DIR_CLASSIFIER_CONF_THRESHOLD) {
        intent = DIR_CLS_UNKNOWN;
    }
#endif

    if (result != 0) {
        result->intent = intent;
        result->score_left = sl;
        result->score_right = sr;
        result->confidence = conf;
        result->trained = Direction_Classifier_IsTrained();
    }
    return intent;
}

const char *Direction_Classifier_ToString(DirectionClassifierIntent_t intent)
{
    switch (intent) {
    case DIR_CLS_LEFT: return "LEFT";
    case DIR_CLS_RIGHT: return "RIGHT";
    default: return "UNKNOWN";
    }
}

const char *Direction_Classifier_ModelName(void)
{
#if DIR_CLASSIFIER_MODEL_TYPE == DIR_CLASSIFIER_MODEL_LDA
    return "LDA";
#elif DIR_CLASSIFIER_MODEL_TYPE == DIR_CLASSIFIER_MODEL_MLP
    return "MLP16";
#else
    return "LINEAR_SVM";
#endif
}

uint8_t Direction_Classifier_IsTrained(void)
{
#ifdef DIR4CH_WEIGHTS_TRAINED
    return DIR4CH_WEIGHTS_TRAINED ? 1 : 0;
#else
    return 1;
#endif
}
