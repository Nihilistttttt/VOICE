#include "mic_hum.h"
#include "Serial.h"
#include "debug.h"
#include "ch32h417.h"
#include "Config.h"
#include "hal_gpio.h"

/* 双 INMP441 麦克风采集
   麦克风A - SPI2: WS=PB12(AF5) CK=PB13(AF5) SD=PC1(AF5)
             DMA2_Channel1 + MuxChannel9 + 请求号66(SPI2_RX)
   麦克风B - SPI3: WS=PA4(AF6) CK=PC10(AF6) SD=PC12(AF6)
             DMA2_Channel2 + MuxChannel10 + 请求号68(SPI3_RX)
   32bit 帧占 4 个 HalfWord, 有效数据位置不固定, 需自动检测 */
#define FRAME_CNT    128
#define HW_PER_FRAME 4
#define HW_CNT       (FRAME_CNT * HW_PER_FRAME)

/* 吹气检测参数 (每tick约8ms = 128/16000) */
uint16_t g_blow_threshold  = 2000;
uint16_t g_short_max_tick  = 32;
uint16_t g_mid_max_tick    = 64;
uint16_t g_combo_wait_tick = 80;
uint16_t g_long_repeat_tick = 32;
uint16_t g_min_gap_tick    = 6;

int g_volume_mode = 0;

static uint16_t dma_buf_a[HW_CNT * 2];
static uint16_t dma_buf_b[HW_CNT * 2];

static int s_valid_pos_a = 0;
static int s_valid_pos_b = 0;

/* 麦克风状态 */
enum { MIC_IDLE, MIC_BLOWING, MIC_BLOW_GAP, MIC_SHORT_WAIT, MIC_SHORT_WAIT2 };

typedef struct {
    int state;
    int blow_ticks;
    int wait_ticks;
    int gap_ticks;
    int long_last_tick;
    int short_count;
    uint16_t peak;
    uint32_t sum;
    uint16_t s_peak;
    uint32_t s_sum;
    int s_ticks;
    int s_gap;
    uint16_t s2_peak;
    uint32_t s2_sum;
    int s2_ticks;
    int s2_gap;
} mic_t;

static mic_t mic_a = {0}, mic_b = {0};

/* 采集结果 (命令返回时填充) */
uint16_t g_col_peak1, g_col_avg1, g_col_ticks1, g_col_judge1;
uint16_t g_col_peak2, g_col_avg2, g_col_ticks2, g_col_judge2;
uint16_t g_col_gap;

/* 中断驱动能量计算 (双缓冲: HT读前半段, TC读后半段) */
static volatile uint8_t  s_a_ready = 0, s_b_ready = 0;
static volatile uint16_t s_irq_mean_a = 0, s_irq_mean_b = 0;
static volatile int32_t  s_irq_amp_a = 0, s_irq_amp_b = 0;

/* 全零重启检测 */
static uint32_t s_zero_cnt = 0;

/* 命令队列(简单环形) */
#define CMD_QUEUE_SIZE 8
static int s_cmd_queue[CMD_QUEUE_SIZE];
static int s_cmd_head = 0;
static int s_cmd_tail = 0;

static void queue_cmd(int cmd)
{
    int next = (s_cmd_tail + 1) % CMD_QUEUE_SIZE;
    if (next == s_cmd_head) return;
    s_cmd_queue[s_cmd_tail] = cmd;
    s_cmd_tail = next;
}

static int dequeue_cmd(void)
{
    if (s_cmd_head == s_cmd_tail) return HUM_CMD_NONE;
    int cmd = s_cmd_queue[s_cmd_head];
    s_cmd_head = (s_cmd_head + 1) % CMD_QUEUE_SIZE;
    return cmd;
}

/* DMA中断: 计算单通道能量 (128帧=8ms) */
static void calc_energy_a(int offset) {
    int32_t mn = 32767, mx = -32768;
    uint32_t acc = 0;
    for (int i = 0; i < FRAME_CNT; i++) {
        int16_t s = (int16_t)dma_buf_a[offset + i * HW_PER_FRAME + s_valid_pos_a];
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        acc += (s < 0) ? (uint32_t)(-s) : (uint32_t)(s);
    }
    s_irq_amp_a = mx - mn;
    s_irq_mean_a = (uint16_t)(acc / FRAME_CNT);
    s_a_ready = 1;
}

