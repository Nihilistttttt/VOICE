#ifndef FFT_H
#define FFT_H

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "malloc.h"            // 引入自定义内存管理

#ifdef __cplusplus
extern "C" {
#endif

/* 不使用 SIMD，固定为标量浮点 */
#undef USE_SIMD
#define FFT_Scalar float
typedef enum {
    FFT_FORWARD = 0,
    FFT_INVERSE = 1
} FFT_Direction;

typedef struct {
    FFT_Scalar r;
    FFT_Scalar i;
} FFT_Complex_t;

typedef struct FFT_InternalState *FFT_ConfigHandle;

/* ---------- API ---------- */
FFT_ConfigHandle FFT_Alloc(int FFT_N, FFT_Direction Inverse_FFT, void *mem, size_t *memLen);

void FFT_Transform(FFT_ConfigHandle cfg, const FFT_Complex_t *fin, FFT_Complex_t *outputData);

void FFT_Transform_Stride(FFT_ConfigHandle cfg, const FFT_Complex_t *fin, FFT_Complex_t *outputData, int fin_stride);

void FFT_Cleanup(void);

int FFT_Next_Fast_Size(int n);

#ifdef __cplusplus
}
#endif

#endif
