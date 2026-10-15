#ifndef __CONFIG_H
#define __CONFIG_H

#include "ch32v30x.h"
#include "hal_gpio.h"

/*============================================================================
 * 固件模式选择（唯一真源）
 *   0 = 正式功能(压差手势+手机控制)   1 = SDIO 基准   2 = U 盘   3 = FatFs 读
 *   4 = USBHS 枚举调试   5 = 旧版V2(麦克风吹气+AOA)
 * ★ 只影响 .text 大小，不影响 RAM 数量级。引用体积数字时必须标注是哪次构建。
 *============================================================================*/
#define TEST_MODE           2

/*============================================================================
 * MODE=5 附加开关：是否必须与手机(USB 主机)取得通信
 *   1 = 正常 HID 鼠标用途：枚举后才打印摇杆/压差读数
 *   0 = 脱机测试：不插手机也持续打印摇杆/压差读数（只影响打印行为；
 *       鼠标上报仍然只在枚举后发送 —— 没有主机就没处发）
 *============================================================================*/
#define HID_REQUIRE_PHONE   1

/*---- ADS1115 排查开关（A/B 对比用）-----------------------------------
 * ADS1115_I2C_HARDWARE: 1=硬件 I2C2 / 0=软件 bit-bang（枚举模式实测能读数）
 *   脱机读数全 0 的 A/B：软件版有数 = 硬件 I2C2 是原因；
 *   软件版也全 0 = 原因不在 I2C（往供电/接线/传感器找）。
 * ADS1115_DEBUG: 1=初始化/读写每步失败打串口（含行号），
 *   ReadChannel 打印原始计数值与 OS 位状态。
 *-------------------------------------------------------------------*/
#define ADS1115_I2C_HARDWARE  1
#define ADS1115_DEBUG         1

/*---- 回主屏方式（s2 短吸触发）---------------------------------------
 * HOME_MODE_CLICKNAV = 点击底部正中主页键（P20/EMUI 适配，当前方案）
 *   原理: 指针冲到左下角(边界钳位) → 右移半屏宽 → 点击主页键
 * HOME_MODE_KEYBOARD = 键盘三连（旧方案保留: Alt+Tab→Esc→Home）
 *   注: EMUI 在应用内不响应键盘 Home 键码（ROM 分发策略），故退役*/
#define HOME_MODE_KEYBOARD  0
#define HOME_MODE_CLICKNAV  1
#define HOME_MODE           HOME_MODE_CLICKNAV

/*---- ALERT/RDY 转换完成中断（实测本板不触发，禁用）-------------------
 * 实测: 传感器 ALERT 引脚在单次转换完成时不拉低（135 次全 lvl=1），
 * 原因未定（RDY 模式行为与手册不符/引脚映射存疑）。禁用后直接走
 * OS 位轮询（1.4ms/读）。若换板/换批次传感器可改 1 重新启用。*/
#define ADS1115_ALERT_ENABLE  0

/*============================================================================
 * 板型选择
 *   V2 = INMP441 麦克风吹气检测（I2S2/I2S3 双通道）
 *   V3 = 压差传感器吹气检测（片内 ADC），取代麦克风
 *============================================================================*/
#if !defined(BOARD_V2) && !defined(BOARD_V3)
#define BOARD_V3
#endif

/*============================================================================
 * 第 1 层 · 板级能力（BOARD_HAS_*）
 *
 *   回答的问题是：**这块板接了什么**。判定依据是 hardware/电路.txt 网表。
 *   有 = 1，没有 = 0 —— 两个值都写出来，不要用"省略"表达"没有"，
 *   这样 grep 一次就能看到全貌。
 *
 *   不同板型可以有相同的外设（V2/V3 都有 OLED），各写各的即可。
 *   加新板型：在这里加一个分支填 6 个值，下面两层不用动。
 *============================================================================*/
#if defined(BOARD_V2)          /* INMP441 麦克风板：PB5 一直上电，无供电控制电路 */
#define BOARD_HAS_MIC_HUM    1
#define BOARD_HAS_OLED       1
#define BOARD_HAS_ADS1115    0
#define BOARD_HAS_JOYSTICK   0
#define BOARD_HAS_PRESSURE   0
#define BOARD_HAS_AOA_PWR    0
#elif defined(BOARD_V3)        /* 压差传感器板：PB5 经 P-MOS 控制 */
#define BOARD_HAS_MIC_HUM    0
#define BOARD_HAS_OLED       1
#define BOARD_HAS_ADS1115    1
#define BOARD_HAS_JOYSTICK   1
#define BOARD_HAS_PRESSURE   1
#define BOARD_HAS_AOA_PWR    1
#else
#error "未知板型：请定义 BOARD_V2 或 BOARD_V3"
#endif

/*============================================================================
 * 第 2 层 · 特性开关 ★★ 平时要改的就是这一段 ★★
 *
 *   想把某个外设编进固件写 1，不编写 0。改完立即生效：
 *   对应的驱动代码、RAM 缓冲区、引脚宏全部退出编译。
 *
 *   【举例】不想用 OLED 了：
 *       #define USE_OLED        0
 *   【换板子】把 ① 的板型改掉后，回来核对这一段 ——
 *       行尾的 [板:xx] 标注了每块板的出厂值，开一个板子上没有的外设
 *       会触发下方 #error。
 *============================================================================*/
