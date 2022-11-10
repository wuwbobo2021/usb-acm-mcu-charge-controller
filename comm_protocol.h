// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef COMM_PROTOCOL_H
#define COMM_PROTOCOL_H

#ifdef __cplusplus
	#include <cstdint>
#else
	#include "stdint.h"
	#ifndef bool
		#include "stdbool.h"
	#endif
#endif

#define Protocol_Header 0x0000ffff // 0x ff ff 00 00
#define Protocol_Key    0x20220914 // 0x 14 09 22 20

#define Cmd_ID_Check          ((uint8_t) 0x01)
#define Cmd_ID_ADC_Start      ((uint8_t) 0x02)
#define Cmd_ID_ADC_Stop       ((uint8_t) 0x03)
#define Cmd_ID_PWM_DAC        ((uint8_t) 0x04)
#define Cmd_ID_Disable_Output ((uint8_t) 0x05)
#define Cmd_ID_Shake          ((uint8_t) 0x06)
#define Cmd_ID_Unlock         ((uint8_t) 0xfe)
#define Cmd_ID_Reset          ((uint8_t) 0xff)

#define Resp_OK               ((uint8_t) 0x00)
#define Resp_Failed           ((uint8_t) 0x01)

// the data block consists of this 32-bit header, then 16-bit AD_RefInt value,
// then adc_bulk_data_amount * (16b(ADC1) + 16b(ADC2)).
#define Data_Header_Length 4
#define Data_Header 0xffffffee // 0x ee ff ff ff

#define ADC_Raw_Value_Max     ((uint16_t) 4095)
#define DAC_Raw_Value_Max     ((uint16_t) 4095)

#define Timeout_Comm_Max       500 //ms
#define Timeout_Data_Max      1000 //ms
#define Shake_Interval_Max   10000 //ms, the MCU should disable DAC output when it is exceeded

#pragma pack(push, 1) //disable aligning (supported by MSVC, GCC and Clang)

typedef struct {
	uint32_t header;      // it must be Protocol_Header
	uint8_t  cmd_id;      // it must be Cmd_ID_XXX
	uint8_t  ext_length;  // it might be zero
} CommCmd;

typedef struct {
	uint32_t header;      // it must be Protocol_Header
	uint8_t  cmd_id;      // it must be Cmd_ID_XXX
	uint8_t  resp_val;    // it must be Resp_XXX
	uint8_t  ext_length;  // it might be zero
} CommResp;

// commands and responses have zero ext_length except those defined below.

typedef struct {
	CommResp resp;
	uint32_t protocol_key;              // it must be Protocol_Key
	bool     dac_support;               // if not, PWM (with RC filter) will be used to implement the function
	uint32_t pwm_clock_freq;            // PWM timer clock frequency (Hz)
	uint32_t adc_clock_freq;            // ADC clock frequency (Hz)
	uint16_t adc_clock_cycles_opts[16]; // options in ascending order, unavailable options should be 0
	uint16_t adc_bulk_data_amount;      // each "element" consist of readings of both channels (4B)
	uint16_t adc_vrefint;               // internal reference voltage (mV)
} Resp_Check;

typedef struct {
	CommCmd  cmd;
	bool     discontinous_mode;        // stop conversion when the ADC double buffer becomes full
	uint8_t  adc_clock_cycles_opt;     // 0 ~ 15, chooses a value in array Resp_Check->adc_clock_cycles_opts
} Cmd_ADC_Start;

// before unlocking, the output should be disabled (0) when ADC is not running,
// and at the middle of the last data bulk if discontinous mode is enabled.
typedef struct {
	CommCmd  cmd;
	uint16_t pwm_tim_prescaler;        // this value plus 1 prescales the timer peripheral clock
	uint16_t pwm_reload_val;           // maximum value of the counter, kept for the last cycle
	uint16_t pwm_duty_val;             // the output becomes low when the counter counts to this value
	uint16_t dac_val;                  // 12-bit DAC value
	bool     no_resp;                  // do not response
} Cmd_PWM_DAC;

#pragma pack(pop) //recover previous align mode

static inline uint8_t cmd_length(uint8_t cmd_id);
static inline CommCmd comm_cmd(uint8_t cmd_id);
static inline bool is_valid_cmd(const uint8_t* ptr, uint8_t length);

static inline uint8_t resp_length(uint8_t cmd_id);
static inline CommResp comm_resp(uint8_t cmd_id, uint8_t resp_val);
static inline bool is_valid_resp(const uint8_t* ptr, uint8_t length);

static inline uint32_t adc_bulk_data_size(const Resp_Check* hard_param);
static inline float adc_raw_data_interval_ms(const Resp_Check* hard_param, uint8_t opt);
static inline float adc_bulk_interval_ms(const Resp_Check* hard_param, uint8_t opt);
static uint8_t choose_adc_clock_cycles_opt(const Resp_Check* hard_param, float interval_ms);

