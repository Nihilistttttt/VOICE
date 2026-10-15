#include "ads1115.h"
#include "Config.h"
#include "debug.h"

#if USE_ADS1115

/*============================================================================
 * 传输层 A/B 开关（Config.h 定义）
 *   ADS1115_I2C_HARDWARE = 1  硬件 I2C2（PB10/PB11 默认映射）
 *                          0  软件 bit-bang（同一组引脚，Delay_Us 掐时序）
 *   两者 A/B 对比用于排查"脱机模式下全部读 0"的问题（已闭环: 硬件版
 *   400k I2C2 转正并实测通过, 软件版留作 A/B 备份）。
 *
 * ADS1115_DEBUG = 1 时，初始化/读写的每一步失败都会打串口（含行号），
 * ReadChannel 会打印原始计数值 —— 用于定位失败发生在哪一步。
 *============================================================================*/

#if ADS1115_DEBUG
#define ADS_DBG(...)   printf(__VA_ARGS__)
#define ADS_FAIL()     printf("[ADS1115] op fail @line %d\r\n", __LINE__)
#else
#define ADS_DBG(...)
#define ADS_FAIL()
#endif

/*============================================================================
 * 总线故障带外通道（审计 F2）
 *   读失败返回 0 与"真实 0 计数"不可区分 → 总线死掉时上层只看到
 *   "无压力"却无任何异常。这里补带外状态: 连续失败计数 + 总线失效
 *   锁存（跨阈值一次性打印, 恢复时也打印）。上层用 ADS1115_BusDead()
 *   查询做可感知降级（OLED/串口指示等）。
 *   注: 转换寄存器读数被破坏无 CRC 不可检, 本通道只覆盖"写配置失败"
 *   与"OS 位轮询超时"两类可检失败 —— 足以覆盖总线断线/器件掉电。
 *============================================================================*/
#define ADS1115_FAIL_DEAD_TH   20      /* 连续失败 N 次 → 判总线失效 */

static uint16_t s_fail_streak = 0;     /* 连续失败计数（成功清零）*/
static uint8_t  s_bus_dead = 0;        /* 总线失效锁存 */

static void ads_note_fail(void)
{
    if (++s_fail_streak >= ADS1115_FAIL_DEAD_TH && !s_bus_dead) {
        s_bus_dead = 1;
        printf("[ADS1115] !! BUS DEAD: %u consecutive fails, readings invalid\r\n",
               (unsigned)s_fail_streak);
    }
}

static void ads_note_ok(void)
{
    if (s_bus_dead) {
        s_bus_dead = 0;
        printf("[ADS1115] bus recovered\r\n");
    }
    s_fail_streak = 0;
}

uint8_t  ADS1115_BusDead(void)    { return s_bus_dead; }
uint16_t ADS1115_FailStreak(void) { return s_fail_streak; }

/*============================================================================
 * ALERT/RDY(PE15) 转换完成中断 —— 走 hal_gpio 的 EXTI 回调分发表
 * （此前"EXTI15_10 被 hal_gpio 占用会撞车"的说法有误: 该模块本就是
 *   按线回调分发表,注册即用,无冲突）
 * ISR 只置标志,I2C 读取全部在主循环 —— 嵌入式原则: ISR 最小化
 *============================================================================*/
#if ADS1115_ALERT_ENABLE
static volatile uint8_t s_alert_flag = 0;
static void ads_alert_isr(void) { s_alert_flag = 1; }
#endif

/*---------------------------------------------------------------------------
 * 传输层 A：硬件 I2C2
 *---------------------------------------------------------------------------*/
#if ADS1115_I2C_HARDWARE

#define ADS1115_I2C_SPEED      400000   /* 上拉 2.2k，ADS1115 规格支持 400k */
#define ADS1115_I2C_TIMEOUT    20000    /* 事件等待循环上限 */

static uint8_t i2c_wait_event(uint32_t event)
{
    uint32_t t = ADS1115_I2C_TIMEOUT;
    while (!I2C_CheckEvent(ADS1115_I2C_INSTANCE, event)) {
        if (--t == 0) return 1;
    }
    return 0;
}

static uint8_t i2c_wait_flag(uint32_t flag, FlagStatus state)
{
    uint32_t t = ADS1115_I2C_TIMEOUT;
    while (I2C_GetFlagStatus(ADS1115_I2C_INSTANCE, flag) == state) {
        if (--t == 0) return 1;
    }
    return 0;
}

static void i2c_bus_release(void)
{
    I2C_GenerateSTOP(ADS1115_I2C_INSTANCE, ENABLE);
    I2C_Cmd(ADS1115_I2C_INSTANCE, ENABLE);   /* 读 SR1+写 CR1 顺带清 STOPF */
}

