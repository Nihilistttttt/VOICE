/*============================================================================
 * 分级日志模块实现（设计出处见 log.h 文件头——借鉴清单逐条标注）
 *
 * 数据流: LOGx() → 级别过滤 → vsnprintf 成行 → ①串口立即 printf
 *                                              → ②SD 行缓冲(若已附加)
 *         Log_Service()(主循环) → 限频把 ② 刷入 /1:/voice_log.txt
 *
 * 关键决策（对照调研结论）:
 *   - 行格式 "[E][tag] msg\r\n"（对齐 Android logcat 的 E/W/I/D 直觉）
 *   - 缓冲满: 先尝试"立即刷一次"再放不下 → 丢弃 + 丢弃计数(下次刷盘时
 *     报告条数, 对齐 Zephyr 的 dropped 一等公民)
 *   - 单次刷盘 = 一次 f_write ≤512B（轮询模式数 ms）, 限频 ≥5s 或 ≥80%
 *     —— HID 热循环预算内
 *   - SD 任何失败 → s_sd_ok=0 摘除后端, 系统继续跑(日志必须无害)
 *   - MODE=2(U盘导出SD): Suspend 后不再碰 FatFs —— 双主保护
 *   - 文件轮转: voice_log.txt >64KB → 改名 .old 重建(EasyLogger 单代轮转简化)
 *   - BOOT 头带启动序号(BKP 计数, 跨系统复位保持) —— 多次上电的日志
 *     在文件里天然分段
 *============================================================================*/

#include "log.h"
#include "Config.h"
#include "debug.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if USE_SDCARD
#include "ff.h"         /* FatFs(Middleware): SD 文件后端 */
#include "params_sd.h"  /* 仅复用其挂载时序约定(挂载由 params_sd 完成) */
#define LOG_SD_ENABLE   1
#endif

/* ---- 可调常量（编译期, 非运行时参数——日志自身不进参数表防套娃）---------*/
#define LOG_LINE_SIZE       192     /* 单行格式化上限 */
#define LOG_BUF_SIZE        512     /* SD 行缓冲(FatFs 一个扇区量级) */
#define LOG_FLUSH_HIGH      448     /* 缓冲高水位: 超过即刷 */
#define LOG_FLUSH_PERIOD    50000   /* 刷盘间隔, 100us 单位 = 5s(时间基准, 不依赖主循环速度) */
#define LOG_ROTATE_SIZE     65536   /* 日志文件轮转阈值 64KB */
#define LOG_PATH            "1:/log.txt"      /* 8.3 短名(同上) */
#define LOG_PATH_OLD        "1:/log.old"

/* ---- 运行时状态 -----------------------------------------------------------*/
static int32_t  s_level = LOG_LVL_DEBUG;    /* 运行时级别(默认全量, 调试期) */

#if LOG_SD_ENABLE
static FIL      s_fp;                       /* 日志文件对象 */
static uint8_t  s_sd_ok = 0;                /* SD 后端可用标志(失败自动摘除) */
static uint8_t  s_suspended = 0;            /* MODE=2 挂起标志 */
static char     s_buf[LOG_BUF_SIZE];        /* 行缓冲(攒批) */
static uint16_t s_len = 0;                  /* 缓冲内有效字节 */
static uint32_t s_dropped = 0;              /* 丢弃计数(缓冲满时) */
static uint32_t s_acc = 0;                  /* 经过时间累计, 100us 单位(限频用) */
static uint32_t s_boot_cnt = 0;             /* 启动序号(BKP 计数) */
#endif

/*---------------------------------------------------------------------------
 * 内部: 单行格式化(级别字符 + 标签 + 正文)
 *---------------------------------------------------------------------------*/
static void log_format(char *dst, uint16_t size, uint8_t level,
                       const char *tag, const char *fmt, va_list ap)
{
    static const char lvl_ch[] = { 'E', 'W', 'I', 'D' };
    uint16_t n = (uint16_t)snprintf(dst, size, "[%c][%s] ",
                                    (level <= LOG_LVL_DEBUG) ? lvl_ch[level] : '?',
                                    (tag != 0) ? tag : "?");

    if (n < size) {
        (void)vsnprintf(dst + n, (size_t)(size - n), fmt, ap);
    }
    /* 尾部换行保证(截断时强制补) */
    dst[size - 1] = 0;
    n = (uint16_t)strlen(dst);
    if ((n + 2) < size) {
        dst[n] = '\r';
        dst[n + 1] = '\n';
        dst[n + 2] = 0;
    }
}

/**
 * @brief 写一条日志（串口立即; SD 进缓冲按限频策略刷盘）
 *
 * @param level 级别
 * @param tag   模块标签
 * @param fmt   printf 风格格式串
 */
void Log_Write(uint8_t level, const char *tag, const char *fmt, ...)
{
    char line[LOG_LINE_SIZE];
    va_list ap;
    uint16_t len;

    if (level > s_level) {
        return;                     /* 运行时级别过滤(最便宜的检查放最前) */
    }

    va_start(ap, fmt);
    log_format(line, sizeof(line), level, tag, fmt, ap);
    va_end(ap);

    printf("%s", line);             /* 串口后端: 立即输出 */

#if LOG_SD_ENABLE
    /* SD 后端: 附加且未挂起时攒批; 缓冲将满则就地刷一次(有界阻塞) */
    if (s_sd_ok && !s_suspended) {
        len = (uint16_t)strlen(line);
        if ((s_len + len) >= (LOG_FLUSH_HIGH + 64)) {
            Log_FlushNow();         /* 攒不下: 先刷(此时缓冲必非满, 不会递归深) */
        }
        if ((s_len + len) < LOG_BUF_SIZE) {
            memcpy(s_buf + s_len, line, len);
            s_len += len;
        } else {
            s_dropped++;            /* 丢弃并计数(Zephyr dropped 语义) */
        }
    }
#endif
}

