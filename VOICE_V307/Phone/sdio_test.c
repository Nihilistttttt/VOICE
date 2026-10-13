#include "sdio_test.h"
#include "sdio_sd.h"
#include "debug.h"
#include "string.h"
#include "ch32v30x_tim.h"

/* ====== 基准测试参数 ====== */
#define BENCH_SECTORS   32              /* 32扇区 = 16KB per I/O */
#define BENCH_ROUNDS    8               /* 每档时钟测8轮取平均 */
#define BENCH_BUF_SIZE  (BENCH_SECTORS * 512)
#define BENCH_START_SEC 1000            /* 测试起始扇区(会覆盖数据!) */

/* 时钟分频表: SDIO_CK = 144MHz / (div + 2) */
typedef struct { u8 div; u32 khz; const char *label; } clk_entry_t;
static const clk_entry_t clk_table[] = {
    { 1, 48000, "48.0MHz" },
    { 2, 36000, "36.0MHz" },
    { 3, 28800, "28.8MHz" },
    { 4, 24000, "24.0MHz" },
    { 6, 18000, "18.0MHz" },
};
#define CLK_TABLE_LEN  (sizeof(clk_table) / sizeof(clk_table[0]))

static u8 s_wr_buf[BENCH_BUF_SIZE] __attribute__((aligned(4)));
static u8 s_rd_buf[BENCH_BUF_SIZE] __attribute__((aligned(4)));

/* TIM2: 144MHz/(1439+1)=100kHz, 1tick=10us, 16位最大655ms */
static void bench_timer_init(void)
{
    TIM_TimeBaseInitTypeDef tim;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    tim.TIM_Period = 0xFFFF;
    tim.TIM_Prescaler = 1439;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);
    TIM_SetCounter(TIM2, 0);
    TIM_Cmd(TIM2, ENABLE);
}

static u32 bench_get_us(void)
{
    return (u32)TIM_GetCounter(TIM2) * 10;
}

static void show_sdcard_info(void)
{
    switch (SDCardInfo.CardType) {
        case SDIO_STD_CAPACITY_SD_CARD_V1_1:  printf("Type: SDSC V1.1\r\n"); break;
        case SDIO_STD_CAPACITY_SD_CARD_V2_0:  printf("Type: SDSC V2.0\r\n"); break;
        case SDIO_HIGH_CAPACITY_SD_CARD:      printf("Type: SDHC/SDXC\r\n"); break;
        case SDIO_MULTIMEDIA_CARD:            printf("Type: MMC\r\n");       break;
        default:                              printf("Type: Unknown(%d)\r\n", SDCardInfo.CardType); break;
    }
    printf("Capacity: %lu MB\r\n", (u32)(SDCardInfo.CardCapacity >> 20));
    printf("BlockSize: %lu\r\n", SDCardInfo.CardBlockSize);
    printf("Bus: 4-bit SDIO\r\n");
}

static void bench_at_clk(const clk_entry_t *e)
{
    u32 i, total_r = 0, total_w = 0, nr = 0, nw = 0;
    u8 sta;
    u64 rate;

    printf("\r\n--- %s (div=%u) ---\r\n", e->label, e->div);

    SDIO_Clock_Set(e->div);
    Delay_Ms(2);

    /* === Read === */
    for (i = 0; i < BENCH_ROUNDS; i++)
    {
        u32 start = bench_get_us();
        sta = SD_ReadDisk(s_rd_buf, BENCH_START_SEC, BENCH_SECTORS);
        u32 us = bench_get_us() - start;
        if (sta != SD_OK) {
            printf("  R%lu FAIL err=%d\r\n", i + 1, sta);
            break;
        }
        total_r += us;
        nr++;
    }
    if (nr > 0) {
        rate = (u64)(BENCH_BUF_SIZE * nr) * 1000000ULL;
        rate /= (u64)total_r;
        rate /= 1024ULL;
        printf("  Read:  %lu KB/s  (%lu ok, %lu us total)\r\n",
               (u32)rate, nr, total_r);
    }

    /* === Write === */
    for (i = 0; i < BENCH_ROUNDS; i++)
    {
        u32 start = bench_get_us();
        sta = SD_WriteDisk(s_wr_buf, BENCH_START_SEC, BENCH_SECTORS);
        u32 us = bench_get_us() - start;
        if (sta != SD_OK) {
            printf("  W%lu FAIL err=%d\r\n", i + 1, sta);
            break;
        }
        total_w += us;
        nw++;
    }
    if (nw > 0) {
        rate = (u64)(BENCH_BUF_SIZE * nw) * 1000000ULL;
        rate /= (u64)total_w;
        rate /= 1024ULL;
        printf("  Write: %lu KB/s  (%lu ok, %lu us total)\r\n",
               (u32)rate, nw, total_w);
    }

    /* 验证数据完整性 */
    if (nr > 0 && nw > 0) {
        SD_ReadDisk(s_rd_buf, BENCH_START_SEC, BENCH_SECTORS);
        if (memcmp(s_wr_buf, s_rd_buf, BENCH_BUF_SIZE) == 0)
            printf("  Verify: OK\r\n");
        else
            printf("  Verify: FAIL!\r\n");
    }
}

void SDIO_Test_Run(void)
{
    u32 i;
    SD_Error sta;

    printf("\r\n========== SDIO Benchmark ==========\r\n");
    printf("SysClk: %lu MHz\r\n", SystemCoreClock / 1000000);
    printf("Config: %u sectors x %u rounds = %u KB per test\r\n",
           BENCH_SECTORS, BENCH_ROUNDS,
           BENCH_BUF_SIZE * BENCH_ROUNDS / 1024);
    printf("Theory max(4bit@48MHz): ~24000 KB/s\r\n");

    bench_timer_init();

    /* 初始化SD卡 */
    while ((sta = SD_Init()) != SD_OK) {
        printf("SD_Init fail: %d, retry...\r\n", sta);
        Delay_Ms(1000);
    }
    printf("SD_Init OK\r\n");
    show_sdcard_info();

    /* 准备测试数据 */
    for (i = 0; i < BENCH_BUF_SIZE; i++)
        s_wr_buf[i] = (u8)(i & 0xFF);

    /* 预写+校验 */
    printf("\r\nPre-write test pattern at sec %u...\r\n", BENCH_START_SEC);
    sta = SD_WriteDisk(s_wr_buf, BENCH_START_SEC, BENCH_SECTORS);
    if (sta != SD_OK) { printf("Pre-write FAIL: %d\r\n", sta); return; }

    memset(s_rd_buf, 0, BENCH_BUF_SIZE);
    sta = SD_ReadDisk(s_rd_buf, BENCH_START_SEC, BENCH_SECTORS);
    if (sta != SD_OK) { printf("Pre-read FAIL: %d\r\n", sta); return; }
    if (memcmp(s_wr_buf, s_rd_buf, BENCH_BUF_SIZE) == 0)
        printf("Verify OK: %u bytes\r\n", BENCH_BUF_SIZE);
    else { printf("Verify FAIL!\r\n"); return; }

    /* 逐档时钟测试 */
    printf("\r\n===== Sweep clock frequencies =====\r\n");
    for (i = 0; i < CLK_TABLE_LEN; i++)
        bench_at_clk(&clk_table[i]);

    printf("\r\n===== Benchmark Done =====\r\n");
    printf("Check results above for max stable rate.\r\n");
}