static void calc_energy_b(int offset) {
    int32_t mn = 32767, mx = -32768;
    uint32_t acc = 0;
    for (int i = 0; i < FRAME_CNT; i++) {
        int16_t s = (int16_t)dma_buf_b[offset + i * HW_PER_FRAME + s_valid_pos_b];
        if (s < mn) mn = s;
        if (s > mx) mx = s;
        acc += (s < 0) ? (uint32_t)(-s) : (uint32_t)(s);
    }
    s_irq_amp_b = mx - mn;
    s_irq_mean_b = (uint16_t)(acc / FRAME_CNT);
    s_b_ready = 1;
}

void DMA2_Channel1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA2_Channel1_IRQHandler(void) {
    if (DMA_GetITStatus(DMA2, DMA2_IT_HT1)) {
        DMA_ClearITPendingBit(DMA2, DMA2_IT_HT1);
        calc_energy_a(0);
    }
    if (DMA_GetITStatus(DMA2, DMA2_IT_TC1)) {
        DMA_ClearITPendingBit(DMA2, DMA2_IT_TC1);
        calc_energy_a(HW_CNT);
    }
}

void DMA2_Channel2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA2_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA2, DMA2_IT_HT2)) {
        DMA_ClearITPendingBit(DMA2, DMA2_IT_HT2);
        calc_energy_b(0);
    }
    if (DMA_GetITStatus(DMA2, DMA2_IT_TC2)) {
        DMA_ClearITPendingBit(DMA2, DMA2_IT_TC2);
        calc_energy_b(HW_CNT);
    }
}

