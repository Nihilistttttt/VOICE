// 包含 FFT 内部头文件，其中定义了复数类型、旋转因子宏和内部状态结构体
#include "FFT_Guts.h"

/**
 * 基-2 蝶形运算（Radix-2 Butterfly）
 * 用于 FFT 长度因子 p=2 的情况。
 *
 * @param OutputData  输入/输出复数数组指针，指向当前蝶形组起始位置
 * @param FFTStride   旋转因子索引步长（即 twiddle 因子在表中的间隔）
 * @param Config      FFT 配置句柄，包含旋转因子表和变换方向等信息
 * @param M           当前子变换的长度，也是每组蝶形的个数（循环次数）
 *
 * 算法原理：
 *   对于每一对点 (a, b) 和旋转因子 w，执行：
 *     a' = a + b*w
 *     b' = a - b*w
 *   此处额外进行了除以 2 的缩放（C_FIX_DIV），防止定点溢出，浮点模式下为空操作。
 */
static void Butterfly2(FFT_Complex_t *OutputData, const size_t FFTStride,  FFT_ConfigHandle Config, int M)
{
    FFT_Complex_t *OutputData2;          // 指向第二组起始位置（偏移 M）
    FFT_Complex_t *tw1 = Config->Twiddles; // 旋转因子指针，初始指向表头
    FFT_Complex_t Temp;                  // 临时复数变量，保存乘积结果
    OutputData2 = OutputData + M;        // 第二组与第一组相距 M 个点
    do {

        // Temp = OutputData2 * tw1
        C_MUL(Temp, *OutputData2, *tw1);

        tw1 += FFTStride;                // 旋转因子步进，指向下一个需要使用的 twiddle

        // OutputData2 = OutputData - Temp
        C_SUB(*OutputData2, *OutputData, Temp);

        // OutputData = OutputData + Temp
        C_ADD_TO(*OutputData, Temp);

        // 移动指针，处理下一对点
        ++OutputData2;
        ++OutputData;
    } while (--M);                       // 重复 M 次
}

/**
 * 基-4 蝶形运算（Radix-4 Butterfly）
 * 用于 FFT 长度因子 p=4 的情况，一次处理 4 个点，减少运算次数。
 *
 * @param OutputData  输入/输出数组指针，指向当前蝶形组的起始点
 * @param FFTStride   旋转因子步长
 * @param Config      配置句柄
 * @param M           组内子变换长度（每组包含 4 个大小为 M 的子块）
 *
 * 算法原理基于标准基-4 FFT 公式，利用 4 个旋转因子 w1, w2, w3，
 * 对四个点 a0, a1, a2, a3 进行组合。详细公式见代码实现。
 */
static void Butterfly4(FFT_Complex_t *OutputData, const size_t FFTStride,  FFT_ConfigHandle Config, const size_t M)
{
    FFT_Complex_t *tw1,*tw2,*tw3;        // 三个旋转因子指针
    FFT_Complex_t Scratch[6];            // 临时存储中间结果
    size_t k = M;                        // 循环计数器，等于 M
    const size_t m2 = 2*M;               // 偏移 2M
    const size_t m3 = 3*M;               // 偏移 3M
    tw3 = tw2 = tw1 = Config->Twiddles;  // 三个指针均初始指向旋转因子表头
    do {
        // 计算三个乘积：a1*w1, a2*w2, a3*w3
        C_MUL(Scratch[0], OutputData[M], *tw1 );
        C_MUL(Scratch[1], OutputData[m2], *tw2 );
        C_MUL(Scratch[2], OutputData[m3], *tw3 );

        // 基-4 蝶形组合
        C_SUB( Scratch[5], *OutputData, Scratch[1] );
        C_ADD_TO(*OutputData, Scratch[1]);
        C_ADD( Scratch[3], Scratch[0], Scratch[2] );
        C_SUB( Scratch[4], Scratch[0], Scratch[2] );
        C_SUB( OutputData[m2], *OutputData, Scratch[3] );

        // 更新旋转因子指针（每个指针步长不同）
        tw1 += FFTStride;
        tw2 += FFTStride*2;
        tw3 += FFTStride*3;

        C_ADD_TO( *OutputData, Scratch[3] );

        // 根据变换方向（正向/反向）调整符号，得到最终输出
        if(Config->Inverse) {
            OutputData[M].r = Scratch[5].r - Scratch[4].i;
            OutputData[M].i = Scratch[5].i + Scratch[4].r;
            OutputData[m3].r = Scratch[5].r + Scratch[4].i;
            OutputData[m3].i = Scratch[5].i - Scratch[4].r;
        } else {
            OutputData[M].r = Scratch[5].r + Scratch[4].i;
            OutputData[M].i = Scratch[5].i - Scratch[4].r;
            OutputData[m3].r = Scratch[5].r - Scratch[4].i;
            OutputData[m3].i = Scratch[5].i + Scratch[4].r;
        }
        ++OutputData;                    // 处理下一组（移动一个点）
    } while(--k);
}

