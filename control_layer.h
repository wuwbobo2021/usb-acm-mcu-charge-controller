// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef CONTROL_LAYER_H
#define CONTROL_LAYER_H

#include "comm_layer.h"
#include <chrono>

using namespace std::chrono;

inline float ms_since(steady_clock::time_point t) {
	return (steady_clock::now() - t).count() / 1000.0 / 1000.0; //nanoseconds to milliseconds
}

inline float ms_since(system_clock::time_point t) {
	return (system_clock::now() - t).count() / 1000.0 / 1000.0;
}

enum ChargeControlState
{
	Device_Disconnected = 0,
	Battery_Disconnected,
	Battery_Connected,
	Battery_Charging_CC, //const current stage
	Battery_Charging_CV, //const voltage stage
	Charge_Completed,
	Charge_Stopped
};

enum ChargeControlEvent
{
	Event_Device_Connect = 0,
	Event_Device_Disconnect,
	Event_Battery_Connect,
	Event_Battery_Disconnect,
	Event_New_Data,
	Event_Charge_Complete,
	Event_Charge_Brake
};

struct ChargeControlConfig
{
	float v_refint = ADC_VRefInt,       // on-chip stable reference voltage (V), it can be calibrated manually
	      v_ext_power = 5.0,			// charge supply voltage (V), no update schedule in current version
	      div_prop = 5.6 / (3.0 + 5.6),	// divider_percentage, for ADC input of VSupply - VBattery
	      r_samp = 0.33,				// value of sampling resistor (ohm)
	      r_extra = 0,                 	// value of extra resistance of battery connection and power connection (Ohm)
	      i_max = 0.5,					// maximum current flowing through the battery and the MOS (A)
	      p_mos_max = 2.0,				// MOS maximum dissipated power (W), affected by actual heat dissipation condition
	      
	      v_bat_detect_th = 0.4,		// threshold for battery detection (V)
	      v_dac_adj_step = 0.001,		// minimum increment/decrement of DAC output voltage (V)
	      v_bat_dec_th = 0.002;			// threshold for detecting voltage decline (V), for Ni-MH batteries
};

struct ChargeParameters
{
	float exp_current = 0.15,
	      exp_voltage = 1.35,
		  exp_charge = 5400.0; //unit: C
	
	bool  opt_stage_const_v = false;	// enable const voltage stage (for Li-ion batteries)
	float min_current = 0.05;
};

struct ChargeStatus
{
	ChargeControlState control_state;
	
	steady_clock::time_point t_last_update;
	float dac_voltage;
	float bat_voltage, bat_current, bat_power, mos_power; //unit: V, A, W
	
	float bat_voltage_initial, bat_voltage_final;
	system_clock::time_point t_charge_start, t_charge_stop;
	
	float bat_voltage_max; steady_clock::time_point t_bat_voltage_max;
	float bat_current_max; steady_clock::time_point t_bat_current_max;
	
	bool flag_ir_measured = false;
	float ir = 0; //DC internal resistance estimation (ohm)
	
	float bat_charge, bat_energy; //unit: C, J
	
	ChargeStatus();
	void reset();
	operator string() const;
};

ostream& operator<<(ostream& ost, const ChargeStatus& ch);

inline ChargeStatus::ChargeStatus()
{
	reset();
}

inline void ChargeStatus::reset()
{
	control_state = Device_Disconnected;
	dac_voltage = 0;
	bat_voltage = bat_current = bat_power = mos_power = 0;
	bat_voltage_initial = bat_voltage_final = 0;
	bat_voltage_max = 0; bat_current_max = 0;
	flag_ir_measured = false; ir = 0;
	bat_charge = bat_energy = 0;
}

typedef void (*EventCallbackAddr)(void*, ChargeControlEvent);

class EventCallbackPtr
{
	void* addr_obj = NULL;
	EventCallbackAddr addr_func = NULL;

public:
	EventCallbackPtr() {}
	EventCallbackPtr(void* pobj, EventCallbackAddr pfunc);
	void call(ChargeControlEvent ev) const;
};

inline EventCallbackPtr::EventCallbackPtr(void* pobj, EventCallbackAddr pfunc):
	addr_obj(pobj), addr_func(pfunc) {}

inline void EventCallbackPtr::call(ChargeControlEvent ev) const
{
	if (addr_func == NULL) return;
	addr_func(addr_obj, ev);
}

template <typename T, void (T::*F)(ChargeControlEvent)>
void MemberFuncEventCallback(void* pobj, ChargeControlEvent ev)
{
	(static_cast<T*>(pobj)->*F)(ev);
}

// use this function to create the pointer for a member function.
template <typename T, void (T::*F)(ChargeControlEvent)>
inline EventCallbackPtr MemberFuncEventCallbackPtr(T* pobj)
{
	return EventCallbackPtr(static_cast<void*>(pobj), &MemberFuncEventCallback<T, F>);
}

class ChargeControlLayer
{
	ChargeControlConfig conf;
	ChargeParameters param;
	ChargeStatus status;
	
	CommLayer comm; DataCallbackPtr data_callback_ptr;
	thread* thread_control = NULL; EventCallbackPtr event_callback_ptr;
	
	volatile bool flag_new_data = false, flag_start = false, flag_stop = false, flag_close = false;
	steady_clock::time_point t_shake, t_shake_suc; unsigned int cnt_shake_failed = 0;
	unsigned int cnt_v_dec = 0, cnt_v_max_detect = 0;
	
	void control_loop();
	
	bool check_comm();
	bool check_shake();
	bool check_bat_connection();
	bool wait_for_new_data();
	bool measure_ir();
	void complete_charging(bool successful = true);
	void do_stop_charging(bool remeasure_voltage);
	
	void data_callback(float udiv, float usamp);
	
public:
	
	ChargeControlLayer();
	~ChargeControlLayer();
	
	ChargeControlConfig hard_config() const;
	ChargeParameters charge_param() const;
	ChargeStatus control_status() const;

	float data_interval() const;
	const float* battery_current_ptr() const;
	const float* battery_voltage_ptr() const;
	
	bool set_hard_config(ChargeControlConfig new_conf);
	bool set_charge_param(ChargeParameters new_param);
	void set_event_callback_ptr(EventCallbackPtr ptr);
	void calibrate(float v_bat_actual);
	bool start_charging();
	void stop_charging();
};

inline ChargeControlConfig ChargeControlLayer::hard_config() const
{
	return conf;
}

inline ChargeParameters ChargeControlLayer::charge_param() const
{
	return param;
}

inline ChargeStatus ChargeControlLayer::control_status() const
{
	return status;
}

inline float ChargeControlLayer::data_interval() const
{
	return comm.data_interval();
}

inline const float* ChargeControlLayer::battery_current_ptr() const
{
	return & status.bat_current;
}

inline const float* ChargeControlLayer::battery_voltage_ptr() const
{
	return & status.bat_voltage;
}

inline void ChargeControlLayer::set_event_callback_ptr(EventCallbackPtr ptr)
{
	event_callback_ptr = ptr;
}

#endif