// function definitions

static inline uint8_t cmd_length(uint8_t cmd_id)
{
	switch (cmd_id) {
		case Cmd_ID_Check: case Cmd_ID_ADC_Stop: case Cmd_ID_Disable_Output:
		case Cmd_ID_Shake: case Cmd_ID_Unlock: case Cmd_ID_Reset:
			return sizeof(CommCmd);
		case Cmd_ID_ADC_Start:
			return sizeof(Cmd_ADC_Start);
		case Cmd_ID_PWM_DAC:
			return sizeof(Cmd_PWM_DAC);
		default:
			return 0;
	}
}

static inline CommCmd comm_cmd(uint8_t cmd_id)
{
	CommCmd cmd = {Protocol_Header, cmd_id,
	               (uint8_t)(cmd_length(cmd_id) - sizeof(CommCmd))};
	return cmd;
}

static inline bool is_valid_cmd(const uint8_t* ptr, uint8_t length)
{
	const CommCmd* cmd = (const CommCmd*) ptr;
	
	if (length < sizeof(CommCmd)
	||  cmd->header != Protocol_Header
	||  ! cmd_length(cmd->cmd_id)
	||  length != sizeof(CommCmd) + cmd->ext_length
	||  length != cmd_length(cmd->cmd_id))
		return false;
	
	if (cmd->cmd_id == Cmd_ID_ADC_Start)
		if (((Cmd_ADC_Start*) cmd)->adc_clock_cycles_opt > 15)
			return false;
	if (cmd->cmd_id == Cmd_ID_PWM_DAC) {
		Cmd_PWM_DAC* cmd_pwm_dac = (Cmd_PWM_DAC*) cmd;
		if (cmd_pwm_dac->dac_val > 4095)
			return false;
	}
	
	return true;
}

static inline uint8_t resp_length(uint8_t cmd_id)
{
	if (cmd_id == Cmd_ID_Check)
		return sizeof(Resp_Check);
	else
		return sizeof(CommResp);
}

static inline CommResp comm_resp(uint8_t cmd_id, uint8_t resp_val)
{
	CommResp resp = {Protocol_Header, cmd_id, resp_val,
	                 (uint8_t)(resp_length(cmd_id) - sizeof(CommResp))};
	return resp;
}

static inline bool is_valid_resp(const uint8_t* ptr, uint8_t length)
{
	const CommResp* resp = (const CommResp*) ptr;
	
	if (length < sizeof(CommResp)
	||  resp->header != Protocol_Header
	||  ! cmd_length(resp->cmd_id)
	||  (resp->resp_val != Resp_OK && resp->resp_val != Resp_Failed)
	||  length != sizeof(CommResp) + resp->ext_length
	||  length != resp_length(resp->cmd_id))
		return false;
	
	if (resp->cmd_id == Cmd_ID_Check) {
		Resp_Check* resp_check = (Resp_Check*) ptr;
		if (resp_check->protocol_key != Protocol_Key
		||  ! resp_check->pwm_clock_freq
		||  ! resp_check->adc_clock_freq
		||  ! resp_check->adc_clock_cycles_opts[0]
		||  ! resp_check->adc_bulk_data_amount
		||  ! resp_check->adc_vrefint)
			return false;
	}
	
	return true;
}

static inline uint32_t adc_bulk_data_size(const Resp_Check* hard_param)
{
	return 2 * 2 * hard_param->adc_bulk_data_amount;
}

static inline float adc_raw_data_interval_ms(const Resp_Check* hard_param, uint8_t opt)
{
	if (hard_param->adc_clock_freq == 0) return 0;
	return hard_param->adc_clock_cycles_opts[opt] * 1000.0 / hard_param->adc_clock_freq;
}

static inline float adc_bulk_interval_ms(const Resp_Check* hard_param, uint8_t opt)
{
	return hard_param->adc_bulk_data_amount
	     * adc_raw_data_interval_ms(hard_param, opt);
}

static uint8_t choose_adc_clock_cycles_opt(const Resp_Check* hard_param, float interval_ms)
{
	if (interval_ms == 0) return 0;
	
	uint16_t val = interval_ms * hard_param->adc_clock_freq / 1000; //clock cycles
	uint8_t opt_r; uint16_t val_r;
	for (opt_r = 0; opt_r < 16; opt_r++) {
		val_r = hard_param->adc_clock_cycles_opts[opt_r];
		if (val_r == 0 || val_r >= val) break;
	}
	
	if (opt_r == 0) return 0; if (opt_r == 16) return 15;
	if (val_r == 0) opt_r--; if (val_r == val) return opt_r;
	
	uint8_t opt_l = opt_r - 1;
	uint16_t val_l = hard_param->adc_clock_cycles_opts[opt_l];
	if (val_r - val > val - val_l)
		return opt_l;
	else
		return opt_r;
}

#endif
