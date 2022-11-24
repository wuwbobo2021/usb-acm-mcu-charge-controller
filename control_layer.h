// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef CONTROL_LAYER_H
#define CONTROL_LAYER_H

#include "comm_layer.h"

#ifdef dbg_print
	#undef dbg_print
#endif
#ifdef DEBUG
	#include <iostream>
	#define dbg_print(str) {cout << "(Ctrl) " << (str) << endl;}
#else
	#define dbg_print(str)
#endif

using namespace std::chrono;

inline unsigned long int ms_since(steady_clock::time_point t) {
	return duration_cast<milliseconds>(steady_clock::now() - t).count();
}

// cast from steady_clock::time_point to system_clock::time_point
inline system_clock::time_point to_system_clock(steady_clock::time_point t) {
	return system_clock::now() - milliseconds(ms_since(t));
}

enum ChargeControlState
{
	Device_Disconnected = 0,
	Battery_Disconnected,
	Battery_Connected,
	DAC_Scanning,
	Battery_Charging_CC, //const current stage
	Battery_Charging_CV, //const voltage stage
	Charge_Completed,
	Charge_Stopped
};

enum ChargeStopFlag
{
	StopFlag_Brake = 0,
	StopFlag_Time_Limit,
	StopFlag_Exp_Charge,
	StopFlag_Exp_Voltage_OC,
	StopFlag_Exp_Voltage,
	StopFlag_VBat_Decline,
	StopFlag_Min_Current,
	StopFlag_Manual
};

enum ChargeControlEvent
{
	Event_Device_Connect = 0,
	Event_Device_Disconnect,
	Event_Battery_Connect,
	Event_Battery_Disconnect,
	Event_New_Data,
	Event_Scan_Complete,
	Event_Charge_Complete,
	Event_Charge_Brake
};

struct ChargeControlConfig
{
	float v_refint = 0,                 // on-chip stable reference voltage (V), it can be calibrated manually
	      v_ext_power = 5.0,			// charge supply voltage (V), no update schedule in current version
	      div_prop = 5.6 / (3.0 + 5.6),	// divider percentage, for ADC input of VSupply - VBattery
	      r_samp = 0.33,				// value of sampling resistor (ohm)
	      r_extra = 0,                 	// value of extra resistance of battery connection and power connection (Ohm)
	      i_max = 0.5,					// maximum current flowing through the battery and the MOS (A)
	      p_mos_max = 2.0,				// MOS maximum dissipated power (W), affected by actual heat dissipation condition
	      
	      v_bat_detect_th = 0.4,		// threshold for battery detection (V)
	      v_dac_adj_step = 0.001,		// minimum increment/decrement of DAC output voltage (V)
	      v_bat_dec_th = 0.002;			// threshold for detecting voltage decline (V), for Ni-MH batteries
	
	bool operator==(const ChargeControlConfig& conf);
	bool operator!=(const ChargeControlConfig& conf);
};

inline bool ChargeControlConfig::operator==(const ChargeControlConfig& conf)
{
	return    v_refint == conf.v_refint
	       && v_ext_power == conf.v_ext_power
	       && div_prop == conf.div_prop
	       && r_samp == conf.r_samp
	       && r_extra == conf.r_extra
	       && i_max == conf.i_max
	       && p_mos_max == conf.p_mos_max
	       
	       && v_bat_detect_th == conf.v_bat_detect_th
	       && v_dac_adj_step == conf.v_dac_adj_step
	       && v_bat_dec_th == conf.v_bat_dec_th;
}

inline bool ChargeControlConfig::operator!=(const ChargeControlConfig& conf)
{
	return ! (*this == conf);
}

struct ChargeParameters
{
	float exp_current = 0.15,
	      exp_voltage = 1.35,
	      exp_voltage_oc = 1.3,
		  exp_charge = 5400.0; //unit: C
	
	bool  opt_stage_const_v = false;	// enable const voltage stage (for Li-ion batteries)
	float min_current = 0.05;
	
	unsigned int time_limit_sec = 3600;
};

struct ChargeStatus
{
	ChargeControlState control_state;
	ChargeStopFlag stop_cause;
	
	steady_clock::time_point t_last_update;
	volatile float dac_voltage;
	volatile float bat_voltage, bat_current, bat_voltage_oc; //unit: V, A. open-circuit voltage is calculated by ir.
	float bat_power, mos_power; //unit: W
	
	float bat_voltage_initial, bat_voltage_final;
	steady_clock::time_point t_charge_start, t_charge_stop;
	
	float bat_voltage_max; steady_clock::time_point t_bat_voltage_max;
	float bat_current_max; steady_clock::time_point t_bat_current_max;
	
	bool flag_ir_measured = false; steady_clock::time_point t_ir_measure;
	float ir = 0; //DC internal resistance estimation (ohm)
	
	float bat_charge, bat_energy; //unit: C, J
	
