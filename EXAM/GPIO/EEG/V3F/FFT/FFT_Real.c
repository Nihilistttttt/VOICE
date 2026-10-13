#include "FFT_Real.h"
#include "FFT_Guts.h"

struct FFT_RealInternalState {
    FFT_ConfigHandle SubConfig;
    FFT_Complex_t *TmpBuffer;
    FFT_Complex_t *SuperTwiddles;
};

FFT_RealConfigHandle FFT_Real_Alloc(int FFT_N, FFT_Direction Inverse_FFT, void *Mem, size_t *MemLen){
    int i;
    FFT_RealConfigHandle Config = NULL;
    size_t subSize = 0, MemNeeded;

    if (FFT_N & 1) return NULL;
    FFT_N >>= 1;

    FFT_Alloc(FFT_N, Inverse_FFT, NULL, &subSize);
    MemNeeded = sizeof(struct FFT_RealInternalState) + subSize + sizeof(FFT_Complex_t) * (FFT_N * 3 / 2);

    if (MemLen == NULL) {
        Config = (FFT_RealConfigHandle) AllocateMemory(MemNeeded);
    } else {
        if (*MemLen >= MemNeeded)
            Config = (FFT_RealConfigHandle) Mem;
        *MemLen = MemNeeded;
    }
    if (!Config) return NULL;

    Config->SubConfig = (FFT_ConfigHandle)(Config + 1);
    Config->TmpBuffer = (FFT_Complex_t *)((char *)Config->SubConfig + subSize);
    Config->SuperTwiddles = Config->TmpBuffer + FFT_N;
    FFT_Alloc(FFT_N, Inverse_FFT, Config->SubConfig, &subSize);

    for (i = 0; i < FFT_N/2; ++i) {
        double phase = -3.14159265358979323846 * ((double)(i+1)/FFT_N + .5);
        if (Inverse_FFT) phase *= -1.0;
        SetTwiddle(Config->SuperTwiddles + i, phase);
    }
    return Config;
}

void FFT_Real_Transform(FFT_RealConfigHandle Config, const FFT_Scalar *TimeData, FFT_Complex_t *FreqData)
{
    int k, ncfft;
    FFT_Complex_t fpnk, fpk, f1k, f2k, tw, tdc;

    if (Config->SubConfig->Inverse) return;
    ncfft = Config->SubConfig->FFT_N;

    FFT_Transform(Config->SubConfig, (const FFT_Complex_t*)TimeData, Config->TmpBuffer);

    tdc.r = Config->TmpBuffer[0].r;
    tdc.i = Config->TmpBuffer[0].i;
    FreqData[0].r = tdc.r + tdc.i;
    FreqData[ncfft].r = tdc.r - tdc.i;
    FreqData[ncfft].i = FreqData[0].i = 0;

    for (k = 1; k <= ncfft/2; ++k) {
        fpk    = Config->TmpBuffer[k];
        fpnk.r =  Config->TmpBuffer[ncfft - k].r;
        fpnk.i = -Config->TmpBuffer[ncfft - k].i;
        C_ADD(f1k, fpk, fpnk);
        C_SUB(f2k, fpk, fpnk);
        C_MUL(tw , f2k, Config->SuperTwiddles[k-1]);
        FreqData[k].r = HALF_OF(f1k.r + tw.r);
        FreqData[k].i = HALF_OF(f1k.i + tw.i);
        FreqData[ncfft - k].r = HALF_OF(f1k.r - tw.r);
        FreqData[ncfft - k].i = HALF_OF(tw.i - f1k.i);
    }
}

void FFT_Real_Inverse(FFT_RealConfigHandle Config, const FFT_Complex_t *FreqData, FFT_Scalar *TimeData)
{
    int k, ncfft;
    if (Config->SubConfig->Inverse == 0) return;
    ncfft = Config->SubConfig->FFT_N;

    Config->TmpBuffer[0].r = FreqData[0].r + FreqData[ncfft].r;
    Config->TmpBuffer[0].i = FreqData[0].r - FreqData[ncfft].r;

    for (k = 1; k <= ncfft/2; ++k) {
        FFT_Complex_t fk, fnkc, fek, fok, tmp;
        fk = FreqData[k];
        fnkc.r = FreqData[ncfft - k].r;
        fnkc.i = -FreqData[ncfft - k].i;
        C_ADD(fek, fk, fnkc);
        C_SUB(tmp, fk, fnkc);
        C_MUL(fok, tmp, Config->SuperTwiddles[k-1]);
        C_ADD(Config->TmpBuffer[k], fek, fok);
        C_SUB(Config->TmpBuffer[ncfft - k], fek, fok);
        Config->TmpBuffer[ncfft - k].i *= -1;
    }
    FFT_Transform(Config->SubConfig, Config->TmpBuffer, (FFT_Complex_t *)TimeData);
}