/**
 * 基-3 蝶形运算（Radix-3 Butterfly）
 * 用于 FFT 长度因子 p=3 的情况。
 *
 * @param OutputData  输入/输出数组指针
 * @param FFTStride   旋转因子步长
 * @param Config      配置句柄
 * @param M           子变换长度
 *
 * 利用一个特殊旋转因子 epi3 = exp(i*2π/3) 或它的共轭，通过组合三个点实现。
 */
static void Butterfly3(FFT_Complex_t *OutputData, const size_t FFTStride,  FFT_ConfigHandle Config, size_t M)
{
    size_t k = M;
    const size_t m2 = 2*M;               // 偏移 2M
    FFT_Complex_t *tw1, *tw2;            // 两个旋转因子指针
    FFT_Complex_t Scratch[5];            // 临时存储区
    // 预取一个特殊的旋转因子，其索引为 FFTStride * M（对应角度 2π/3 或 -2π/3）
    FFT_Complex_t epi3 = Config->Twiddles[FFTStride*M];
    tw1 = tw2 = Config->Twiddles;
    do {
        // 乘以旋转因子
        C_MUL(Scratch[1], OutputData[M], *tw1);
        C_MUL(Scratch[2], OutputData[m2], *tw2);

        // 组合
        C_ADD(Scratch[3], Scratch[1], Scratch[2]);
        C_SUB(Scratch[0], Scratch[1], Scratch[2]);

        tw1 += FFTStride;
        tw2 += FFTStride*2;

        // 计算输出值
        OutputData[M].r = OutputData->r - HALF_OF(Scratch[3].r);
        OutputData[M].i = OutputData->i - HALF_OF(Scratch[3].i);

        // Scratch[0] 乘以 epi3 的虚部（等同于乘以 sqrt(3)/2 并调整符号）
        C_MUL_BY_SCALAR( Scratch[0], epi3.i );

        C_ADD_TO(*OutputData, Scratch[3]);

        OutputData[m2].r = OutputData[M].r + Scratch[0].i;
        OutputData[m2].i = OutputData[M].i - Scratch[0].r;
        OutputData[M].r -= Scratch[0].i;
        OutputData[M].i += Scratch[0].r;

        ++OutputData;
    } while(--k);
}

/**
 * 基-5 蝶形运算（Radix-5 Butterfly）
 * 用于 FFT 长度因子 p=5 的情况。
 * 使用预先计算的旋转因子 ya 和 yb（对应角度 2π/5 和 4π/5），通过一系列组合实现。
 *
 * @param OutputData  输入/输出数组指针
 * @param FFTStride   旋转因子步长
 * @param Config      配置句柄
 * @param M           子变换长度
 */
