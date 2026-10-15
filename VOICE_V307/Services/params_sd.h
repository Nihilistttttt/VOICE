#ifndef __PARAMS_SD_H__
#define __PARAMS_SD_H__

/*============================================================================
 * 参数 SD 持久化接口（实现见 params_sd.c, 仅 USE_SDCARD=1 时有函数体）
 *
 * 上电流程: Params_SD_Init() 一次（在 Mode_HidMouse 等入口初始化段调用,
 * 早于任何使用 g_params 的逻辑; SD 不在时静默退回默认值）。
 * 保存: Params_SD_Save() 显式触发（串口/AOA/蓝牙的"保存"命令直通）。
 * 恢复出厂: Params_SD_FactoryReset() = 默认值 + 立即落盘。
 *============================================================================*/

void    Params_SD_Init(void);           /* 上电加载(缺失自愈写模板) */
uint8_t Params_SD_Save(void);           /* 全量写回当前参数表 */
uint8_t Params_SD_FactoryReset(void);   /* 恢复出厂默认 + 落盘 */

#endif /* __PARAMS_SD_H__ */
