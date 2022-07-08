// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef COMM_LAYER_H
#define COMM_LAYER_H

#include <chrono>
#include <thread>

#include "serialib/serialib.h"
#include "usb_adc_dac.h"

using namespace std;
using namespace std::chrono;

typedef void (*DataCallbackAddr)(void*, float, float);

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

const unsigned int Oversampling_Radius = 8,
                   Data_Amount_Per_Av_First = 128,
                   Data_Amount_Per_Av_Second = ADC_Bulk_Data_Amount / Data_Amount_Per_Av_First,
                   Interval_Read_VRefInt = 10 * 1000;//60 * 1000;

class CommLayer
{
	Serialib::Serial serial;
	volatile bool flag_connected = false;
	Cmd_ADC_Config adc_conf; float bulk_interval_ms;
	
	float vdda = 3.3;
	
	thread* thread_comm = NULL; thread* thread_proc = NULL;
	
	char adc_raw_data[ADC_Bulk_Size]; bool flag_data_ready = false;
	uint16_t adc1_raw_data[ADC_Buffer_Data_Amount], adc2_raw_data[ADC_Buffer_Data_Amount];
	float adc1_values[Data_Amount_Per_Av_Second], adc2_values[Data_Amount_Per_Av_Second];
	float adc1_value, adc2_value; unsigned int cnt_zero = 0;
    
	DataCallbackPtr callback_ptr;
	
	steady_clock::time_point t_read_vrefint;
	
	volatile bool flag_dac_output = false; volatile uint16_t dac_new_val;
	volatile bool flag_shake = false, flag_shake_success = false;
	volatile bool flag_close = false;
	
	bool send_cmd_rec_resp(char cmd_id, const char* data_extra = NULL, uint32_t sz_extra = 0);
	bool send_cmd(char cmd_id, const char* data_extra = NULL, uint32_t sz_extra = 0);
	int rec_resp(char* data_extra = NULL, uint32_t sz_extra = 0, uint32_t timeout_ms = 500);
	bool rec_until(const char* exp_data, uint32_t sz_data, uint32_t timeout_ms = 500);
	void rec_discard_in_ms(uint32_t ms);
	
	bool rec_data(uint32_t timeout_ms);
    void process_data();
    
	bool read_vrefint(bool recover = false);
	float get_voltage(float val);
	uint16_t from_voltage(float val);
	
	void comm_loop();
	void process_loop();
	
public:
	float vrefint = ADC_VRefInt; 
	
	~CommLayer();
	
	bool is_connected() const;
	float voltage_vdda() const;
	
	bool connect(DataCallbackPtr cb_ptr);
	void disconnect();
	bool shake();
	float vrefint_calibrate(float v_adc1_actual); //returns new estimation of VRefInt
	void dac_output(float val);
};

inline CommLayer::~CommLayer()
{
	if (flag_connected) disconnect();
}

inline bool CommLayer::is_connected() const
{
	return flag_connected;
}

inline float CommLayer::voltage_vdda() const
{
	return vdda;
}

inline float CommLayer::vrefint_calibrate(float v_adc1_actual)
{
	if (!flag_connected || adc1_value == 0) return vrefint; //invalid operation, return original value of vrefint
	
	float d = v_adc1_actual / get_voltage(adc1_value);
	vdda *= d; vrefint *= d;
	return vrefint;
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
