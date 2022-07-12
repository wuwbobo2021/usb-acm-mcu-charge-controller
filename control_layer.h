// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef CONTROL_LAYER_H
#define CONTROL_LAYER_H

#include "comm_layer.h"
#include <string>
#include <chrono>
#include <iostream>

using namespace std::chrono;

inline float ms_since(steady_clock::time_point t) {
	return (steady_clock::now() - t).count() / 1000.0 / 1000.0; //nanoseconds to milliseconds
}

struct ChargeControlConfig
{
	bool  opt_stage_const_v = false;    // enable const voltage stage (for Li-ion batteries)
	
	float v_refint = ADC_VRefInt,       // on-chip stable reference voltage (V), it can be calibrated manually
	      v_ext_power = 5.0,			// charge supply voltage (V), no update schedule in current version
	      div_per = 5.6 / (3.0 + 5.6),	// divider_percentage, for ADC input of VSupply - VBattery
	      r_samp = 0.33,				// value of sampling resistor (ohm)
	      r_extra = 0,                 	// value of extra resistance of battery connection and power connection (ohm)
	      i_max = 0.5,					// maximum current flowing through the battery and the MOS (A)
	      p_mos_max = 2.0,				// MOS maximum dissipated power (W), affected by actual heat dissipation condition
	      
	      v_bat_detect_th = 0.4,		// threshold for battery detection (V)
	      v_dac_adj_step = 0.001,		// minimum increment/decrement of DAC output voltage (V)
	      v_bat_dec_th = 0.003;			// threshold for detecting voltage decline (V), for Ni-MH batteries
};

enum ChargeControlState
{
	Device_Disconnected = 0,
	Battery_Disconnected,
	Battery_Connected,
	Battery_Charging_CC,
	Battery_Charging_CV,
	Charge_Completed,
	Charge_Stopped
};

inline ostream& operator<<(ostream& ost, const ChargeControlState& st)
{
	switch (st) {
		case Device_Disconnected:	ost << "Disconnected";	break;
		case Battery_Disconnected:	ost << "No Battery";	break;
		case Battery_Connected:		ost << "Ready";			break;
		case Battery_Charging_CC:	ost << "Charging (CC)";	break; //const current stage
		case Battery_Charging_CV:	ost << "Charging (CV)";	break; //const voltage stage
		case Charge_Completed:		ost << "Complete";		break;
		case Charge_Stopped:		ost << "Stopped";		break;
	}
	return ost;
}

struct ChargeStatus
{
	ChargeControlState control_state;
	float exp_current = 0, exp_voltage = 0, exp_charge = 0;
	
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
	// exp_current, exp_voltage, exp_charge are not reset
	dac_voltage = 0;
	bat_voltage = bat_current = bat_power = mos_power = 0;
	bat_voltage_initial = bat_voltage_final = 0;
	bat_voltage_max = 0; bat_current_max = 0;
	flag_ir_measured = false; ir = 0;
	bat_charge = bat_energy = 0;
}

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

inline ostream& operator<<(ostream& ost, const ChargeControlEvent& ev)
{
	switch (ev) {
		case Event_Device_Connect:		ost << "Device Connected";		break;
		case Event_Device_Disconnect:	ost << "Device Disconnected";	break;
		case Event_Battery_Connect:		ost << "Battery Connected";		break;
		case Event_Battery_Disconnect:	ost << "Battery Disconnected";	break;
		case Event_New_Data:			ost << "Data Updated";			break;
		case Event_Charge_Complete:		ost << "Charge Completed";		break;
		case Event_Charge_Brake:		ost << "! Emergency: Stopped";	break;
	}
	return ost;
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
	ChargeStatus status;
	volatile bool flag_new_data = false, flag_start = false, flag_stop = false, flag_close = false;
	steady_clock::time_point t_last_shake; unsigned int cnt_shake_failed = 0;
	unsigned int cnt_v_dec = 0;
	
	CommLayer comm; DataCallbackPtr data_callback_ptr;
	thread* thread_control = NULL; EventCallbackPtr event_callback_ptr;
	
	void control_loop();
	void wait_for_new_data();
	void complete_charging(bool successful = true);
	void do_stop_charging(bool remeasure_voltage);
	void switch_off();
	void data_callback(float udiv, float usamp);
	
public:
	ChargeControlConfig conf;
	
	ChargeControlLayer();
	~ChargeControlLayer();
	
	ChargeStatus control_status() const;
	const float* battery_current_ptr() const;
	const float* battery_voltage_ptr() const;
	
	void set_event_callback_ptr(EventCallbackPtr ptr);
	bool set_charge_exp(float cur, float vol, float chg);
	void calibrate(float v_adc1_actual);
	void calibrate(float v_bat_actual, float v_ext_power, float r_extra = -1); //-1 means do not change
	bool start_charging();
	void stop_charging();
};

inline ChargeStatus ChargeControlLayer::control_status() const
{
	return status;
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
