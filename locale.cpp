#include "locale.h"

#include <ctime>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
	#include <winnls.h>
	#pragma comment(lib, "kernel32.lib")
#else
	#include <locale>
#endif

void Locale::auto_set()
{
#ifdef _WIN32
	LANGID lid = GetUserDefaultLangID();
	if (lid == 0x0804)
		set_lang_zh_cn();
	else
		set_lang_en();
#else
	std::string user_locale = std::locale("").name();
	if (user_locale.find("CN") != string::npos)
		set_lang_zh_cn();
	else
		set_lang_en();
#endif
}

void Locale::set_lang_en()
{
	name_v_refint				= "On-chip VRefInt (V):";
	name_v_ext_power			= "Charge Power Voltage (V):";
	name_div_prop				= "MOS_VD Input Divider Proportion:";
	name_r_samp					= "Sampling Resistor Value (Ω):";
	name_r_extra				= "Extra Resistance (Ω):";
	name_i_max					= "Maximum Charge Current (A):";
	name_p_mos_max				= "MOS Maximum Dissipated Power (W):";
	
	name_v_bat_detect_th		= "Battery Detection Voltage (V):";
	name_v_dac_adj_step			= "VDAC Adjustment Step (mV):";
	name_v_bat_dec_th			= "VBat Decline Threshold (mV):";
	
	name_exp_current			= "Exp Current (mA):";
	name_exp_voltage			= "Exp Voltage (V):";
	name_exp_charge				= "Exp Charge (mAh):";
	name_opt_stage_const_v		= "Const-Voltage Stage";
	name_min_current			= "Min Current (mA):";
	
	state_device_disconnected	= "Connecting...";
	state_battery_disconnected	= "No Battery";
	state_battery_connected		= "Ready";
	state_battery_charging_cc	= "Charging... (CC)";
	state_battery_charging_cv	= "Charging... (CV)";
	state_charge_completed		= "Complete";
	state_charge_stopped		= "Stopped";
	
	event_device_connnect		= "Device Connected";
	event_device_disconnect		= "Device Disconnected";
	event_battery_connect		= "Battery Connected";
	event_battery_disconnect	= "Battery Disconnected";
	event_new_data				= "Data Updated";
	event_charge_complete		= "Charge Completed";
	event_charge_brake			= "! Emergency: Stopped";
	
	event_buffer_full			= "Record buffer is full...";
	
	name_bat_voltage			= "Battery Voltage";
	name_bat_current			= "Charge Current";
	
	title_main_window			= "Charge Monitor";
	caption_button_on			= "ON";
	caption_button_off			= "OFF";
	caption_button_config		= "  Config...  ";
	caption_button_calibrate	= "Calibrate...";
	caption_button_open			= "Open";
	caption_button_save			= "Save";
	caption_button_apply		= "Apply";
	
	title_dialog_input          = "Input Value";
	message_input_v_bat			= "Input your measurement of battery voltage (V):";
	message_report_vrefint		= "On-chip VRefInt is calculated:";
	
	message_invalid_input		= "Invalid input.";
	
	title_dialog_open_file		= "Open CSV File";
	title_dialog_save_file		= "Save As CSV File (Interval: 0.1 s)";
	name_file_filter_csv		= "CSV Files (.csv)";
	caption_button_ok			= "_OK";
	caption_button_cancel		= "_Cancel";
	message_failed_to_save_file = "Failed to save as file.";
}

