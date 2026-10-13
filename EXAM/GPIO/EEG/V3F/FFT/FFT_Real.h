#ifndef FFT_REAL_H
#define FFT_REAL_H

#include "FFT.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FFT_RealInternalState *FFT_RealConfigHandle;

FFT_RealConfigHandle FFT_Real_Alloc(int FFT_N, FFT_Direction Inverse_FFT, void *Mem, size_t *MemLen);
void FFT_Real_Transform(FFT_RealConfigHandle Config, const FFT_Scalar *TimeData, FFT_Complex_t *FreqData);
void FFT_Real_Inverse(FFT_RealConfigHandle Config, const FFT_Complex_t *FreqData, FFT_Scalar *TimeData);

#ifdef __cplusplus
}
#endif
#endif