static void transport_init(void)
{
    /* 硬件 I2C 引脚 = 复用开漏（开漏 + 外部上拉 2.2k 构成总线）*/
    Hal_GPIO_Init(ADS1115_SCL_PIN_ENC, HAL_GPIO_MODE_AF_OD, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(ADS1115_SDA_PIN_ENC, HAL_GPIO_MODE_AF_OD, HAL_GPIO_SPEED_HIGH, 0);

    I2C_ClockEnable(ADS1115_I2C_INSTANCE);

    I2C_InitTypeDef I2C_InitStructure = {0};
    I2C_InitStructure.I2C_ClockSpeed          = ADS1115_I2C_SPEED;
    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_16_9;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(ADS1115_I2C_INSTANCE, &I2C_InitStructure);

    I2C_Cmd(ADS1115_I2C_INSTANCE, ENABLE);
    ADS_DBG("[ADS1115] transport = hardware I2C2 @%ukHz\r\n", ADS1115_I2C_SPEED / 1000);
}

uint8_t ADS1115_WriteReg(uint8_t reg, uint16_t value)
{
    /* 写: START → 器件地址(W) → 寄存器指针 → 16 位数据(高在前) → STOP */
    I2C_GenerateSTART(ADS1115_I2C_INSTANCE, ENABLE);
    if (i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT))               { ADS_FAIL(); i2c_bus_release(); return 1; }

    I2C_Send7bitAddress(ADS1115_I2C_INSTANCE, ADS1115_I2C_ADDR << 1, I2C_Direction_Transmitter);
    if (i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { ADS_FAIL(); i2c_bus_release(); return 1; }

    I2C_SendData(ADS1115_I2C_INSTANCE, reg);
    if (i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))          { ADS_FAIL(); i2c_bus_release(); return 1; }

    I2C_SendData(ADS1115_I2C_INSTANCE, (uint8_t)(value >> 8));
    if (i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))          { ADS_FAIL(); i2c_bus_release(); return 1; }

    I2C_SendData(ADS1115_I2C_INSTANCE, (uint8_t)(value & 0xFF));
    if (i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))          { ADS_FAIL(); i2c_bus_release(); return 1; }

    I2C_GenerateSTOP(ADS1115_I2C_INSTANCE, ENABLE);
    I2C_Cmd(ADS1115_I2C_INSTANCE, ENABLE);
    return 0;
}

uint16_t ADS1115_ReadReg(uint8_t reg)
{
    uint8_t  hi, lo;

    /* 先把寄存器指针写到器件（写事务）*/
    I2C_GenerateSTART(ADS1115_I2C_INSTANCE, ENABLE);
    if (i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT))               { ADS_FAIL(); i2c_bus_release(); return 0; }

    I2C_Send7bitAddress(ADS1115_I2C_INSTANCE, ADS1115_I2C_ADDR << 1, I2C_Direction_Transmitter);
    if (i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { ADS_FAIL(); i2c_bus_release(); return 0; }

    I2C_SendData(ADS1115_I2C_INSTANCE, reg);
    if (i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED))          { ADS_FAIL(); i2c_bus_release(); return 0; }

    /* 重复 START，转入读方向 */
    I2C_GenerateSTART(ADS1115_I2C_INSTANCE, ENABLE);
    if (i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT))               { ADS_FAIL(); i2c_bus_release(); return 0; }

    I2C_Send7bitAddress(ADS1115_I2C_INSTANCE, ADS1115_I2C_ADDR << 1, I2C_Direction_Receiver);
    if (i2c_wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))    { ADS_FAIL(); i2c_bus_release(); return 0; }

    /* 2 字节接收: b1 正常 ACK 收走 → 关 ACK + 编程 STOP → 读 b2。
     * ★ ACK=0 必须在 b1 收走之后再设 —— ADDR 后立即关 ACK 会让 NACK
     *   提前到 b1 生效, 传感器收到 NACK 后不再发 b2
     *   （实测 612 次失败 + BTF 变体 365 次失败, 全部死于此）。 */
    if (i2c_wait_flag(I2C_FLAG_RXNE, RESET))                        { ADS_FAIL(); i2c_bus_release(); return 0; }
    hi = I2C_ReceiveData(ADS1115_I2C_INSTANCE);

    I2C_AcknowledgeConfig(ADS1115_I2C_INSTANCE, DISABLE);
    I2C_GenerateSTOP(ADS1115_I2C_INSTANCE, ENABLE);

    if (i2c_wait_flag(I2C_FLAG_RXNE, RESET))                        { ADS_FAIL(); I2C_AcknowledgeConfig(ADS1115_I2C_INSTANCE, ENABLE); return 0; }
    lo = I2C_ReceiveData(ADS1115_I2C_INSTANCE);

    I2C_AcknowledgeConfig(ADS1115_I2C_INSTANCE, ENABLE);
    I2C_Cmd(ADS1115_I2C_INSTANCE, ENABLE);            /* 清 STOPF，为下次事务做准备 */

    return (uint16_t)hi << 8 | lo;
}