void Locale::set_lang_zh_cn()
{
	name_v_refint				= "单片机内部参考电压 (V):";
	name_v_ext_power			= "充电电源电压 (V):";
	name_div_prop				= "MOS 管漏极电压 ADC 输入分压率:";
	name_r_samp					= "电流取样电阻阻值 (Ω):";
	name_r_extra				= "充电线路额外电阻估值 (Ω):";
	name_i_max					= "最大充电电流 (A):";
	name_p_mos_max				= "MOS 管最大耗散功率 (W):";
	
	name_v_bat_detect_th		= "电池探测阈值电压 (V):";
	name_v_dac_adj_step			= "VDAC 调节步进电压 (mV):";
	name_v_bat_dec_th			= "负压差检测阈值 (mV):";
	
	name_exp_current			= "电流设定 (mA):";
	name_exp_voltage			= "电压限定 (V):";
	name_exp_charge				= "充入电量 (mAh):";
	name_opt_stage_const_v		= "恒压阶段";
	name_min_current			= "最小电流 (mA):";
	
	state_device_disconnected	= "正在寻找设备...";
	state_battery_disconnected	= "电池未接入";
	state_battery_connected		= "就绪";
	state_battery_charging_cc	= "恒流充电中...";
	state_battery_charging_cv	= "恒压充电中...";
	state_charge_completed		= "充电完成";
	state_charge_stopped		= "充电中断";
	
	event_device_connnect		= "设备已连接";
	event_device_disconnect		= "设备已断开";
	event_battery_connect		= "电池已接入电路";
	event_battery_disconnect	= "电池已断开连接";
	event_new_data				= "数据已更新";
	event_charge_complete		= "充电已完成";
	event_charge_brake			= "! 紧急中断";
	
	event_buffer_full			= "数据已满...";
	
	name_bat_voltage			= "电池电压";
	name_bat_current			= "充电电流";
	
	title_main_window			= "充电监视器";
	caption_button_on			= "开始";
	caption_button_off			= "停止";
	caption_button_config		= "配置...";
	caption_button_calibrate	= "校准...";
	caption_button_open			= "打开";
	caption_button_save			= "保存";
	caption_button_apply		= "应用";
	
	title_dialog_input          = "输入";
	message_input_v_bat			= "输入电池实际电压 (V):";
	message_report_vrefint		= "单片机内部参考电压计算值:";
	
	message_invalid_input		= "无效输入。";
	
	title_dialog_open_file		= "打开 CSV 文件";
	title_dialog_save_file		= "保存为 CSV 文件 (数据间隔为 0.1 s)";
	name_file_filter_csv		= "CSV 文件 (.csv)";
	caption_button_ok			= "确定(_O)";
	caption_button_cancel		= "取消(_C)";
	message_failed_to_save_file = "保存文件失败。";
}

inline ostream& operator<<(ostream& ost, const system_clock::time_point& t)
{
	const time_t t_c = system_clock::to_time_t(t);
	return ost << put_time(localtime(&t_c), "%H:%M:%S") << flush;
}

void Locale::get_control_status_str(const ChargeStatus& st, Glib::ustring& str) const
{
	ostringstream sst; sst.setf(ios::fixed);
	
	sst << setprecision(3) << st.bat_voltage << " V  "
		<< setprecision(0) << st.bat_current * 1000.0 << " mA" << endl
		
		<< "MOS: " << setprecision(0) << st.mos_power * 1000.0 << " mW" << endl
	    << "BAT: " << st.bat_power * 1000.0 << " mW" << endl << endl
	    
		<< "DAC: " << setprecision(3) << st.dac_voltage << " V" << endl << endl

		<< "VMax: " << setprecision(3) << st.bat_voltage_max << " V" << endl
		<< "IMax: " << setprecision(0) << st.bat_current_max * 1000.0 << " mA" << endl << endl;
	
	if (st.flag_ir_measured)
		sst << "r (DC): " << setprecision(0) << st.ir * 1000.0 << " mOhm" << endl << endl;
	
	sst << st.t_charge_start << " " << setprecision(3) << st.bat_voltage_initial << " V" << endl;
	if (st.control_state == Charge_Completed || st.control_state == Charge_Stopped)
		sst << st.t_charge_stop << " " << setprecision(3) << st.bat_voltage_final << " V" << endl;
	
	sst << setprecision(0) << st.bat_charge * 1000.0 / 3600.0 << " mAh  "
		                   << st.bat_energy * 1000.0 / 3600.0 << " mWh" << endl;
	
	str = control_state_to_str(st.control_state) + '\n';
	str += sst.str();
}
