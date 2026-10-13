#ifndef __MODE_H__
#define __MODE_H__

/* ====== 模式开关 (main.c与按模式条件编译的库文件共用) ======
 * 0 = 正常运行 (麦克风采集 + USB AOA 手机控制)
 * 1 = SDIO SD卡测试 (初始化SD卡 + 读写校验)
 * 2 = USB MSC U盘模式 (SD卡经USBFS Device模式暴露给电脑)
 * 3 = SD卡FAT文件读取 (读123.txt打印到串口+OLED)
 * 4 = USBHS Device枚举调试 (需usbhs_dev.h里USBHS_DEBUG_EN=1)
 * 5 = USBFS HID键鼠模式 (手机OTG, 像鼠标一样控制手机)
 */
#define TEST_MODE           2

#endif