/* 单麦克风吹气状态机, 返回 HUM_CMD_* 或 HUM_CMD_NONE
   cmd_short:  短吹
   cmd_long:   中吹(单次) / 长吹(连续触发)
   cmd_ss:     短+短
   cmd_sss:    短+短+短
*/
static int mic_process(mic_t *m, int blowing, uint16_t energy,
                       int cmd_short, int cmd_long, int cmd_ss, int cmd_sss)
{
    switch (m->state) {
    case MIC_IDLE:
        if (blowing) {
            m->state = MIC_BLOWING;
            m->blow_ticks = 1;
            m->long_last_tick = 0;
            m->peak = energy;
            m->sum = energy;
        }
        break;

    case MIC_BLOWING:
        if (blowing) {
            m->blow_ticks++;
            if (energy > m->peak) m->peak = energy;
            m->sum += energy;
            if (m->blow_ticks > g_mid_max_tick) {
                if (m->long_last_tick == 0) {
                    m->long_last_tick = m->blow_ticks;
                    g_col_peak1 = m->peak;
                    g_col_avg1 = m->sum / m->blow_ticks;
                    g_col_ticks1 = m->blow_ticks;
                    g_col_judge1 = 3;
                    g_col_judge2 = 0;
                    g_col_gap = 0;
                    return cmd_long;
                } else if (m->blow_ticks - m->long_last_tick >= g_long_repeat_tick) {
                    m->long_last_tick = m->blow_ticks;
                    g_col_peak1 = m->peak;
                    g_col_avg1 = m->sum / m->blow_ticks;
                    g_col_ticks1 = m->blow_ticks;
                    g_col_judge1 = 3;
                    g_col_judge2 = 0;
                    g_col_gap = 0;
                    return cmd_long;
                }
            }
        } else {
            m->state = MIC_BLOW_GAP;
            m->gap_ticks = 0;
        }
        break;

    case MIC_BLOW_GAP:
        if (blowing) {
            m->state = MIC_BLOWING;
            m->blow_ticks++;
            if (energy > m->peak) m->peak = energy;
            m->sum += energy;
        } else {
            m->gap_ticks++;
            if (m->gap_ticks >= g_min_gap_tick) {
                if (m->blow_ticks <= g_short_max_tick) {
                    if (m->short_count == 0) {
                        m->short_count = 1;
                        m->state = MIC_SHORT_WAIT;
                        m->wait_ticks = 0;
                        m->s_peak = m->peak;
                        m->s_sum = m->sum;
                        m->s_ticks = m->blow_ticks;
                    } else if (m->short_count == 1) {
                        m->short_count = 2;
                        m->state = MIC_SHORT_WAIT2;
                        m->wait_ticks = 0;
                        m->s2_peak = m->peak;
                        m->s2_sum = m->sum;
                        m->s2_ticks = m->blow_ticks;
                        m->s2_gap = m->gap_ticks;
                    } else {
                        m->short_count = 0;
                        m->state = MIC_IDLE;
                        g_col_peak1 = m->s2_peak;
                        g_col_avg1 = m->s2_sum / m->s2_ticks;
                        g_col_ticks1 = m->s2_ticks;
                        g_col_judge1 = 1;
                        g_col_peak2 = m->peak;
                        g_col_avg2 = m->sum / m->blow_ticks;
                        g_col_ticks2 = m->blow_ticks;
                        g_col_judge2 = 1;
                        g_col_gap = (uint16_t)m->s2_gap;
                        return cmd_sss;
                    }
                } else {
                    int cmd = HUM_CMD_NONE;
                    if (m->long_last_tick == 0) {
                        cmd = cmd_long;
                        g_col_peak1 = m->peak;
                        g_col_avg1 = m->sum / m->blow_ticks;
                        g_col_ticks1 = m->blow_ticks;
                        g_col_judge1 = (m->blow_ticks > g_mid_max_tick) ? 3 : 2;
                        g_col_judge2 = 0;
                        g_col_gap = 0;
                    }
                    m->short_count = 0;
                    m->state = MIC_IDLE;
                    return cmd;
                }
            }
        }
        break;

    case MIC_SHORT_WAIT:
        m->wait_ticks++;
        if (blowing) {
            if (m->wait_ticks < g_min_gap_tick) {
                m->state = MIC_BLOWING;
                m->blow_ticks = m->s_ticks + 1;
                m->peak = (m->s_peak > energy) ? m->s_peak : energy;
                m->sum = m->s_sum + energy;
                m->long_last_tick = 0;
                m->short_count = 0;
            } else {
                m->s_gap = m->wait_ticks;
                m->state = MIC_BLOWING;
                m->blow_ticks = 1;
                m->long_last_tick = 0;
                m->peak = energy;
                m->sum = energy;
            }
        } else if (m->wait_ticks > g_combo_wait_tick) {
            m->short_count = 0;
            m->state = MIC_IDLE;
            g_col_peak1 = m->s_peak;
            g_col_avg1 = m->s_sum / m->s_ticks;
            g_col_ticks1 = m->s_ticks;
            g_col_judge1 = 1;
            g_col_judge2 = 0;
            g_col_gap = 0;
            return cmd_short;
        }
        break;

    case MIC_SHORT_WAIT2:
        m->wait_ticks++;
        if (blowing) {
            if (m->wait_ticks < g_min_gap_tick) {
                m->state = MIC_BLOWING;
                m->blow_ticks = m->s2_ticks + 1;
                m->peak = (m->s2_peak > energy) ? m->s2_peak : energy;
                m->sum = m->s2_sum + energy;
                m->long_last_tick = 0;
                m->short_count = 1;
            } else {
                m->s2_gap = m->wait_ticks;
                m->state = MIC_BLOWING;
                m->blow_ticks = 1;
                m->long_last_tick = 0;
                m->peak = energy;
                m->sum = energy;
            }
        } else if (m->wait_ticks > g_combo_wait_tick) {
            m->short_count = 0;
            m->state = MIC_IDLE;
            g_col_peak1 = m->s_peak;
            g_col_avg1 = m->s_sum / m->s_ticks;
            g_col_ticks1 = m->s_ticks;
            g_col_judge1 = 1;
            g_col_peak2 = m->s2_peak;
            g_col_avg2 = m->s2_sum / m->s2_ticks;
            g_col_ticks2 = m->s2_ticks;
            g_col_judge2 = 1;
            g_col_gap = (uint16_t)m->s2_gap;
            return cmd_ss;
        }
        break;
    }
    return HUM_CMD_NONE;
}

