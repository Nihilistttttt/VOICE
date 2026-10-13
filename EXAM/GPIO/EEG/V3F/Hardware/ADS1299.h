#ifndef __ADS1299_H
#define __ADS1299_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32h417.h"
#include "debug.h"
#include <stdint.h>


/*============================ ADS1299 ������ ============================*/

#define ADS1299_CMD_WAKEUP                 0x02U   /* �Ӵ���ģʽ���� */
#define ADS1299_CMD_STANDBY                0x04U   /* �������ģʽ */
#define ADS1299_CMD_RESET                  0x06U   /* ������λ */
#define ADS1299_CMD_START                  0x08U   /* ���ʽ����ת�� */
#define ADS1299_CMD_STOP                   0x0AU   /* ���ʽֹͣת�� */
#define ADS1299_CMD_RDATAC                 0x10U   /* ����������ģʽ */
#define ADS1299_CMD_SDATAC                 0x11U   /* ֹͣ����������ģʽ */
#define ADS1299_CMD_RDATA                  0x12U   /* ���ζ�һ֡���� */
#define ADS1299_CMD_RREG                   0x20U   /* ���Ĵ��������ֵ��001r rrrr */
#define ADS1299_CMD_WREG                   0x40U   /* д�Ĵ��������ֵ��010r rrrr */

/*============================ ADS1299 �Ĵ�����ַ ============================*/

#define ADS1299_REG_ID                     0x00U
#define ADS1299_REG_CONFIG1                0x01U
#define ADS1299_REG_CONFIG2                0x02U
#define ADS1299_REG_CONFIG3                0x03U
#define ADS1299_REG_LOFF                   0x04U
#define ADS1299_REG_CH1SET                 0x05U
#define ADS1299_REG_CH2SET                 0x06U
#define ADS1299_REG_CH3SET                 0x07U
#define ADS1299_REG_CH4SET                 0x08U
#define ADS1299_REG_CH5SET                 0x09U
#define ADS1299_REG_CH6SET                 0x0AU
#define ADS1299_REG_CH7SET                 0x0BU
#define ADS1299_REG_CH8SET                 0x0CU
#define ADS1299_REG_BIAS_SENSP             0x0DU
#define ADS1299_REG_BIAS_SENSN             0x0EU
#define ADS1299_REG_LOFF_SENSP             0x0FU
#define ADS1299_REG_LOFF_SENSN             0x10U
#define ADS1299_REG_LOFF_FLIP              0x11U
#define ADS1299_REG_LOFF_STATP             0x12U
#define ADS1299_REG_LOFF_STATN             0x13U
#define ADS1299_REG_GPIO                   0x14U
#define ADS1299_REG_MISC1                  0x15U
#define ADS1299_REG_MISC2                  0x16U
#define ADS1299_REG_CONFIG4                0x17U

/*============================ ���ݳ��ȶ��� ============================*/

#define ADS1299_CHANNEL_NUM                8U
#define ADS1299_STATUS_BYTE_NUM            3U
#define ADS1299_CHANNEL_BYTE_NUM           3U
#define ADS1299_FRAME_BYTE_NUM             (ADS1299_STATUS_BYTE_NUM + ADS1299_CHANNEL_NUM * ADS1299_CHANNEL_BYTE_NUM)  /* 3 + 8*3 = 27 �ֽ� */

/* ������ʵ������ǰ 4 ·�Ե磺CH1=CP3, CH2=CP4, CH3=C3, CH4=C4��
 * ADS1299 ��������ʱ��Ȼ��ȡ���� 8 ͨ��֡��δ���õ� CH5~CH8 �ڼĴ����йرա�
 */
#define ADS1299_ACTIVE_EEG_CHANNEL_NUM      8U
#define ADS1299_EEG_CH_F4                   0U
#define ADS1299_EEG_CH_C4                   1U
#define ADS1299_EEG_CH_CP4                  2U
#define ADS1299_EEG_CH_OZ                   3U
#define ADS1299_EEG_CH_O1                   4U
#define ADS1299_EEG_CH_CP3                  5U
#define ADS1299_EEG_CH_C3                   6U
#define ADS1299_EEG_CH_F3                   7U


/*============================ ADS1299 ��ʱ ============================*/
#define ADS1299_SPI_TIMEOUT                0x000FFFFFUL
#define ADS1299_POWER_ON_DELAY_MS          1000U   /* �ϵ��ȴ�ģ��ǰ�ˡ���Դ��ο���ѹ�����ȶ� */
#define ADS1299_RESET_LOW_DELAY_MS         2U      /* RESET ���ͱ���ʱ�� */
#define ADS1299_POST_RESET_DELAY_MS        100U    /* RESET ���ߺ�ȴ�оƬ����ڲ���λ */
#define ADS1299_REF_STABLE_DELAY_MS        200U    /* �����ڲ��ο���ȴ��ȶ� */


/*============================ ADS1299 ��ѹת�� ============================*/
#define ADS1299_VREF_DEFAULT 4.5f
#define ADS1299_GAIN_DEFAULT 24.0f
#define ADS1299_FULL_SCALE_CODE 8388608.0f

/*============================ ADS1299 寄存器配置值 ============================*/
#define ADS1299_CFG1_DATARATE_250SPS   0x96u
#define ADS1299_CFG2_INT_TEST_OFF      0xC0u
#define ADS1299_CFG3_BIAS_REF_INT      0xECu
#define ADS1299_CFG3_BIAS_LOFF_SENS    0xEEu
#define ADS1299_CFG4_SINGLE_SHOT       0x02u
#define ADS1299_BIAS_SENSP_ALL       0xFFu
#define ADS1299_BIAS_SENSN_ALL       0xFFu
#define ADS1299_MISC1_DEFAULT          0x00u
#define ADS1299_CHSET_POWER_NORMAL     0x60u
#define ADS1299_CHSET_POWER_DOWN       0x81u


#define ADS1299_IMPEDANCE_N_FRAMES    128u
#define ADS1299_IMPEDANCE_I_NA        6.0f


#define ADS1299_BIAS_LOFF_SENS_BIT    0x02u

/*============================ ���⺯������ ============================*/
uint8_t   ADS1299_Init(void);
void ADS1299_ParseRawFrame(const uint8_t *frame_buf, uint32_t *status, int32_t ch_data[ADS1299_CHANNEL_NUM]);
float ADS1299_CodeToVolt (int32_t code, float vref, float gain) ;
float ADS1299_CodeToMicroVolt (int32_t code, float vref, float gain);
uint8_t ring_buffer_get_frame(uint8_t *dest);
uint8_t ring_buffer_has_frame(void);

void ADS1299_EnterImpedanceMode(void);
void ADS1299_ExitImpedanceMode(void);
void ADS1299_MeasureImpedance(float out_kohm[ADS1299_CHANNEL_NUM]);


uint8_t ADS1299_GetBiasStatus(void);
uint8_t ADS1299_RecheckBias (void);
#ifdef __cplusplus
}
#endif

#endif /* __ADS1299_H */
