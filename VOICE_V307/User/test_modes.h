#ifndef __TEST_MODES_H
#define __TEST_MODES_H

/*============================================================================
 * test_modes.h —— 各 TEST_MODE 的完整实现（声明）
 *
 * main.c 只负责「怎么启动」和「选哪个模式」，不关心每个模式内部怎么跑。
 * 增/删/改一个模式只动 test_modes.c，不用碰入口。
 *
 * 命名约定：Mode_Xxx() 一一对应 Common/Config.h 里的 TEST_MODE 取值。
 * ⚠ 所有模式函数都**不会返回** —— 各自 hold 在自己的 while(1) 里，
 *   所以调用点之后的代码只在模式号非法时才可能执行。
 *============================================================================*/

void Mode_SdioBench(void);      /* TEST_MODE 1  SDIO 读写速率基准 */
void Mode_Udisk(void);          /* TEST_MODE 2  USB MSC U 盘 */
void Mode_Fatfs(void);          /* TEST_MODE 3  SD 卡 FAT 读文件 */
void Mode_UsbhsEnum(void);      /* TEST_MODE 4  USBHS Device 枚举调试 */
void Mode_HidMouse(void);       /* TEST_MODE 5  USBFS HID 摇杆鼠标 */
void Mode_PhoneControl(void);   /* TEST_MODE 0  正式功能：吹气采集 + AOA 手机控制 */

#endif /* __TEST_MODES_H */
