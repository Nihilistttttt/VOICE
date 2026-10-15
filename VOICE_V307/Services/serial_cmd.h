#ifndef __SERIAL_CMD_H__
#define __SERIAL_CMD_H__

#include <stdint.h>

/*============================================================================
 * 调试串口命令行（USART1 RXNE 轮询, 逐行分发）
 *
 * 命令表（回车结束, 无回显——终端开本地回显）:
 *   mode0 / mode2   切换模式: 写 BKP + 系统复位, 重启后进入目标模式
 *                   （注: 需双模式共存固件——USBFS 栈门控运行时化后生效,
 *                     当前版本保留接口、执行时打印提示）
 *   save            参数表写回 SD
 *   factory         恢复出厂默认值并落盘
 *   log=N           运行时日志级别 (0=E 1=W 2=I 3=D)
 *   ver             版本/启动序号/当前级别
 *   help            命令表
 *
 * 接线说明: USART1 RX=PA10 已由 Debug 层配好输入; CTRL 开关切到 CH340
 * 侧即可用 PC 串口发命令（TX 打印与 RX 命令同口）。
 *
 * 使用: 在每个模式主循环里调用 Cmd_Poll(sd_ok) —— sd_ok=0 的模式下
 * save/factory 被拒绝（U 盘模式导出 SD 时 FatFs 禁写, 双主保护）。
 *============================================================================*/

/**
 * @brief 初始化（确保 USART1 RX 可用; 幂等）
 */
void Cmd_Init(void);

/**
 * @brief 轮询串口收字节并按行分发（每个模式主循环调用一次）
 *
 * @param sd_ok 1=当前模式允许 SD 操作(0 则 save/factory 拒绝)
 */
void Cmd_Poll(uint8_t sd_ok);

#endif /* __SERIAL_CMD_H__ */