static int Auto_Detect_Pos_A(void)
{
    int valid_pos = 0;
    int32_t min_amp[4] = {100000, 100000, 100000, 100000};
    for (int g = 0; g < 10; g++) {
        while (!DMA_GetFlagStatus(DMA2, DMA2_FLAG_TC1)) {}
        DMA_ClearFlag(DMA2, DMA2_FLAG_TC1);
        for (int p = 0; p < 4; p++) {
            int32_t mn = 32767, mx = -32768;
            for (int i = 0; i < FRAME_CNT; i++) {
                int16_t s = (int16_t)dma_buf_a[i * HW_PER_FRAME + p];
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
            int32_t a = mx - mn;
            if (a < min_amp[p]) min_amp[p] = a;
        }
    }
    int32_t best_amp = 100000;
    for (int p = 0; p < 4; p++) {
        if (min_amp[p] < best_amp && min_amp[p] > 50) {
            best_amp = min_amp[p];
            valid_pos = p;
        }
    }
    Serial_Dbg_Printf("MIC_A valid_pos=%d (amp: %ld %ld %ld %ld)\r\n",
           valid_pos, (long)min_amp[0], (long)min_amp[1], (long)min_amp[2], (long)min_amp[3]);
    return valid_pos;
}

static int Auto_Detect_Pos_B(void)
{
    int valid_pos = 0;
    int32_t min_amp[4] = {100000, 100000, 100000, 100000};
    for (int g = 0; g < 10; g++) {
        while (!DMA_GetFlagStatus(DMA2, DMA2_FLAG_TC2)) {}
        DMA_ClearFlag(DMA2, DMA2_FLAG_TC2);
        for (int p = 0; p < 4; p++) {
            int32_t mn = 32767, mx = -32768;
            for (int i = 0; i < FRAME_CNT; i++) {
                int16_t s = (int16_t)dma_buf_b[i * HW_PER_FRAME + p];
                if (s < mn) mn = s;
                if (s > mx) mx = s;
            }
            int32_t a = mx - mn;
            if (a < min_amp[p]) min_amp[p] = a;
        }
    }
    int32_t best_amp = 100000;
    for (int p = 0; p < 4; p++) {
        if (min_amp[p] < best_amp && min_amp[p] > 50) {
            best_amp = min_amp[p];
            valid_pos = p;
        }
    }
    Serial_Dbg_Printf("MIC_B valid_pos=%d (amp: %ld %ld %ld %ld)\r\n",
           valid_pos, (long)min_amp[0], (long)min_amp[1], (long)min_amp[2], (long)min_amp[3]);
    return valid_pos;
}

static void I2S_Restart(void)
{
    DMA_ITConfig(DMA2_Channel1, DMA_IT_TC | DMA_IT_HT, DISABLE);
    DMA_ITConfig(DMA2_Channel2, DMA_IT_TC | DMA_IT_HT, DISABLE);

    I2S_Cmd(SPI2, DISABLE);
    I2S_Cmd(SPI3, DISABLE);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Rx, DISABLE);
    DMA_Cmd(DMA2_Channel1, DISABLE);
    DMA_Cmd(DMA2_Channel2, DISABLE);
    Delay_Ms(100);

    I2S_Cmd(SPI2, ENABLE);
    Delay_Ms(500);
    I2S_Cmd(SPI3, ENABLE);
    Delay_Ms(500);
    DMA_ClearFlag(DMA2, DMA2_FLAG_TC1);
    DMA_ClearFlag(DMA2, DMA2_FLAG_TC2);
    DMA_Cmd(DMA2_Channel1, ENABLE);
    DMA_Cmd(DMA2_Channel2, ENABLE);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Rx, ENABLE);
    for (int g = 0; g < 4; g++) {
        while (!DMA_GetFlagStatus(DMA2, DMA2_FLAG_TC1)) {}
        DMA_ClearFlag(DMA2, DMA2_FLAG_TC1);
        while (!DMA_GetFlagStatus(DMA2, DMA2_FLAG_TC2)) {}
        DMA_ClearFlag(DMA2, DMA2_FLAG_TC2);
    }
    s_valid_pos_a = Auto_Detect_Pos_A();
    s_valid_pos_b = Auto_Detect_Pos_B();

    DMA_ClearFlag(DMA2, DMA2_FLAG_HT1 | DMA2_FLAG_TC1);
    DMA_ClearFlag(DMA2, DMA2_FLAG_HT2 | DMA2_FLAG_TC2);
    DMA_ITConfig(DMA2_Channel1, DMA_IT_TC | DMA_IT_HT, ENABLE);
    DMA_ITConfig(DMA2_Channel2, DMA_IT_TC | DMA_IT_HT, ENABLE);

    s_a_ready = 0; s_b_ready = 0;
    s_zero_cnt = 0;
}

