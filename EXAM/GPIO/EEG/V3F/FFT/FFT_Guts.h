#ifndef FFT_GUTS_H
#define FFT_GUTS_H
#include <limits.h>
#include "FFT.h"
#define MAX_FACTORS 32
struct FFT_InternalState {
    int FFT_N;
    FFT_Direction Inverse;
    int Factors[2 * MAX_FACTORS];
    FFT_Complex_t Twiddles[1];
};
# define S_MUL(a, b)        ( (a)*(b) )
# define C_MUL(m, a, b) \
    do { (m).r = (a).r*(b).r - (a).i*(b).i; \
         (m).i = (a).r*(b).i + (a).i*(b).r; } while(0)
# define C_MUL_BY_SCALAR(c, s) \
    do { (c).r *= (s); (c).i *= (s); } while(0)

#define C_ADD(res, a, b) \
    do { (res).r = (a).r + (b).r; (res).i = (a).i + (b).i; } while(0)
#define C_SUB(res, a, b) \
    do { (res).r = (a).r - (b).r; (res).i = (a).i - (b).i; } while(0)
#define C_ADD_TO(res, a) \
    do { (res).r += (a).r; (res).i += (a).i; } while(0)
#define C_SUB_FROM(res, a) \
    do { (res).r -= (a).r; (res).i -= (a).i; } while(0)

#define FFT_COS(phase) (FFT_Scalar) cos(phase)
#define FFT_SIN(phase) (FFT_Scalar) sin(phase)
#define HALF_OF(x)          ((x) * ((FFT_Scalar)0.5))

#define SetTwiddle(x, phase) \
    do { (x)->r = FFT_COS(phase); (x)->i = FFT_SIN(phase); } while(0)


#endif
