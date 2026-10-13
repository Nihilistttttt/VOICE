#include "Direction_Model.h"
#include "Direction_Classifier.h"

static Direction_t convert_intent(DirectionClassifierIntent_t intent)
{
    switch (intent) {
    case DIR_CLS_LEFT:
        return DIRECTION_LEFT;
    case DIR_CLS_RIGHT:
        return DIRECTION_RIGHT;
    default:
        return DIRECTION_UNKNOWN;
    }
}

Direction_t Direction_Model_Infer(const float feature[24], int32_t score[DIR_CLASS_NUM])
{
    DirectionClassifierResult_t result;
    DirectionClassifierIntent_t intent = Direction_Classifier_Predict(feature, &result);

    if (score != 0) {
        score[0] = result.score_left;
        score[1] = result.score_right;
    }
    return convert_intent(intent);
}

const char *Direction_ToString(Direction_t dir)
{
    switch (dir) {
    case DIRECTION_LEFT:
        return "LEFT";
    case DIRECTION_RIGHT:
        return "RIGHT";
    default:
        return "UNKNOWN";
    }
}

uint8_t Direction_Model_IsTrained(void)
{
    return Direction_Classifier_IsTrained();
}

const char *Direction_Model_Name(void)
{
    return Direction_Classifier_ModelName();
}