static void Butterfly5(FFT_Complex_t *OutputData, const size_t FFTStride,  FFT_ConfigHandle Config, int M)
{
    FFT_Complex_t *OutputData0,*OutputData1,*OutputData2,*OutputData3,*OutputData4;
    int u;
    FFT_Complex_t Scratch[13];           // 临时空间
    FFT_Complex_t *Twiddles = Config->Twiddles;
    FFT_Complex_t *tw;
    FFT_Complex_t ya, yb;                // 两个固定的旋转因子

    // 获取预计算的旋转因子（角度 2π/5 和 4π/5）
    ya = Twiddles[FFTStride*M];
    yb = Twiddles[FFTStride*2*M];

    // 初始化五个数据块的指针，每块相隔 M 个点
    OutputData0 = OutputData;
    OutputData1 = OutputData0 + M;
    OutputData2 = OutputData0 + 2*M;
    OutputData3 = OutputData0 + 3*M;
    OutputData4 = OutputData0 + 4*M;

    tw = Config->Twiddles;

    // 对每个 u 进行蝶形（共 M 组）
    for (u=0; u<M; ++u) {
        Scratch[0] = *OutputData0;

        // 乘以旋转因子（twiddle 索引随 u 线性增长）
        C_MUL(Scratch[1], *OutputData1, tw[u*FFTStride]);
        C_MUL(Scratch[2], *OutputData2, tw[2*u*FFTStride]);
        C_MUL(Scratch[3], *OutputData3, tw[3*u*FFTStride]);
        C_MUL(Scratch[4], *OutputData4, tw[4*u*FFTStride]);

        // 组合中间结果
        C_ADD(Scratch[7], Scratch[1], Scratch[4]);
        C_SUB(Scratch[10], Scratch[1], Scratch[4]);
        C_ADD(Scratch[8], Scratch[2], Scratch[3]);
        C_SUB(Scratch[9], Scratch[2], Scratch[3]);

        // 更新 OutputData0 的累加
        OutputData0->r += Scratch[7].r + Scratch[8].r;
        OutputData0->i += Scratch[7].i + Scratch[8].i;

        // 根据公式计算五个输出点
        Scratch[5].r = Scratch[0].r + S_MUL(Scratch[7].r, ya.r) + S_MUL(Scratch[8].r, yb.r);
        Scratch[5].i = Scratch[0].i + S_MUL(Scratch[7].i, ya.r) + S_MUL(Scratch[8].i, yb.r);
        Scratch[6].r =  S_MUL(Scratch[10].i, ya.i) + S_MUL(Scratch[9].i, yb.i);
        Scratch[6].i = -S_MUL(Scratch[10].r, ya.i) - S_MUL(Scratch[9].r, yb.i);

        C_SUB(*OutputData1, Scratch[5], Scratch[6]);
        C_ADD(*OutputData4, Scratch[5], Scratch[6]);

        Scratch[11].r = Scratch[0].r + S_MUL(Scratch[7].r, yb.r) + S_MUL(Scratch[8].r, ya.r);
        Scratch[11].i = Scratch[0].i + S_MUL(Scratch[7].i, yb.r) + S_MUL(Scratch[8].i, ya.r);
        Scratch[12].r = -S_MUL(Scratch[10].i, yb.i) + S_MUL(Scratch[9].i, ya.i);
        Scratch[12].i =  S_MUL(Scratch[10].r, yb.i) - S_MUL(Scratch[9].r, ya.i);

        C_ADD(*OutputData2, Scratch[11], Scratch[12]);
        C_SUB(*OutputData3, Scratch[11], Scratch[12]);

        // 所有指针前进到下一组元素
        ++OutputData0; ++OutputData1; ++OutputData2; ++OutputData3; ++OutputData4;
    }
}

/**
 * 通用蝶形运算（处理任意因子 p，p 不是 2,3,4,5 时使用）
 * 采用标准的 DFT 公式，对每个输出点计算所有输入点的加权和。
 * 由于效率较低，通常只用于很少出现的素数因子。
 *
 * @param OutputData  输入/输出数组
 * @param FFTStride   旋转因子步长
 * @param Config      配置
 * @param M           子变换长度（即每组内每个子块的元素个数）
 * @param P           当前因子（质数或大于 5 的数）
 */
static void ButterflyGeneric(FFT_Complex_t *OutputData, const size_t FFTStride,  FFT_ConfigHandle Config, int M, int P)
{
    int u, k, q1, q;
    FFT_Complex_t *Twiddles = Config->Twiddles;
    FFT_Complex_t Temp;
    int FFT_N_Original = Config->FFT_N;  // 原始 FFT 长度，用于旋转因子索引取模

    // 临时分配 P 个复数空间，用于存储当前组的数据
    FFT_Complex_t *Scratch = (FFT_Complex_t*)AllocateMemory(sizeof(FFT_Complex_t)*P);
    if (Scratch == NULL) return;

    // 外层循环：处理 M 个独立的小组
    for (u=0; u<M; ++u) {
        // 收集 P 个数据点（步长为 M）到 Scratch
        k = u;
        for (q1=0 ; q1<P ; ++q1) {
            Scratch[q1] = OutputData[ k ];
            k += M;
        }

        // 将 Scratch 中的数据加权累加回 OutputData
        k = u;
        for (q1=0 ; q1<P ; ++q1) {
            int Twiddle_Idx = 0;
            OutputData[ k ] = Scratch[0]; // 初始化为第一个值
            for (q=1; q<P; ++q) {
                // 计算旋转因子索引：累加 FFTStride * k，并做模 FFT_N_Original 归约
                Twiddle_Idx += FFTStride * k; // NOLINT(*-narrowing-conversions)
                if (Twiddle_Idx >= FFT_N_Original)
                    Twiddle_Idx -= FFT_N_Original;
                C_MUL(Temp, Scratch[q], Twiddles[Twiddle_Idx]);
                C_ADD_TO( OutputData[ k ], Temp);
            }
            k += M;
        }
    }

    FreeMemory(Scratch);   // 释放临时缓冲区
}