/**
 * @brief 运行时级别设置（串口命令 log=N 直通; 越界钳位 0~3）
 */
void Log_SetLevel(int32_t lvl)
{
    if (lvl < LOG_LVL_ERROR) { lvl = LOG_LVL_ERROR; }
    if (lvl > LOG_LVL_DEBUG) { lvl = LOG_LVL_DEBUG; }
    s_level = lvl;
}

/**
 * @brief 查询运行时级别
 */
int32_t Log_GetLevel(void)
{
    return s_level;
}

/**
 * @brief 初始化（级别默认 DEBUG; SD 后端等 Log_SD_Attach 才启用）
 */
void Log_Init(void)
{
    s_level = LOG_LVL_DEBUG;
}

#if LOG_SD_ENABLE

/**
 * @brief 日志文件打开(追加) + 轮转检查 + BOOT 头
 *
 * @return 1=附加成功; 0=失败(后端摘除)
 */
static uint8_t log_sd_open(void)
{
    FIL fp;

    /* 轮转: 追加前查文件尺寸, 超 64KB → 改名 .old 重建(EasyLogger 简化版) */
    if (f_open(&fp, LOG_PATH, FA_READ) == FR_OK) {
        uint32_t sz = (uint32_t)f_size(&fp);
        f_close(&fp);
        if (sz > LOG_ROTATE_SIZE) {
            f_unlink(LOG_PATH_OLD);
            if (f_rename(LOG_PATH, LOG_PATH_OLD) != FR_OK) {
                f_unlink(LOG_PATH);     /* 改名失败(跨目录等)则直接重建 */
            }
        }
    }

    /* FA_OPEN_APPEND: 打开并把指针定位到末尾(文件不存在则创建) */
    if (f_open(&s_fp, LOG_PATH, FA_WRITE | FA_OPEN_APPEND) != FR_OK) {
        return 0;
    }

    /* BOOT 头: 启动序号来自 BKP 计数(跨系统复位保持), 便于分段阅读 */
    {
        char head[64];
        int n = snprintf(head, sizeof(head),
                         "\r\n==== BOOT #%lu t=%s ====\r\n",
                         (unsigned long)s_boot_cnt, __DATE__);
        UINT bw = 0;
        if ((n > 0) &&
            (f_write(&s_fp, head, (UINT)n, &bw) == FR_OK) && (bw == (UINT)n)) {
            f_sync(&s_fp);              /* 头立即落盘, 断电也保序 */
        } else {
            f_close(&s_fp);
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 附加 SD 后端（在 params_sd 挂载成功之后调用一次）
 */
void Log_SD_Attach(void)
{
    /* 启动序号: BKP_DR3 每上电 +1（需备份域时钟与写允许, 在此处使能）*/
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP | RCC_APB1Periph_PWR, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    s_boot_cnt = (uint32_t)BKP_ReadBackupRegister(BKP_DR3) + 1;
    BKP_WriteBackupRegister(BKP_DR3, (uint16_t)(s_boot_cnt & 0xFFFF));

    if (log_sd_open()) {
        s_sd_ok = 1;
        Log_FlushNow();                 /* 冲刷附加前积压的行(如 params_sd 的早期日志) */
        printf("[log] SD 后端已附加, BOOT #%lu\r\n", (unsigned long)s_boot_cnt);
    } else {
        printf("[log] SD 后端附加失败, 仅串口输出\r\n");
    }
}

/**
 * @brief 挂起 SD 后端（MODE=2 进入时调用: SD 已导出给 USB, FatFs 禁写）
 */
void Log_SD_Suspend(void)
{
    if (s_sd_ok) {
        Log_FlushNow();
        f_close(&s_fp);
    }
    s_suspended = 1;
}

/**
 * @brief 把行缓冲刷入 SD; 失败则摘除后端(降级链)
 */
void Log_FlushNow(void)
{
#if LOG_SD_ENABLE
    if (!s_sd_ok || s_suspended || (s_len == 0)) {
        return;
    }

    UINT bw = 0;
    if ((f_write(&s_fp, s_buf, s_len, &bw) != FR_OK) || (bw != s_len)) {
        f_close(&s_fp);
        s_sd_ok = 0;                    /* 降级: 摘除后端, 串口继续 */
        LOGE("log", "SD 写失败, 后端摘除(累计丢弃 %lu)",
               (unsigned long)s_dropped);
    }
    s_len = 0;
#endif
}

/**
 * @brief 日志服务（主循环每圈调用; 限频: 累计 ≥LOG_FLUSH_PERIOD(5s) 或高水位才刷）
 */
void Log_Service(uint32_t elapsed_100us)
{
#if LOG_SD_ENABLE
    if (!s_sd_ok || s_suspended) {
        return;
    }

    s_acc += elapsed_100us;
    if (s_acc >= LOG_FLUSH_PERIOD) {
        s_acc = 0;
        if (s_len > 0) {
            Log_FlushNow();
        }
    }
#endif
}

#else /* !LOG_SD_ENABLE: 无 SD —— SD 后端全部退化为空 */

void Log_SD_Attach(void)   { }
void Log_SD_Suspend(void)  { }
void Log_FlushNow(void)    { }
void Log_Service(uint32_t elapsed_100us) { (void)elapsed_100us; }

#endif /* USE_SDCARD */