/*---------------------------------------------------------------------------
 * 传输层 B：软件 bit-bang（枚举模式下已实测能读到数据）
 *---------------------------------------------------------------------------*/
#else

static void I2C_Delay(void)          { Delay_Us(5); }
static void I2C_SDA_High(void)       { Hal_GPIO_Set(ADS1115_SDA_PIN_ENC); }
static void I2C_SDA_Low(void)        { Hal_GPIO_Reset(ADS1115_SDA_PIN_ENC); }
static void I2C_SCL_High(void)       { Hal_GPIO_Set(ADS1115_SCL_PIN_ENC); }
static void I2C_SCL_Low(void)        { Hal_GPIO_Reset(ADS1115_SCL_PIN_ENC); }
static uint8_t I2C_SDA_Read(void)    { return Hal_GPIO_Read(ADS1115_SDA_PIN_ENC); }

static void I2C_Start(void)
{
    I2C_SDA_High();
    I2C_SCL_High();
    I2C_Delay();
    I2C_SDA_Low();
    I2C_Delay();
    I2C_SCL_Low();
    I2C_Delay();
}

static void I2C_Stop(void)
{
    I2C_SDA_Low();
    I2C_SCL_High();
    I2C_Delay();
    I2C_SDA_High();
    I2C_Delay();
}

static uint8_t I2C_WriteByte(uint8_t data)
{
    uint8_t i, ack;

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80) I2C_SDA_High(); else I2C_SDA_Low();
        data <<= 1;
        I2C_Delay();
        I2C_SCL_High();
        I2C_Delay();
        I2C_SCL_Low();
    }

    I2C_SDA_High();
    I2C_Delay();
    I2C_SCL_High();
    I2C_Delay();
    ack = I2C_SDA_Read();
    I2C_SCL_Low();
    I2C_Delay();

    return ack;
}

static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t i, data = 0;

    I2C_SDA_High();
    for (i = 0; i < 8; i++)
    {
        I2C_SCL_High();
        I2C_Delay();
        data <<= 1;
        if (I2C_SDA_Read()) data |= 1;
        I2C_SCL_Low();
        I2C_Delay();
    }

    if (ack) I2C_SDA_Low(); else I2C_SDA_High();
    I2C_Delay();
    I2C_SCL_High();
    I2C_Delay();
    I2C_SCL_Low();
    I2C_Delay();

    return data;
}

static void transport_init(void)
{
    Hal_GPIO_Init(ADS1115_SCL_PIN_ENC, HAL_GPIO_MODE_OUTPUT_OD, HAL_GPIO_SPEED_HIGH, 0);
    Hal_GPIO_Init(ADS1115_SDA_PIN_ENC, HAL_GPIO_MODE_OUTPUT_OD, HAL_GPIO_SPEED_HIGH, 0);
    I2C_SCL_High();
    I2C_SDA_High();
    ADS_DBG("[ADS1115] transport = software bit-bang\r\n");
}

uint8_t ADS1115_WriteReg(uint8_t reg, uint16_t value)
{
    I2C_Start();
    if (I2C_WriteByte(ADS1115_I2C_ADDR << 1)) { I2C_Stop(); ADS_FAIL(); return 1; }
    if (I2C_WriteByte(reg))                   { I2C_Stop(); ADS_FAIL(); return 1; }
    if (I2C_WriteByte((uint8_t)(value >> 8))) { I2C_Stop(); ADS_FAIL(); return 1; }
    if (I2C_WriteByte((uint8_t)(value & 0xFF))){ I2C_Stop(); ADS_FAIL(); return 1; }
    I2C_Stop();
    return 0;
}

uint16_t ADS1115_ReadReg(uint8_t reg)
{
    uint16_t value;

    I2C_Start();
    if (I2C_WriteByte(ADS1115_I2C_ADDR << 1)) { I2C_Stop(); ADS_FAIL(); return 0; }
    if (I2C_WriteByte(reg))                   { I2C_Stop(); ADS_FAIL(); return 0; }

    I2C_Start();
    if (I2C_WriteByte((ADS1115_I2C_ADDR << 1) | 1)) { I2C_Stop(); ADS_FAIL(); return 0; }
    value = (uint16_t)I2C_ReadByte(1) << 8;
    value |= I2C_ReadByte(0);
    I2C_Stop();

    return value;
}

#endif /* ADS1115_I2C_HARDWARE */