/**
 * FFT 递归工作函数
 * 实现 Cooley-Tukey 算法，将问题分解为较小的子问题，然后合并。
 *
 * @param OutputData  输出数组（计算完成后存放结果）
 * @param InputData   输入数组（原始数据）
 * @param FFTStride   输入数据的步长（用于从 InputData 中读取元素）
 * @param InStride    输入数据在原始序列中的步长（通常为 1）
 * @param Factors     因子分解表指针（当前指向一对 (p, m)）
 * @param Config      配置
 */
static void WorkFFT(FFT_Complex_t *OutputData, const FFT_Complex_t *InputData, const size_t FFTStride, // NOLINT(*-no-recursion)
                    int InStride, int *Factors, FFT_ConfigHandle Config)
{
    FFT_Complex_t *OutputBegin = OutputData;
    const int p = *Factors++;   // 当前因子 p（例如 2,3,4,5 或其它）
    const int m = *Factors++;   // 子变换长度（n/p）
    const FFT_Complex_t *OutputEnd = OutputData + p * m;

    if (m == 1) {
        // 最底层：直接复制输入到输出（按照步长读取）
        do {
            *OutputData = *InputData;
            InputData += FFTStride * InStride;
        } while (++OutputData != OutputEnd);
    } else {
        // 递归处理每个子块
        do {
            WorkFFT(OutputData, InputData, FFTStride * p, InStride, Factors, Config);
            InputData += FFTStride * InStride;
        } while ((OutputData += m) != OutputEnd);
    }

    // 回到块起始位置，执行蝶形合并
    OutputData = OutputBegin;
    switch (p) {
        case 2: Butterfly2(OutputData, FFTStride, Config, m); break;
        case 3: Butterfly3(OutputData, FFTStride, Config, m); break;
        case 4: Butterfly4(OutputData, FFTStride, Config, m); break;
        case 5: Butterfly5(OutputData, FFTStride, Config, m); break;
        default: ButterflyGeneric(OutputData, FFTStride, Config, m, p); break;
    }
}

/**
 * 将整数 n 分解为因子对（p, n/p），存入 FacBuf 数组。
 * 分解顺序：首先尝试 4,2,3,5,7,9,... 但只保留 2,3,5 等因子，以及可能的大素数因子（放在最后）。
 * 每找到一对 (p, n/p) 就存入 FacBuf，然后对 n/p 继续分解，直到 n == 1。
 *
 * @param n       待分解的正整数（FFT 长度）
 * @param FacBuf  输出缓冲区，格式为 p1, m1, p2, m2, ...，其中 m_i = n / p_i
 */
static void FactorizeSize(int n, int *FacBuf)
{
    int p = 4;
    double floor_sqrt = floor(sqrt((double)n));
    do {
        // 当 n 不能被 p 整除时，尝试下一个可能的因子
        while (n % p) {
            switch (p) {
                case 4: p = 2; break;
                case 2: p = 3; break;
                default: p += 2; break;
            }
            if (p > floor_sqrt) p = n;   // 如果 p 超过 sqrt(n)，则 n 本身为质数
        }
        n /= p;
        *FacBuf++ = p;      // 存储因子 p
        *FacBuf++ = n;      // 存储剩余长度 m
    } while (n > 1);
}

/**
 * 分配并初始化 FFT 配置结构体。
 *
 * @param FFT_N       变换长度（必须是可分解为 2,3,5 的乘积，否则内部会按质数处理）
 * @param Inverse_FFT 0 表示正向变换，非 0 表示反向变换
 * @param mem         可选的外部内存缓冲区（若为 NULL 则动态分配）
 * @param MemLen      输入时为 mem 缓冲区大小，输出时为实际需要的字节数
 * @return            成功返回配置句柄，失败返回 NULL
 */