	ChargeStatus();
	void reset();
	void set_state(ChargeControlState st);
	
	bool is_charging() const;
	operator string() const;
};

ostream& operator<<(ostream& ost, const ChargeStatus& ch);

inline ChargeStatus::ChargeStatus()
{
	reset();
}

inline void ChargeStatus::reset()
{
	set_state(Device_Disconnected);
	dac_voltage = 0;
	bat_voltage = bat_current = 0;
}

inline void ChargeStatus::set_state(ChargeControlState st)
{
	control_state = st;
	bat_voltage_oc = bat_power = mos_power = 0;
	bat_voltage_initial = bat_voltage_final = 0;
	bat_voltage_max = bat_current_max = 0;
	flag_ir_measured = false; ir = 0;
	bat_charge = bat_energy = 0;
	
	steady_clock::time_point t_init;
	t_last_update = t_init;
	t_charge_start = t_charge_stop = t_init;
	t_bat_voltage_max = t_bat_current_max = t_init;
}

inline bool ChargeStatus::is_charging() const
{
	return control_state == Battery_Charging_CC
	    || control_state == Battery_Charging_CV;
}

using EventCallbackAddr = void (*)(void*, ChargeControlEvent);

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
public:
	ChargeControlLayer();
	~ChargeControlLayer();
	
	float data_interval() const;
	ChargeControlConfig hard_config() const;
	ChargeParameters charge_param() const;
	ChargeStatus control_status() const;
	const ChargeStatus* control_status_ptr() const;
	
	bool set_hard_config(ChargeControlConfig new_conf);
	bool set_charge_param(ChargeParameters new_param);
	void set_event_callback_ptr(EventCallbackPtr ptr);
	void calibrate(float v_bat_actual);
	bool dac_scan();
	void stop_dac_scan();
	bool start_charging();
	void stop_charging();

private:
	ChargeControlConfig conf;
	ChargeParameters param;
	ChargeStatus status;
	EventCallbackPtr event_callback_ptr;
	
	CommLayer comm; DataCallbackPtr data_callback_ptr;
	thread* thread_control = NULL;
	
	// set by data callback from CommLayer
	volatile bool flag_new_data = false;
	volatile float udiv = 0, usamp = 0;
	
	// set by external functions
	volatile bool flag_dac_scan = false, flag_stop_dac_scan = false;
	volatile bool flag_start = false, flag_stop = false, flag_close = false;
	
	steady_clock::time_point t_shake, t_shake_suc;
	unsigned int cnt_shake_failed = 0;
	
	float bat_voltage_raw = 0, bat_current_raw = 0;
	
	bool flag_scrolling_average = false; //status values are averaged when it's set 
	CircularBuffer buf_bat_voltage, buf_bat_current;
	
	// used for voltage decline check. when flag_scrolling_average is true,
	// it is the recent maximum value under stable current condition.
	float bat_voltage_cur_max = 0;
	
	void control_loop();
	
	bool check_comm();
	bool check_shake();
	
	bool wait_for_new_data();
	bool check_bat_connection();
	void update_status_values();
	
	bool dac_output(float val);
	
	bool measure_ir();
	void stop_charging(ChargeStopFlag flag);
	void do_stop_charging(bool remeasure_voltage);
	
	void enable_scrolling_average(); //only this function clears both buffers
	void disable_scrolling_average();
	void restart_scrolling_average();
	
	void data_callback(float udiv, float usamp);
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

inline const ChargeStatus* ChargeControlLayer::control_status_ptr() const
{
	return &status;
}

inline void ChargeControlLayer::set_event_callback_ptr(EventCallbackPtr ptr)
{
	event_callback_ptr = ptr;
}

// private functions

inline void ChargeControlLayer::enable_scrolling_average()
{
	if (flag_scrolling_average) return;
	buf_bat_current.clear(true); buf_bat_voltage.clear(true);
	if (bat_voltage_raw) {
		buf_bat_current.push(status.bat_current = bat_current_raw);
		buf_bat_voltage.push(status.bat_voltage = bat_voltage_raw);
	}
	flag_scrolling_average = true;
	dbg_print("enabled scrolling average. current voltage: " + to_string(status.bat_voltage));
}

inline void ChargeControlLayer::disable_scrolling_average()
{
	if (! flag_scrolling_average) return;
	flag_scrolling_average = false;
	if (bat_voltage_raw) {
		status.bat_current = bat_current_raw;
		status.bat_voltage = bat_voltage_raw;
	}
	dbg_print("disabled scrolling average. current voltage: " + to_string(status.bat_voltage));
}

inline void ChargeControlLayer::restart_scrolling_average()
{
	disable_scrolling_average();
	enable_scrolling_average();
}

inline bool ChargeControlLayer::dac_output(float val)
{
	bool suc = comm.dac_output(val);
	if (suc) status.dac_voltage = val;
	return suc;
}

#endif
