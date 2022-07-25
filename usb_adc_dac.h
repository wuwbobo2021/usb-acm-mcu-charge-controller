// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef USB_ADC_DAC_H
#define USB_ADC_DAC_H

#ifdef __cplusplus
	#include <cstdint>
#else
	#include "stdint.h"
	#ifndef bool
		#include "stdbool.h"
	#endif
#endif

#define ADC_Raw_Value_Max 4095

#define ADC_VRefInt 1.222
#define ADC_Clock_Freq (12 * 1000 * 1000)

#ifndef ADC_SampleTime_1Cycles5
	#define ADC_SampleTime_1Cycles5                    ((uint8_t)0x00)   /*!<  ADC sampling time 1.5 cycle */
	#define ADC_SampleTime_2Cycles5                    ((uint8_t)0x01)   /*!<  ADC sampling time 2.5 cycles */
	#define ADC_SampleTime_4Cycles5                    ((uint8_t)0x02)   /*!<  ADC sampling time 4.5 cycles */
	#define ADC_SampleTime_7Cycles5                    ((uint8_t)0x03)   /*!<  ADC sampling time 7.5 cycles */
	#define ADC_SampleTime_19Cycles5                   ((uint8_t)0x04)   /*!<  ADC sampling time 19.5 cycles */
	#define ADC_SampleTime_61Cycles5                   ((uint8_t)0x05)   /*!<  ADC sampling time 61.5 cycles */
	#define ADC_SampleTime_181Cycles5                  ((uint8_t)0x06)   /*!<  ADC sampling time 181.5 cycles */
	#define ADC_SampleTime_601Cycles5                  ((uint8_t)0x07)   /*!<  ADC sampling time 601.5 cycles */
#endif

#define ADC_Buffer_Data_Amount (16 * 128)
#define ADC_Buffer_Size (2 * 2 * ADC_Buffer_Data_Amount)
#define ADC_Bulk_Size (ADC_Buffer_Size / 2)
#define ADC_Bulk_Data_Amount (ADC_Buffer_Data_Amount / 2)

#define DAC_Raw_Value_Max 4095
#define DAC_Voltage_Min 0.2
#define DAC_Voltage_Max 3.1

#define Cmd_Header_Length 4
#define Cmd_Header 0x0000ffff //0x ff ff 00 00

#define Cmd_ID_Check 0x00
#define Cmd_ID_ADC_Config 0x01 //Cmd_ADC_Config at right
#define Cmd_ID_ADC_Start 0x02
#define Cmd_ID_Shake 0x03
#define Cmd_ID_DAC_Output 0x04 //2 bytes of DAC data at right; doesn't expect a response
#define Cmd_ID_ADC_Stop 0x05
#define Cmd_ID_Reset 0xff

#define Resp_Check 0x20220531 //0x 31 05 22 20
#define Resp_OK 0x00
#define Resp_Failed 0x01

#define Data_Header_Length 4
#define Data_Header 0xffffffee //0x ee ff ff ff

#define Shake_Interval_Max 10000 //ms

typedef struct {
	bool Use_VRefInt_For_ADC2;
	uint8_t ADC_SampleTime;
} Cmd_ADC_Config;

inline float adc_raw_data_interval_ms(uint8_t adc_sampletime)
{
	float spt;
	switch (adc_sampletime) {
		case ADC_SampleTime_1Cycles5: spt = 1.5; break;
		case ADC_SampleTime_2Cycles5: spt = 2.5; break;
		case ADC_SampleTime_4Cycles5: spt = 4.5; break;
		case ADC_SampleTime_7Cycles5: spt = 7.5; break;
		case ADC_SampleTime_19Cycles5: spt = 19.5; break;
		case ADC_SampleTime_181Cycles5: spt = 181.5; break;
		case ADC_SampleTime_601Cycles5: spt = 601.5; break;
		default: return 0;
	}
	
	return (12.5 + spt) * 1000.0 / ADC_Clock_Freq;
}

#endif
