/*============================================================================
 * 参数 SD 持久化 —— /1:/voice_config.ini 的读取与写回
 *
 * 文件说明:
 *   上电 Params_SD_Init(): SD_Init → f_mount → 读取 INI 逐行解析进参数表;
 *   文件缺失时自动写一份默认模板（自愈）; SD 不在/挂载失败 → 静默用默认
 *   值（SD 是可选件, 不阻塞进入任何模式）。
 *   Params_SD_Save(): 全量写回（显式"保存"命令触发, 不自动写——防磨损）。
 *   Params_SD_FactoryReset(): 恢复默认 + 立即落盘 = 恢复出厂（软件选项入口）。
 *
 * INI 格式: 每行 key=value; '#' 开头为注释; 键名=参数描述符表名。
 *
 * 实现注记: ffconf.h FF_USE_STRFUNC=0 → f_gets/f_printf 不可用,
 * 读取用 f_read + 自写按行解析, 写出用 f_write + snprintf 拼行。
 * 挂载点 "1:" 沿用 MODE=3 的可用先例。
 *============================================================================*/

#include "params.h"
#include "Config.h"

#if USE_SDCARD

#include "params_sd.h"
#include "log.h"
#include "sdio_sd.h"
#include "ff.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PARAMS_SD_PATH   "1:/params.ini"   /* 8.3 短名: FF_USE_LFN=0 下长名报 FR_INVALID_NAME */
#define PARAMS_BUF_SIZE  512     /* 单次读入上限; 16 参数模板约 350B, 充裕 */

static FATFS s_pfs;              /* 挂载对象（本模块专用一份） */

/**
 * @brief 解析配置文本: 逐行 name=value → Params_SetByName
 *
 * 容错: '#' 注释行/空行/无等号行跳过; 键名尾部 \r 与空格剔除;
 * 未知键名静默忽略（前向兼容: 新版固件读旧文件、旧文件多键不报错）。
 *
 * @param text 已补 '\0' 的配置文本（可被本函数就地改写）
 */
static void params_parse(char *text)
{
    char *save = text;

    while (*save != 0) {
        char *line = save;
        char *nl  = strchr(line, '\n');

        if (nl != 0) {
            *nl = 0;
            save = nl + 1;
        } else {
            save = line + strlen(line);     /* 最后一行无换行 */
        }

        if (line[0] == '#' || line[0] == 0) {
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq == 0) {
            continue;
        }
        *eq = 0;

        /* 键名尾部去 '\r'/' '（Windows 编辑器常见残留）*/
        char *e = eq;
        while (e > line && (e[-1] == '\r' || e[-1] == ' ')) {
            e--;
        }
        *e = 0;

        (void)Params_SetByName(line, atoi(eq + 1));
    }
}

/**
 * @brief 上电初始化: SD 初始化 → 挂载 → 加载配置（缺失自愈写模板）
 *
 * 任何一步失败都只打印并退回默认参数, 绝不阻塞调用方进入工作模式。
 */
void Params_SD_Init(void)
{
    int sd_err = (int)SD_Init();

    if (sd_err != 0) {
        LOGE("params", "SD Init 失败 sd_err=%d, 使用默认参数", sd_err);
        return;
    }

    /* SD 切 POLLING 模式并关闭 SDIO 中断 —— MODE=2 的实测教训:
     * SD 的 DMA 读写依赖 SDIO 中断, 而 USBFS 中断同为优先级 0 无法嵌套,
     * 会导致读/写扇区超时(写目录项/写文件全部失败且无文件产物)。
     * 轮询模式下 SD 与 USB 并存无冲突; 本模块后续的 Save/FactoryReset
     * 也都运行在此模式下。 */
    SD_SetDeviceMode(SD_POLLING_MODE);
    NVIC_DisableIRQ(SDIO_IRQn);

    FRESULT fr = f_mount(&s_pfs, "1:", 1);
    if (fr != FR_OK) {
        LOGE("params", "f_mount 失败 fr=%d, 使用默认参数", (int)fr);
        return;
    }

    FIL fp;
    fr = f_open(&fp, PARAMS_SD_PATH, FA_READ);
    if (fr != FR_OK) {
        /* 自愈: 无配置文件(FR_NO_FILE) → 写入默认模板, 供拔卡手改 */
        LOGW("params", "配置打开失败(读) fr=%d, 写入默认模板", (int)fr);
        (void)Params_SD_Save();
        return;
    }

    char buf[PARAMS_BUF_SIZE];
    UINT br = 0;

    fr = f_read(&fp, buf, sizeof(buf) - 1, &br);
    if (fr != FR_OK) {
        LOGE("params", "配置读取失败 fr=%d, 使用默认参数", (int)fr);
        f_close(&fp);
        return;
    }
    f_close(&fp);

    buf[br] = 0;
    params_parse(buf);
    LOGI("params", "配置已从 SD 加载 (%u 字节)", (unsigned)br);
}

/**
 * @brief 全量写回当前参数表到 SD（覆盖式; 显式"保存"命令触发）
 *
 * @return 1=成功; 0=打开或写入失败
 */
uint8_t Params_SD_Save(void)
{
    FIL fp;

    FRESULT fr = f_open(&fp, PARAMS_SD_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        LOGE("params", "配置文件打开失败(写) fr=%d", (int)fr);
        return 0;
    }

    char    line[64];
    UINT    bw = 0;
    uint8_t ok = 1;
    uint8_t i;

    for (i = 0; ok && (i < Params_Count()); i++) {
        const char *name = 0;
        int32_t     v    = Params_GetByIndex(i, &name);
        int         n    = snprintf(line, sizeof(line), "%s=%ld\r\n",
                                    name, (long)v);

        if ((n <= 0) || (f_write(&fp, line, (UINT)n, &bw) != FR_OK) ||
            (bw != (UINT)n)) {
            ok = 0;
        }
    }

    f_close(&fp);
    LOGI("params", "配置%s写回 SD", ok ? "已" : "未");
    return ok;
}

/**
 * @brief 恢复出厂默认值并立即落盘（"恢复出厂"软件选项的完整入口）
 *
 * @return 1=默认值已恢复且落盘成功; 0=落盘失败（运行时仍是默认值）
 */
uint8_t Params_SD_FactoryReset(void)
{
    Params_FactoryReset();
    return Params_SD_Save();
}

#endif /* USE_SDCARD */
