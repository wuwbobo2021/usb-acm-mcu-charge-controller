// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef COMM_LAYER_H
#define COMM_LAYER_H

#include <chrono>
#include <thread>

#include "serialib/serialib.h"
#include "simple-cairo-plot/circularbuffer.h"
#include "comm_protocol.h"

using namespace std;
using namespace std::chrono;
using SimpleCairoPlot::CircularBuffer;

using DataCallbackAddr = void (*)(void*, float, float);

class DataCallbackPtr
{
	void* addr_obj = NULL;
	DataCallbackAddr addr_func = NULL;

public:
	DataCallbackPtr() {}
	DataCallbackPtr(void* pobj, DataCallbackAddr pfunc);
	void call(float u1, float u2) const;
};

inline DataCallbackPtr::DataCallbackPtr(void* pobj, DataCallbackAddr pfunc):
	addr_obj(pobj), addr_func(pfunc) {}

inline void DataCallbackPtr::call(float u1, float u2) const
{
	if (addr_func == NULL) return;
	addr_func(addr_obj, u1, u2);
}

template <typename T, void (T::*F)(float, float)>
void MemberFuncDataCallback(void* pobj, float u1, float u2)
{
	(static_cast<T*>(pobj)->*F)(u1, u2);
}

// use this function to create the pointer for a member function.
template <typename T, void (T::*F)(float, float)>
inline DataCallbackPtr MemberFuncDataCallbackPtr(T* pobj)
{
	return DataCallbackPtr(static_cast<void*>(pobj), &MemberFuncDataCallback<T, F>);
}

class CommLayer
{
public:
	CommLayer();
	~CommLayer();
	
	bool is_connected() const;
	float data_interval() const;
	float voltage_vrefint() const;
	float voltage_vdda() const;
	
	bool connect(DataCallbackPtr cb_ptr);
	void disconnect();
	bool shake();
	bool set_voltage_vrefint(float new_vrefint);
	float vrefint_calibrate(float v_adc1_actual); //returns new estimation of VRefInt
	bool dac_output(float val);

private:
	enum {
		Raw_Data_Interval = 100,
		Data_Amount_Per_Av_First = 128,
		Oversampling_Radius = 8,
	};
	
	Serialib::Serial serial;
	volatile bool flag_connected = false;
	
	Resp_Check hard_param;
	Cmd_ADC_Start adc_conf; float bulk_interval_ms;
	unsigned int data_amount_per_av_first, data_amount_per_av_second;
	
	Cmd_PWM_DAC pwm_dac_conf = {comm_cmd(Cmd_ID_PWM_DAC), 0, 0, 1, 0, true};
	
	volatile float vrefint = 0; volatile bool flag_override_vrefint = false;
	volatile float vdda = 3.3; CircularBuffer buf_vdda; //calculated at this layer
	
	thread* thread_comm = NULL;
	thread* thread_proc = NULL;
	
	volatile bool flag_dac_output = false;
	float vdac = 0.0; volatile uint16_t dac_new_val;
	
	volatile bool flag_data_ready = false;
	volatile uint16_t ad_refint; uint8_t* adc_raw_data = NULL;
	
	uint16_t* adc1_raw_data; uint16_t* adc2_raw_data;
	float* adc1_values; float* adc2_values;
	float adc1_value, adc2_value; unsigned int cnt_zero = 0;
    
	DataCallbackPtr callback_ptr;
	
	volatile bool flag_shake = false, flag_shake_success = false;
	volatile bool flag_close = false;
	
	bool adc_config();
	
	void comm_loop();
	void process_loop();
	
	inline bool apply_cmd(uint8_t cmd_id, CommResp* rec_data = NULL);
	bool apply_cmd(const CommCmd& cmd, CommResp* rec_data = NULL);
	
	bool rec_data(uint32_t timeout_ms);
	bool rec_until(const uint8_t* exp_data, uint32_t sz_data,
	               uint32_t timeout_ms = Timeout_Comm_Max);
	void rec_discard_in_ms(uint32_t ms);
	
	void process_data();
	
	float get_voltage(float val);
	uint16_t from_voltage(float val);
};

inline bool CommLayer::is_connected() const
{
	return flag_connected;
}

inline float CommLayer::data_interval() const
{
	return bulk_interval_ms;
}

inline float CommLayer::voltage_vdda() const
{
	return vdda;
}

inline float CommLayer::voltage_vrefint() const
{
	return vrefint;
}

inline bool CommLayer::set_voltage_vrefint(float new_vrefint)
{
	if (new_vrefint < 0.1 || new_vrefint > 4.8) return false;
	if (vrefint) vdda *= new_vrefint / vrefint;
	vrefint = new_vrefint;
	flag_override_vrefint = true;
	return true;
}

inline float CommLayer::vrefint_calibrate(float v_adc1_actual)
{
	if (!flag_connected || adc1_value == 0)
		return vrefint; //invalid operation, return original value of vrefint
	
	float d = v_adc1_actual / get_voltage(adc1_value);
	vrefint *= d; vdda *= d;
	buf_vdda.clear(true); buf_vdda.push(vdda);
	return vrefint;
}

inline CommLayer::~CommLayer()
{
	if (flag_connected) disconnect();
}

// private functions

inline bool CommLayer::apply_cmd(uint8_t cmd_id, CommResp* rec_data)
{
	CommCmd cmd = comm_cmd(cmd_id);
	return apply_cmd(cmd, rec_data);
}

inline float CommLayer::get_voltage(float val) {
	return val / ADC_Raw_Value_Max * vdda;
}

inline uint16_t CommLayer::from_voltage(float val) {
	if (val < 0) return 0;
	if (val > vdda) return DAC_Raw_Value_Max;
	return val / vdda * DAC_Raw_Value_Max;
}

#endif