void Mic_Hum_Init(void)
{
    I2S_InitTypeDef  I2S_InitStructure  = {0};
    DMA_InitTypeDef  DMA_InitStructure  = {0};

    AFIO_ClockEnable();
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SPI2, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SPI3, ENABLE);
    DMA_ClockEnable(DMA2);
    GPIO_ClockEnable(HAL_GPIO_PORT(I2S2_WS_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(I2S2_SD_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(I2S3_WS_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(I2S3_CK_PIN_ENC));
    GPIO_ClockEnable(HAL_GPIO_PORT(I2S3_SD_PIN_ENC));

    /* SPI2 引脚: WS=PB12 CK=PB13 SD=PC1 (AF5) */
    Hal_GPIO_Init(I2S2_WS_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, I2S2_GPIO_AF);
    Hal_GPIO_Init(I2S2_CK_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, I2S2_GPIO_AF);
    Hal_GPIO_Init(I2S2_SD_PIN_ENC, HAL_GPIO_MODE_AF_INPUT, HAL_GPIO_SPEED_VERY_HIGH, I2S2_GPIO_AF);

    /* SPI3 引脚: WS=PA4 CK=PC10 SD=PC12 (AF6) */
    Hal_GPIO_Init(I2S3_WS_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, I2S3_GPIO_AF);
    Hal_GPIO_Init(I2S3_CK_PIN_ENC, HAL_GPIO_MODE_AF_PP, HAL_GPIO_SPEED_VERY_HIGH, I2S3_GPIO_AF);
    Hal_GPIO_Init(I2S3_SD_PIN_ENC, HAL_GPIO_MODE_AF_INPUT, HAL_GPIO_SPEED_VERY_HIGH, I2S3_GPIO_AF);

    /* I2S2 配置 */
    I2S_InitStructure.I2S_Mode       = I2S_Mode_MasterRx;
    I2S_InitStructure.I2S_Standard   = I2S_Standard_Phillips;
    I2S_InitStructure.I2S_DataFormat = I2S_DataFormat_32b;
    I2S_InitStructure.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
    I2S_InitStructure.I2S_AudioFreq  = I2S_AudioFreq_16k;
    I2S_InitStructure.I2S_CPOL       = I2S_CPOL_Low;
    I2S_Init(SPI2, &I2S_InitStructure);
    SPI2->I2SPR = 0x00C3;

    /* I2S3 配置 */
    I2S_Init(SPI3, &I2S_InitStructure);
    SPI3->I2SPR = 0x00C3;

    /* DMA2_Channel1 - SPI2_RX (麦克风A) */
    DMA_DeInit(DMA2_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DATAR;
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)dma_buf_a;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = HW_CNT * 2;
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA2_Channel1, &DMA_InitStructure);
    DMA_MuxChannelConfig(DMA_MuxChannel9, 66);

    /* DMA2_Channel2 - SPI3_RX (麦克风B) */
    DMA_DeInit(DMA2_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI3->DATAR;
    DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)dma_buf_b;
    DMA_Init(DMA2_Channel2, &DMA_InitStructure);
    DMA_MuxChannelConfig(DMA_MuxChannel10, 68);

    /* 启动 I2S2 */
    I2S_Cmd(SPI2, ENABLE);
    Delay_Ms(500);

    /* 启动 I2S3 */
    I2S_Cmd(SPI3, ENABLE);
    Delay_Ms(500);

    /* 启动 DMA */
    DMA_ClearFlag(DMA2, DMA2_FLAG_TC1);
    DMA_Cmd(DMA2_Channel1, ENABLE);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);

    DMA_ClearFlag(DMA2, DMA2_FLAG_TC2);
    DMA_Cmd(DMA2_Channel2, ENABLE);
    SPI_I2S_DMACmd(SPI3, SPI_I2S_DMAReq_Rx, ENABLE);
    Delay_Ms(100);

    /* 丢弃前几个周期 */
    for (int g = 0; g < 4; g++) {
        while (!DMA_GetFlagStatus(DMA2, DMA2_FLAG_TC1)) {}
        DMA_ClearFlag(DMA2, DMA2_FLAG_TC1);
        while (!DMA_GetFlagStatus(DMA2, DMA2_FLAG_TC2)) {}
        DMA_ClearFlag(DMA2, DMA2_FLAG_TC2);
    }

    s_valid_pos_a = Auto_Detect_Pos_A();
    s_valid_pos_b = Auto_Detect_Pos_B();

    /* 使能DMA中断 (双缓冲: HT+TC, 每次处理128帧=8ms) */
    DMA_ClearFlag(DMA2, DMA2_FLAG_HT1 | DMA2_FLAG_TC1);
    DMA_ClearFlag(DMA2, DMA2_FLAG_HT2 | DMA2_FLAG_TC2);
    DMA_ITConfig(DMA2_Channel1, DMA_IT_TC | DMA_IT_HT, ENABLE);
    DMA_ITConfig(DMA2_Channel2, DMA_IT_TC | DMA_IT_HT, ENABLE);
    NVIC_SetPriority(DMA2_Channel1_IRQn, (1 << 7) | (3 << 4));
    NVIC_EnableIRQ(DMA2_Channel1_IRQn);
    NVIC_SetPriority(DMA2_Channel2_IRQn, (1 << 7) | (3 << 4));
    NVIC_EnableIRQ(DMA2_Channel2_IRQn);

    Serial_Dbg_Printf("========================================\r\n");
    Serial_Dbg_Printf("  双麦克风吹气控制\r\n");
    Serial_Dbg_Printf("  MIC_A: 短=确定 中/长=下一个 短短=播放 短短短=音量模式\r\n");
    Serial_Dbg_Printf("  MIC_B: 短=返回 中/长=上一个 短短=模式切换 短短短=音量模式\r\n");
    Serial_Dbg_Printf("  跨麦: 左短+右短=左 右短+左短=右\r\n");
    Serial_Dbg_Printf("  (音量模式下中/长=音量+/-, 短短短切换)\r\n");
    Serial_Dbg_Printf("========================================\r\n");
    Serial_Dbg_Printf("系统就绪，请吹气操作!\r\n");
}

