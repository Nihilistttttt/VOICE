/*============================================================================
 * 调试串口命令行实现
 *
 * 借鉴: Zephyr Shell 的"串口即控制台"思想与 ulog 的动态过滤命令,
 * 裁剪成无行编辑/无自动补全的 8 条平命令——资源受限下够用为先。
 *
 * 模式切换机制: 串口命令写 BKP 备份寄存器(魔数+目标模式)后系统复位,
 * main.c 启动时读 BKP 优先于编译期 TEST_MODE。BKP 跨系统复位保持、
 * 断电清零(无 VBAT 电池)——即"切换"在本次上电会话内持久, 拔电回落
 * 到编译期默认。USBFS 栈的运行时化(HID/MSC 双模式共存)是前置条件,
 * 未完成前 mode 命令只打印说明。
 *============================================================================*/

#include "serial_cmd.h"
#include "log.h"
#include "params.h"
#include "Config.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

#if USE_SDCARD
#include "params_sd.h"
#define CMD_SD_OK   1
#else
#define CMD_SD_OK   0
#endif

/* ---- BKP 布局: DR1=魔数(0xA55A=有效) DR2=目标模式 ------------------------*/
#define CMD_BKP_MAGIC       1
#define CMD_BKP_MODE        2
#define CMD_SWITCH_MAGIC    0xA55A

#define CMD_LINE_SIZE       16      /* 命令都很短, 16 字节够 */

static char    s_line[CMD_LINE_SIZE];
static uint8_t s_len = 0;

/* 前置声明 */
static void cmd_dispatch(char *line, uint8_t sd_ok);

/**
 * @brief 初始化（PA10 RX 与 USART1 已由 Debug 层配置, 此处兜底再配一次;
 *        幂等, 重复调用无害）
 */
void Cmd_Init(void)
{
    GPIO_ClockEnable(0);    /* GPIOA —— USART1 RX(PA10) 时钟兜底 */
}

/**
 * @brief 命令分发
 *
 * @param line  已去换行的命令行
 * @param sd_ok 当前模式是否允许 SD 操作
 */
static void cmd_dispatch(char *line, uint8_t sd_ok)
{
    if ((strcmp(line, "mode0") == 0) || (strcmp(line, "mode2") == 0)) {
        /* 模式切换: BKP 记录目标 + 复位。USBFS 栈运行时化(HID/MSC 共存)
         * 完成前, 该命令只写标记——重启后若固件未含目标模式仍走编译期
         * 默认（未来双模式固件直接生效）。 */
        uint8_t mode = (line[4] == '0') ? 0 : 2;
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_BKP | RCC_APB1Periph_PWR, ENABLE);
        PWR_BackupAccessCmd(ENABLE);
        BKP_WriteBackupRegister(CMD_BKP_MAGIC, CMD_SWITCH_MAGIC);
        BKP_WriteBackupRegister(CMD_BKP_MODE, mode);
        LOGW("cmd", "重启进入 MODE=%u ...", mode);
        Log_FlushNow();
        Delay_Ms(100);
        NVIC_SystemReset();
    } else if (strcmp(line, "save") == 0) {
#if USE_SDCARD
        if (!sd_ok) {
            LOGE("cmd", "当前模式禁写 SD(U盘导出中)");
        } else {
            (void)Params_SD_Save();
        }
#else
        LOGE("cmd", "未启用 SD");
#endif
    } else if (strcmp(line, "factory") == 0) {
#if USE_SDCARD
        if (!sd_ok) {
            LOGE("cmd", "当前模式禁写 SD(U盘导出中)");
        } else {
            (void)Params_SD_FactoryReset();
            LOGI("cmd", "已恢复出厂默认值");
        }
#else
        LOGE("cmd", "未启用 SD");
#endif
    } else if (strncmp(line, "log=", 4) == 0) {
        Log_SetLevel(atoi(line + 4));
        LOGI("cmd", "日志级别=%ld", (long)Log_GetLevel());
    } else if (strcmp(line, "ver") == 0) {
        LOGI("cmd", "VOICE " __DATE__ " " __TIME__);
    } else if (strcmp(line, "help") == 0) {
        LOGI("cmd", "mode0/mode2/save/factory/log=N/ver/help");
    } else if (line[0] != 0) {
        LOGW("cmd", "未知命令, help 查看命令表");
    }
}

/**
 * @brief 轮询串口收字节并按行分发（每个模式主循环调用一次）
 *
 * @param sd_ok 1=当前模式允许 SD 操作
 */
void Cmd_Poll(uint8_t sd_ok)
{
    /* 命令处理期间若有新字节到达, RXNE 置位会保留到下一轮轮询,
     * 2M 波特率下连续字符靠硬件接收——超 16 字节的行按截断处理 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) {
        char c = (char)USART_ReceiveData(USART1);

        if ((c == '\r') || (c == '\n')) {
            s_line[s_len] = 0;
            cmd_dispatch(s_line, sd_ok);
            s_len = 0;
        } else if (s_len < (CMD_LINE_SIZE - 1)) {
            s_line[s_len++] = c;
        } else {
            s_len = 0;              /* 超长行丢弃重来 */
            LOGW("cmd", "命令过长");
        }
    }
}