FFT_ConfigHandle FFT_Alloc(int FFT_N, FFT_Direction Inverse_FFT, void *mem, size_t *MemLen)
{
    FFT_ConfigHandle Config = NULL;
    // 所需内存：状态结构体大小 + 旋转因子表大小（FFT_N 个复数，但结构体中已定义长度为1的柔性数组）
    size_t MemNeeded = sizeof(struct FFT_InternalState) + sizeof(FFT_Complex_t)*(FFT_N-1);

    if (MemLen == NULL) {
        // 用户未提供缓冲区，动态申请
        Config = (FFT_ConfigHandle) AllocateMemory(MemNeeded);
    } else {
        // 用户提供了缓冲区，检查大小是否足够
        if (mem != NULL && *MemLen >= MemNeeded)
            Config = (FFT_ConfigHandle) mem;
        *MemLen = MemNeeded;   // 返回所需大小
    }

    if (Config) {
        int i;
        Config->FFT_N = FFT_N;
        Config->Inverse = Inverse_FFT;

        // 预计算旋转因子表：对于每个 k (0..FFT_N-1)，计算 exp(-2π * i * k / FFT_N)
        // 反向变换时使用共轭，即相位取反。
        for (i=0; i<FFT_N; ++i) {
            const double pi = 3.1415926535897932384626433832795;
            double phase = -2.0 * pi * i / FFT_N;
            if (Config->Inverse) phase *= -1.0;
            SetTwiddle(Config->Twiddles + i, phase);
        }

        // 分解 FFT 长度，生成因子对表
        FactorizeSize(FFT_N, Config->Factors);
    }
    return Config;
}

/**
 * FFT 变换（支持输入步长）
 *
 * @param Config      配置句柄
 * @param InputData   输入复数数组
 * @param OutputData  输出复数数组（可以与 InputData 相同，进行原地变换）
 * @param InStride    从 InputData 中读取数据的步长（例如步长为2表示每隔一个取一个点）
 *
 * 如果 InputData == OutputData 且不为 NULL，将使用临时缓冲区避免数据覆盖。
 */
void FFT_Transform_Stride(FFT_ConfigHandle Config, const FFT_Complex_t *InputData, FFT_Complex_t *OutputData, int InStride)
{
    if (InputData == OutputData) {
        if (OutputData == NULL) return;
        // 原地变换需要临时缓冲区
        FFT_Complex_t *TmpBuf = (FFT_Complex_t*)AllocateMemory(sizeof(FFT_Complex_t)*Config->FFT_N);
        if (TmpBuf == NULL) return;
        WorkFFT(TmpBuf, InputData, 1, InStride, Config->Factors, Config);
        memcpy(OutputData, TmpBuf, sizeof(FFT_Complex_t)*Config->FFT_N);
        FreeMemory(TmpBuf);
    } else {
        // 非原地，直接计算
        WorkFFT(OutputData, InputData, 1, InStride, Config->Factors, Config);
    }
}

/**
 * 标准 FFT 变换（步长为 1）
 *
 * @param Config      配置句柄
 * @param InputData   输入数组
 * @param OutputData  输出数组
 */
void FFT_Transform(FFT_ConfigHandle Config, const FFT_Complex_t *InputData, FFT_Complex_t *OutputData)
{
    FFT_Transform_Stride(Config, InputData, OutputData, 1);
}

/**
 * 清理资源（当前版本未实现任何操作，因为内存在用户代码中通过 FreeMemory 释放）
 */
void FFT_Cleanup(void)
{
    // nothing
}

/**
 * 返回大于等于 n 的最小“快速尺寸”，即其质因子只包含 2、3、5。
 * 用于选择适合 FFT 的变换长度，以获得最高性能。
 *
 * @param n  输入长度
 * @return   满足条件的最小整数
 */
int FFT_Next_Fast_Size(int n)
{
    while (1) {
        int m = n;
        // 依次除去因子 2,3,5
        while ((m % 2) == 0) m /= 2;
        while ((m % 3) == 0) m /= 3;
        while ((m % 5) == 0) m /= 5;
        if (m <= 1) break;   // 只剩下1，说明 n 满足要求
        n++;
    }
    return n;
}