#define USE_MIC_HUM     0      /* INMP441 麦克风       [板:V2=1 V3=0] */
#define USE_OLED        1    /* OLED 显示屏         [板:V2=1 V3=1] */
#define USE_ADS1115     1    /* ADS1115 ADC         [板:V2=0 V3=1] */
#define USE_JOYSTICK    1    /* 摇杆                [板:V2=0 V3=1] */
#define USE_PRESSURE    1    /* 压差传感器          [板:V2=0 V3=1] */
#define USE_AOA_PWR     1    /* 手机供电控制        [板:V2=0 V3=1] */

/*---- 一致性检查：特性开关不能超出板级能力 -------------------------------*/
#if USE_MIC_HUM && !BOARD_HAS_MIC_HUM
#error "USE_MIC_HUM=1，但当前板型没有麦克风（V3 用压差传感器，见 BOARD_V2/V3）"
#endif
#if USE_ADS1115 && !BOARD_HAS_ADS1115
#error "USE_ADS1115=1，但当前板型没有 ADS1115"
#endif
#if USE_JOYSTICK && !BOARD_HAS_JOYSTICK
#error "USE_JOYSTICK=1，但当前板型没有摇杆"
#endif
#if USE_PRESSURE && !BOARD_HAS_PRESSURE
#error "USE_PRESSURE=1，但当前板型没有压差传感器"
#endif
#if USE_AOA_PWR && !BOARD_HAS_AOA_PWR
#error "USE_AOA_PWR=1，但当前板型没有手机供电电路"
#endif

/*============================================================================
 * 第 3 层 · 接线：每个外设的引脚
 *
 *   两块板相同的脚只定义一次；只有真正不同的（OLED CS）按板分支。
 *   每块都被 #if USE_X 包住 —— 特性关了，引脚宏也随之消失；
 *   对应的驱动若没一起关，编译会立刻报"未声明的标识符"。
 *============================================================================*/

/*---- OLED SSD1306（V2/V3 都有，仅 CS 不同）-------------------------------
 *   SCK=PA5(SPI1)  MOSI=PA7(SPI1)  RES=PC4  DC=PC5
 *   CS: V2=PB0  V3=PA6    依据 hardware/电路.txt U12 */
#if USE_OLED
#define OLED_RES_PORT           2
#define OLED_RES_PIN            4
#define OLED_RES_PIN_ENC        HAL_GPIO_PIN(OLED_RES_PORT, OLED_RES_PIN)
#define OLED_DC_PORT            2
#define OLED_DC_PIN             5
#define OLED_DC_PIN_ENC         HAL_GPIO_PIN(OLED_DC_PORT, OLED_DC_PIN)
#define OLED_SCK_PORT           0
#define OLED_SCK_PIN            5
#define OLED_SCK_PIN_ENC        HAL_GPIO_PIN(OLED_SCK_PORT, OLED_SCK_PIN)
#define OLED_MOSI_PORT          0
#define OLED_MOSI_PIN           7
#define OLED_MOSI_PIN_ENC       HAL_GPIO_PIN(OLED_MOSI_PORT, OLED_MOSI_PIN)
#define OLED_SPI_INSTANCE       SPI1
#if defined(BOARD_V2)
#define OLED_CS_PORT            1
#define OLED_CS_PIN             0
#elif defined(BOARD_V3)
#define OLED_CS_PORT            0
#define OLED_CS_PIN             6
#endif
#define OLED_CS_PIN_ENC         HAL_GPIO_PIN(OLED_CS_PORT, OLED_CS_PIN)
#endif /* USE_OLED */

/*---- INMP441 麦克风 A/B（仅 V2）------------------------------------------
 *   A: WS=PB12  CK=PB13  SD=PB15   SPI2_RX -> DMA1_Channel4
 *   B: WS=PA15  CK=PB3   SD=PB5    SPI3_RX -> DMA2_Channel1
 *   需禁用 JTAG 保留 SWD，释放 PA15/PB3 */
