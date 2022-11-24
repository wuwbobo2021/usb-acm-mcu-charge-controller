#ifndef UI_LOCALE_H
#define UI_LOCALE_H

#include <glibmm/ustring.h>

#include "control_layer.h"

using Glib::ustring;

struct Locale
{
	ustring name_v_refint,
	        name_v_ext_power,
	        name_div_prop,
	        name_r_samp,
	        name_r_extra,
	        name_i_max,
	        name_p_mos_max,
	        
			name_v_bat_detect_th,
	        name_v_dac_adj_step,
	        name_v_bat_dec_th,
	        
	        name_exp_current,
			name_exp_voltage,
			name_exp_voltage_oc,
	        name_exp_charge,
	        name_time_limit,
	        name_opt_stage_const_v,
	        name_min_current,
			
	        state_device_disconnected,
	        state_battery_disconnected,
	        state_battery_connected,
	        state_battery_charging_cc,
	        state_battery_charging_cv,
	        state_charge_completed,
	        state_charge_stopped,
	        
	        stop_flag_brake,
	        stop_flag_time_limit,
	        stop_flag_exp_charge,
	        stop_flag_exp_voltage_oc,
	        stop_flag_exp_voltage,
	        stop_flag_vbat_decline,
	        stop_flag_min_current,
	        stop_flag_manual,
	        
	        event_device_connnect,
	        event_device_disconnect,
	        event_battery_connect,
	        event_battery_disconnect,
	        event_new_data,
	        event_charge_complete,
	        event_charge_brake,
	        
			event_buffer_full,
	        
	        name_bat_voltage,
	        name_dac_voltage,
	        name_bat_current,
	        
	        title_main_window,
	        caption_button_on,
	        caption_button_off,
	        caption_button_config,
	        caption_button_calibrate,
	        caption_button_open,
	        caption_button_save,
	        caption_button_apply,
			
			title_dialog_input,
			message_input_v_bat,
			message_report_vrefint,
			
			title_dialog_config,
			title_dialog_dac_scan,
	        caption_button_ok,
	        caption_button_cancel,
			caption_button_dac_scan,
	        caption_button_close,
			message_invalid_input,
			
	        title_dialog_open_file,
	        title_dialog_save_file,
	        name_file_filter_csv,
	        message_failed_to_save_file;
	
	const ustring str_empty = "", str_hyphen = " - ";
	
	Locale();
	
	void auto_set();
	void set_lang_en();
	void set_lang_zh_cn();
	
	void get_control_status_str(const ChargeStatus& st, Glib::ustring& str) const;
	
	const ustring& window_title() const;
	const ustring window_title(ustring str) const;
	const ustring window_title(ChargeControlState st) const;
	const ustring  control_state_to_str(ChargeControlState st, ChargeStopFlag fl) const;
	const ustring& control_state_to_str(ChargeControlState st) const;
	const ustring& stop_flag_to_str(ChargeStopFlag fl) const;
	const ustring& control_event_to_str(ChargeControlEvent ev) const;
};

inline Locale::Locale()
{
	auto_set();
}

const inline ustring& Locale::window_title() const
{
	return title_main_window;
}

const inline ustring Locale::window_title(ustring str) const
{
	return title_main_window + str_hyphen + str;
}

const inline ustring Locale::window_title(ChargeControlState st) const
{
	return window_title(control_state_to_str(st));
}

inline const ustring Locale::control_state_to_str(ChargeControlState st, ChargeStopFlag fl) const
{
	ustring str = control_state_to_str(st);
	if (st == Charge_Completed || st == Charge_Stopped)
		str += ": " + stop_flag_to_str(fl);
	return str;
}

inline const ustring& Locale::control_state_to_str(ChargeControlState st) const
{
	switch (st) {
		case Device_Disconnected:	return state_device_disconnected;
		case Battery_Disconnected:	return state_battery_disconnected;
		case Battery_Connected:		return state_battery_connected;
		case Battery_Charging_CC:	return state_battery_charging_cc;
		case Battery_Charging_CV:	return state_battery_charging_cv;
		case Charge_Completed:		return state_charge_completed;
		case Charge_Stopped:		return state_charge_stopped;
		
		default:					return str_empty;
	}
}

inline const ustring& Locale::stop_flag_to_str(ChargeStopFlag fl) const
{
	switch (fl) {
		case StopFlag_Brake:			return stop_flag_brake;
		case StopFlag_Time_Limit:		return stop_flag_time_limit;
		case StopFlag_Exp_Charge:		return stop_flag_exp_charge;
		case StopFlag_Exp_Voltage_OC:	return stop_flag_exp_voltage_oc;
		case StopFlag_Exp_Voltage:		return stop_flag_exp_voltage;
		case StopFlag_VBat_Decline:		return stop_flag_vbat_decline;
		case StopFlag_Min_Current:		return stop_flag_min_current;
		case StopFlag_Manual:			return stop_flag_manual;
		
		default:						return str_empty;
	}
}

inline const ustring& Locale::control_event_to_str(ChargeControlEvent ev) const
{
	switch (ev) {
		case Event_Device_Connect:		return event_device_connnect;
		case Event_Device_Disconnect:	return event_device_disconnect;
		case Event_Battery_Connect:		return event_battery_connect;
		case Event_Battery_Disconnect:	return event_battery_disconnect;
		case Event_New_Data:			return event_new_data;
		case Event_Charge_Complete:		return event_charge_complete;
		case Event_Charge_Brake:		return event_charge_brake;
		
		default:						return str_empty;
	}
}

#endif