/*============================================================================
 * 协议层（与传输层无关，两种传输共用）
 *============================================================================*/
void ADS1115_Init(void)
{
    GPIO_ClockEnable(ADS1115_SCL_PORT);
    GPIO_ClockEnable(ADS1115_SDA_PORT);
    GPIO_ClockEnable(ADS1115_ALERT_PORT);

    transport_init();

    Hal_GPIO_Init(ADS1115_ALERT_PIN_ENC, HAL_GPIO_MODE_INPUT_PU, HAL_GPIO_SPEED_HIGH, 0);
#if ADS1115_ALERT_ENABLE
    /* ALERT/RDY: 转换完成拉低(RDY 模式为上电默认), 下降沿置完成标志 */
    Hal_GPIO_IRQ_Config(ADS1115_ALERT_PIN_ENC, HAL_GPIO_IRQ_FALLING, 1, 3, ads_alert_isr);
#endif

#if ADS1115_ALERT_ENABLE
    /* 官方转换就绪配置（TI 手册）: 阈值寄存器显式写默认值启用 RDY 模式。
     * 此前依赖上电默认值未显式写入 —— 若该单元默认值不同则 RDY 不启用。 */
    ADS1115_WriteReg(ADS1115_REG_LO_THRESH, 0x8000);
    ADS1115_WriteReg(ADS1115_REG_HI_THRESH, 0x7FFF);
#endif
}

int16_t ADS1115_ReadChannel(uint8_t channel)
{
    uint16_t mux;
    uint16_t config;
    uint16_t cfg_read = 0;
    int16_t  raw;

    switch (channel)
    {
        case 0: mux = ADS1115_MUX_AIN0_GND; break;
        case 1: mux = ADS1115_MUX_AIN1_GND; break;
        case 2: mux = ADS1115_MUX_AIN2_GND; break;
        case 3: mux = ADS1115_MUX_AIN3_GND; break;
        default: return 0;
    }

    config = ADS1115_OS_START | mux | ADS1115_PGA_6_144V |
             ADS1115_MODE_SINGLE | ADS1115_DR_475SPS;

    if (ADS1115_WriteReg(ADS1115_REG_CONFIG, config)) {
        ADS_DBG("[ADS1115] ch%d: config write failed\r\n", channel);
        ads_note_fail();
        return 0;
    }

    uint16_t poll = 0;

#if ADS1115_ALERT_ENABLE
    /* 等转换完成: ALERT/RDY 下降沿中断置标志（475SPS 约 2.1ms）。
     * 上限 15ms 防挂死；超时回退为 OS 位轮询（兼容 ALERT 未接）。 */
    s_alert_flag = 0;
    while (!s_alert_flag && ++poll < 100) {
        Delay_Us(150);
    }
    if (!s_alert_flag) {
        ADS_DBG("[ADS1115] ch%d: ALERT timeout, fallback OS poll\r\n", channel);
        do {
            Delay_Us(200);
            cfg_read = ADS1115_ReadReg(ADS1115_REG_CONFIG);
        } while ((cfg_read & 0x8000) == 0 && ++poll < 160);
    }
    raw = (int16_t)ADS1115_ReadReg(ADS1115_REG_CONVERSION);

#if ADS1115_DEBUG
    if (!s_alert_flag)
        ADS_DBG("[ADS1115] ch%d: ALERT timeout lvl=%d cfg=%04X raw=%d\r\n",
                channel, Hal_GPIO_Read(ADS1115_ALERT_PIN_ENC), cfg_read, raw);
    else
        ADS_DBG("[ADS1115] ch%d: raw=%d\r\n", channel, raw);
#endif

    if (!s_alert_flag && (cfg_read & 0x8000) == 0) {
        ads_note_fail();
        return 0;
    }
#else
    /* OS 位轮询（本板 ALERT 不触发, 已禁用等待）: 475SPS 约 2.1ms, 上限 15ms */
    do {
        Delay_Us(200);
        cfg_read = ADS1115_ReadReg(ADS1115_REG_CONFIG);
    } while ((cfg_read & 0x8000) == 0 && ++poll < 60);
    raw = (int16_t)ADS1115_ReadReg(ADS1115_REG_CONVERSION);

#if ADS1115_DEBUG
    if ((cfg_read & 0x8000) == 0)
        ADS_DBG("[ADS1115] ch%d: OS bit=0, cfg=%04X raw=%d\r\n", channel, cfg_read, raw);
    else
        ADS_DBG("[ADS1115] ch%d: raw=%d\r\n", channel, raw);
#endif

    if ((cfg_read & 0x8000) == 0) {
        ads_note_fail();
        return 0;
    }
#endif

    ads_note_ok();
    return raw;
}
#endif /* USE_ADS1115 */