int Mic_Hum_Poll(uint16_t *p_mean_a, uint16_t *p_mean_b)
{
    *p_mean_a = 0;
    *p_mean_b = 0;

    /* 先处理队列中待发命令 */
    int queued = dequeue_cmd();
    if (queued != HUM_CMD_NONE) return queued;

    /* 等待中断计算的能量值就绪 (8ms/tick) */
    if (!s_a_ready || !s_b_ready) return HUM_CMD_NONE;
    s_a_ready = 0; s_b_ready = 0;

    uint32_t mean_a = s_irq_mean_a;
    uint32_t mean_b = s_irq_mean_b;
    int32_t amp_a = s_irq_amp_a;
    int32_t amp_b = s_irq_amp_b;
    *p_mean_a = (mean_a > 65535) ? 65535 : (uint16_t)mean_a;
    *p_mean_b = (mean_b > 65535) ? 65535 : (uint16_t)mean_b;
    int blow_a = (mean_a > g_blow_threshold);
    int blow_b = (mean_b > g_blow_threshold);

    /* 冲突判决: 两个都吹时只取能量高的 */
    if (blow_a && blow_b) {
        if (mean_a >= mean_b) blow_b = 0;
        else                  blow_a = 0;
    }

    /* MIC_A: 短=ENTER 中/长=NEXT(或VOL_UP) 短短=PLAY 短短短=VOL_MODE */
    int prev_state_a = mic_a.state;
    int prev_state_b = mic_b.state;
    int cmd_long_a = g_volume_mode ? HUM_CMD_VOL_UP : HUM_CMD_NEXT;
    int cmd_long_b = g_volume_mode ? HUM_CMD_VOL_DN : HUM_CMD_PREV;
    int cmd_a = mic_process(&mic_a, blow_a, mean_a, HUM_CMD_ENTER, cmd_long_a, HUM_CMD_PLAY, HUM_CMD_VOL_MODE);
    int cmd_b = mic_process(&mic_b, blow_b, mean_b, HUM_CMD_BACK, cmd_long_b, HUM_CMD_MODE, HUM_CMD_VOL_MODE);

    /* 跨麦克风短吹组合: 左短+右短=LEFT, 右短+左短=RIGHT */
    if (mic_b.state == MIC_SHORT_WAIT && prev_state_b != MIC_SHORT_WAIT
        && mic_a.state == MIC_SHORT_WAIT) {
        cmd_b = HUM_CMD_LEFT;
        g_col_peak1 = mic_a.s_peak; g_col_avg1 = mic_a.s_sum / mic_a.s_ticks;
        g_col_ticks1 = mic_a.s_ticks; g_col_judge1 = 1;
        g_col_peak2 = mic_b.s_peak; g_col_avg2 = mic_b.s_sum / mic_b.s_ticks;
        g_col_ticks2 = mic_b.s_ticks; g_col_judge2 = 1;
        g_col_gap = 0;
        mic_a.state = MIC_IDLE; mic_a.short_count = 0;
        mic_b.state = MIC_IDLE; mic_b.short_count = 0;
    } else if (mic_a.state == MIC_SHORT_WAIT && prev_state_a != MIC_SHORT_WAIT
               && mic_b.state == MIC_SHORT_WAIT) {
        cmd_a = HUM_CMD_RIGHT;
        g_col_peak1 = mic_b.s_peak; g_col_avg1 = mic_b.s_sum / mic_b.s_ticks;
        g_col_ticks1 = mic_b.s_ticks; g_col_judge1 = 1;
        g_col_peak2 = mic_a.s_peak; g_col_avg2 = mic_a.s_sum / mic_a.s_ticks;
        g_col_ticks2 = mic_a.s_ticks; g_col_judge2 = 1;
        g_col_gap = 0;
        mic_a.state = MIC_IDLE; mic_a.short_count = 0;
        mic_b.state = MIC_IDLE; mic_b.short_count = 0;
    }

    if (cmd_a != HUM_CMD_NONE) queue_cmd(cmd_a);
    if (cmd_b != HUM_CMD_NONE) queue_cmd(cmd_b);

    /* 全零重启检测 */
    if (amp_a == 0 && amp_b == 0) {
        s_zero_cnt++;
        if (s_zero_cnt >= 12) {
            Serial_Dbg_Printf("!!! all zero, restart I2S !!!\r\n");
            s_zero_cnt = 0;
            I2S_Restart();
        }
    } else {
        s_zero_cnt = 0;
    }

    int cmd = dequeue_cmd();
    if (cmd == HUM_CMD_VOL_MODE) {
        g_volume_mode = !g_volume_mode;
        Serial_Dbg_Printf("Volume mode: %d\r\n", g_volume_mode);
    }
    return cmd;
}

void Mic_Hum_TestLoop(void)
{
    Mic_Hum_Init();

    uint32_t loop = 0;
    while (1) {
        while (!s_a_ready || !s_b_ready) {}
        s_a_ready = 0; s_b_ready = 0;

        Serial_Dbg_Printf("[%lu] A: mean=%u amp=%ld  B: mean=%u amp=%ld  %s%s\r\n",
               (unsigned long)loop,
               (unsigned)s_irq_mean_a, (long)s_irq_amp_a,
               (unsigned)s_irq_mean_b, (long)s_irq_amp_b,
               (s_irq_mean_a > g_blow_threshold) ? "BLOW_A" : "",
               (s_irq_mean_b > g_blow_threshold) ? " BLOW_B" : "");
        loop++;

        if (s_irq_amp_a == 0 && s_irq_amp_b == 0) {
            s_zero_cnt++;
            if (s_zero_cnt >= 12) {
                Serial_Dbg_Printf("!!! all zero, restart !!!\r\n");
                s_zero_cnt = 0;
                I2S_Restart();
            }
        } else {
            s_zero_cnt = 0;
        }
    }
}