#if USE_MIC_HUM
#define I2S2_WS_PORT            1
#define I2S2_WS_PIN             12
#define I2S2_WS_PIN_ENC         HAL_GPIO_PIN(I2S2_WS_PORT, I2S2_WS_PIN)
#define I2S2_CK_PORT            1
#define I2S2_CK_PIN             13
#define I2S2_CK_PIN_ENC         HAL_GPIO_PIN(I2S2_CK_PORT, I2S2_CK_PIN)
#define I2S2_SD_PORT            1
#define I2S2_SD_PIN             15
#define I2S2_SD_PIN_ENC         HAL_GPIO_PIN(I2S2_SD_PORT, I2S2_SD_PIN)
#define I2S2_GPIO_AF            0
#define I2S2_SPI_INSTANCE       SPI2
#define I2S2_DMA_CHANNEL        DMA1_Channel4
#define I2S2_DMA_FLAG_TC        DMA1_FLAG_TC4
#define I2S2_DMA_FLAG_HT        DMA1_FLAG_HT4
#define I2S2_DMA_IRQn           DMA1_Channel4_IRQn
#define I2S3_WS_PORT            0
#define I2S3_WS_PIN             15
#define I2S3_WS_PIN_ENC         HAL_GPIO_PIN(I2S3_WS_PORT, I2S3_WS_PIN)
#define I2S3_CK_PORT            1
#define I2S3_CK_PIN             3
#define I2S3_CK_PIN_ENC         HAL_GPIO_PIN(I2S3_CK_PORT, I2S3_CK_PIN)
#define I2S3_SD_PORT            1
#define I2S3_SD_PIN             5
#define I2S3_SD_PIN_ENC         HAL_GPIO_PIN(I2S3_SD_PORT, I2S3_SD_PIN)
#define I2S3_GPIO_AF            0
#define I2S3_SPI_INSTANCE       SPI3
#define I2S3_DMA_CHANNEL        DMA2_Channel1
#define I2S3_DMA_FLAG_TC        DMA2_FLAG_TC1
#define I2S3_DMA_FLAG_HT        DMA2_FLAG_HT1
#define I2S3_DMA_IRQn           DMA2_Channel1_IRQn
#endif /* USE_MIC_HUM */

/*---- ADS1115 16 位 I2C ADC（仅 V3）---------------------------------------
 *   SCL=PB10  SDA=PB11  ALERT/RDY=PE15
 *   AIN0=压差传感器2  AIN1=压差传感器1  AIN2=摇杆X  AIN3=摇杆Y */
#if USE_ADS1115
#define ADS1115_SCL_PORT        1
#define ADS1115_SCL_PIN         10
#define ADS1115_SCL_PIN_ENC     HAL_GPIO_PIN(ADS1115_SCL_PORT, ADS1115_SCL_PIN)
#define ADS1115_SDA_PORT        1
#define ADS1115_SDA_PIN         11
#define ADS1115_SDA_PIN_ENC     HAL_GPIO_PIN(ADS1115_SDA_PORT, ADS1115_SDA_PIN)
#define ADS1115_ALERT_PORT      4
#define ADS1115_ALERT_PIN       15
#define ADS1115_ALERT_PIN_ENC   HAL_GPIO_PIN(ADS1115_ALERT_PORT, ADS1115_ALERT_PIN)
#define ADS1115_I2C_ADDR        0x48
#define ADS1115_I2C_INSTANCE    I2C2   /* PB10/PB11 = I2C2 默认引脚映射，无需重映射 */
#endif /* USE_ADS1115 */

/*---- 摇杆 YTMR-YG13-006（仅 V3）------------------------------------------
 *   X轴→ADS1115 AIN2  Y轴→ADS1115 AIN3  按键=PE14 */
#if USE_JOYSTICK
#define JOYSTICK_KEY_PORT       4
#define JOYSTICK_KEY_PIN        14
#define JOYSTICK_KEY_PIN_ENC    HAL_GPIO_PIN(JOYSTICK_KEY_PORT, JOYSTICK_KEY_PIN)
#define JOYSTICK_X_CHANNEL      2
#define JOYSTICK_Y_CHANNEL      3
#endif /* USE_JOYSTICK */

/*---- 压差传感器片内 ADC（仅 V3，取代麦克风）------------------------------
 *   传感器1→运放→PB0/ADC8  传感器2→运放→PB1/ADC9 */
#if USE_PRESSURE
#define PRESSURE_ADC1_CH        8
#define PRESSURE_ADC2_CH        9
#endif /* USE_PRESSURE */

/*---- AOA 手机供电（PB5）--------------------------------------------------
 *   链路: PB5 → R12(4.7k) → Q3(NPN) → Q2(P-MOS) → VPHONE
 *   拉高 = 给手机供电；拉低 = 断电（断联后强制重置手机 USB 状态）
 *   ★ 这不是板载 LED，板载 LED 在 PB8（经 R52）。依据：hardware/电路.txt 网表 */
#if USE_AOA_PWR
#define AOA_PWR_PORT            1
#define AOA_PWR_PIN             5
#define AOA_PWR_PIN_ENC         HAL_GPIO_PIN(AOA_PWR_PORT, AOA_PWR_PIN)
#endif /* USE_AOA_PWR */

/*============================================================================
 * 外设时钟使能声明
 *   GPIO_ClockEnable  -> hal_gpio.c (声明在 hal_gpio.h)
 *   SPI/DMA/AFIO       -> Config.c
 * 集中声明的原因：分散时漏开时钟的表现是「引脚完全没反应，但编译没有任何警告」。
 *============================================================================*/
void SPI_ClockEnable(SPI_TypeDef *SPIx);
void I2C_ClockEnable(I2C_TypeDef *I2Cx);
void USART_ClockEnable(USART_TypeDef *USARTx);
void DMA_ClockEnable(DMA_TypeDef *DMAx);
void AFIO_ClockEnable(void);

#endif /* __CONFIG_H */
