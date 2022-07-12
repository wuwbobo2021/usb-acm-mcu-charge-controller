// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "control_layer.h"

#include <sstream>
#include <ctime>
#include <iomanip>

using namespace std::this_thread;

static inline ostream& operator<<(ostream& ost, const system_clock::time_point& t)
{
	const time_t t_c = system_clock::to_time_t(t);
	return ost << put_time(localtime(&t_c), "%H:%M:%S") << flush;
}

ostream& operator<<(ostream& ost, const ChargeStatus& ch)
{
	ost.setf(ios::fixed);
	ost << ch.control_state << endl
	
	    << setprecision(3) << ch.bat_voltage << " V  "
		<< setprecision(0) << ch.bat_current * 1000.0 << " mA" << endl
		
		<< "MOS: " << setprecision(0) << ch.mos_power * 1000.0 << " mW" << endl
	    << "BAT: " << ch.bat_power * 1000.0 << " mW" << endl << endl
	    
		<< "DAC: " << setprecision(3) << ch.dac_voltage << " V" << endl << endl
		
		<< "Expected: " << endl
		
	    << setprecision(3) << ch.exp_voltage << " V  "
		<< setprecision(0) << ch.exp_current * 1000.0 << " mA" << endl
		<< ch.exp_charge * 1000.0 / 3600.0 << " mAh" << endl << endl
		
		<< "VMax: " << setprecision(3) << ch.bat_voltage_max << " V" << endl
		<< "IMax: " << setprecision(0) << ch.bat_current_max * 1000.0 << " mA" << endl << endl;
	
	if (ch.flag_ir_measured)
		ost << "r: " << setprecision(0) << ch.ir * 1000.0 << " mOhm" << endl;
	
	ost << ch.t_charge_start << " " << setprecision(3) << ch.bat_voltage_initial << " V" << endl;
	if (ch.control_state == Charge_Completed || ch.control_state == Charge_Stopped)
		ost << ch.t_charge_stop << " " << setprecision(3) << ch.bat_voltage_final << " V" << endl;
	ost << endl;
	
	ost << setprecision(0) << ch.bat_charge * 1000.0 / 3600.0 << " mAh  "
		                   << ch.bat_energy * 1000.0 / 3600.0 << " mWh" << endl;
	
	return ost;
}

ChargeStatus::operator string() const
{
	ostringstream sst; sst << *this;
	return sst.str();
}


ChargeControlLayer::ChargeControlLayer()
{
	status.exp_voltage = 1.2; status.exp_current = 0.1; status.exp_charge = 5400.0;
	data_callback_ptr = MemberFuncDataCallbackPtr<ChargeControlLayer, &ChargeControlLayer::data_callback>(this);
	thread_control = new thread(&ChargeControlLayer::control_loop, this);
}

ChargeControlLayer::~ChargeControlLayer()
{
	flag_close = true;
	thread_control->join();
	delete thread_control;
}

bool ChargeControlLayer::set_charge_exp(float cur, float vol, float chg)
{
	if (cur < 0.01 || vol < conf.v_bat_detect_th + 0.1 || chg < 1.0
	|| (status.control_state == Charge_Completed && vol <= status.exp_voltage))
		return false;
	
	if (cur > conf.i_max) cur = conf.i_max;
	if (vol > conf.v_ext_power - 0.1) vol = conf.v_ext_power - 0.1;
	
	status.exp_current = cur; status.exp_voltage = vol; status.exp_charge = chg;
	return true;
}

void ChargeControlLayer::calibrate(float v_adc1_actual)
{
	conf.v_refint = comm.vrefint_calibrate(v_adc1_actual);
}

void ChargeControlLayer::calibrate(float v_bat_actual, float v_ext_power, float r_extra)
{
	if (v_ext_power > 0) conf.v_ext_power = v_ext_power;
	if (r_extra >= 0) conf.r_extra = r_extra;
	float v_adc1_actual = conf.v_ext_power - v_bat_actual - status.bat_current * conf.r_extra;
	calibrate(v_adc1_actual);
}

bool ChargeControlLayer::start_charging()
{
	if (status.control_state != Battery_Connected
	&&  status.control_state != Charge_Completed
	&&  status.control_state != Charge_Stopped)
		return false;
	
	flag_start = true; return true;
}

void ChargeControlLayer::stop_charging()
{
	if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV)
		flag_stop = true;
}

void ChargeControlLayer::control_loop()
{
	while (true) {
		if (flag_close) {
			if (comm.is_connected()) comm.disconnect(); //the microcontroller will stop charging by itself
			return;
		}
		
		if (! comm.is_connected()) {
			if (status.control_state != Device_Disconnected) {
				// disconnect event
				do_stop_charging(false); status.control_state = Device_Disconnected;
				event_callback_ptr.call(Event_Device_Disconnect);
				this_thread::sleep_for(milliseconds(500));
			}
			
			if (! comm.connect(data_callback_ptr)) {
				this_thread::sleep_for(milliseconds(500)); continue;
			}
			
			// connect event
			comm.dac_output(0);
			status.reset(); cnt_shake_failed = 0; wait_for_new_data();
			if (status.bat_voltage >= conf.v_bat_detect_th)
				status.control_state = Battery_Connected;
			else
				status.control_state = Battery_Disconnected;
			event_callback_ptr.call(Event_Device_Connect); continue;
		}
		
		// handshake to avoid reset of microcontroller by the watchdog
		if (ms_since(t_last_shake) >= Shake_Interval_Max / 2.0) {
			if (comm.shake()) {
				cnt_shake_failed = 0; t_last_shake = steady_clock::now();
			} else {
				if (++cnt_shake_failed > 4) {
					//disconnect event
					do_stop_charging(false); status.control_state = Device_Disconnected;
					comm.disconnect();
					event_callback_ptr.call(Event_Device_Disconnect);
				}
				this_thread::sleep_for(milliseconds(500)); continue;
			}
		}
		
		// wait for new data callback
		wait_for_new_data();
		
		// check for battery connection
		if (status.bat_voltage >= conf.v_bat_detect_th && status.bat_voltage < conf.v_ext_power - 0.002) {
			if (status.control_state == Battery_Disconnected) {
				// battery connect event
				status.control_state = Battery_Connected;
				event_callback_ptr.call(Event_Battery_Connect);
			}
		} else {
			if (status.control_state != Battery_Disconnected) {
				// battery disconnect event
				do_stop_charging(false);
				status.control_state = Battery_Disconnected;
				event_callback_ptr.call(Event_Battery_Disconnect);
			}
			this_thread::sleep_for(milliseconds(100)); continue;
		}
		
		if (flag_start) {
			flag_start = false;
			cnt_v_dec = 0;
			float v = status.bat_voltage; status.reset();
			status.control_state = Battery_Charging_CC;
			status.bat_voltage_max = status.bat_voltage_initial = status.bat_voltage = v;
			status.t_bat_voltage_max = steady_clock::now();
			status.t_charge_start = system_clock::now();
			this_thread::sleep_for(milliseconds(10));
		}
		else if (flag_stop) {
			do_stop_charging(true); flag_stop = false;
			status.control_state = Charge_Stopped;
		}
		else if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV) {
			// check for emergency stop
			if (status.bat_current > 1.1 * conf.i_max || status.mos_power > 1.1 * conf.p_mos_max) {
				complete_charging(false); continue;
			}
			
			// check for voltage decline
			if (status.bat_voltage_max - status.bat_voltage >= conf.v_bat_dec_th)
				cnt_v_dec++;
			else
				cnt_v_dec = 0;
			
			// check for completion
			if (status.bat_charge >= status.exp_charge || cnt_v_dec > 5) {
				complete_charging(); continue;
			}
			
			bool p_mos_reached_max = (status.mos_power > conf.p_mos_max);	
			float dac_voltage = status.dac_voltage;
			
			// internal resistance measuring
			if (! status.flag_ir_measured && status.bat_current >= status.exp_current * 3.0 / 4.0) {
				float bat_voltage_prev = status.bat_voltage, bat_current_prev = status.bat_current;
				comm.dac_output(0);
				while (status.bat_current >= bat_current_prev / 5.0)
					wait_for_new_data();
				
				if (status.bat_current < bat_current_prev)
					status.ir =   (bat_voltage_prev - status.bat_voltage)
					            / (bat_current_prev - status.bat_current);
				status.flag_ir_measured = true;
				
				comm.dac_output(dac_voltage);
				while (status.bat_current < bat_current_prev - 0.05)
					wait_for_new_data();
				continue;
			}
			
			// const current stage
			if (status.control_state == Battery_Charging_CC) {
				// check for battery voltage
				if (status.bat_voltage >= status.exp_voltage) {
					if (! conf.opt_stage_const_v)
						complete_charging();
					else
						status.control_state = Battery_Charging_CV;
					continue;
				}
				
				// current adjustment
				float diff_current = status.bat_current - status.exp_current;
				if (p_mos_reached_max)
					dac_voltage -= conf.v_dac_adj_step * 3.0 * status.mos_power / conf.p_mos_max;
				else if (diff_current > 0.003)
					dac_voltage -= conf.v_dac_adj_step * (diff_current / 0.006);
				else if (diff_current < -0.003)
					dac_voltage += conf.v_dac_adj_step * (-diff_current / 0.006);
			}
			
			// const voltage stage
			else {
				// check for low current threshold
				if (dac_voltage == 0 || status.bat_current < status.exp_current / 100.0) {
					complete_charging(); continue;
				} else if (p_mos_reached_max)
					dac_voltage -= conf.v_dac_adj_step * status.mos_power / conf.p_mos_max;
				else {
					 // current should be lower when the internal resistance of the battery become higher
					float diff_voltage = status.bat_voltage - status.exp_voltage;
					if (diff_voltage > 0.002)
						dac_voltage -= conf.v_dac_adj_step;
				}
			}
			
			// apply the adjusted DAC voltage value
			if (dac_voltage != status.dac_voltage) {
				comm.dac_output(dac_voltage);
				status.dac_voltage = dac_voltage;
			}
		}
		else {
			this_thread::sleep_for(milliseconds(100)); //idle
		}
	}
}

void ChargeControlLayer::wait_for_new_data()
{
	flag_new_data = false;
	while (!flag_new_data && !flag_close && ms_since(t_last_shake) < Shake_Interval_Max - 2000)
		this_thread::sleep_for(milliseconds(10));
}

void ChargeControlLayer::complete_charging(bool successful) {
	do_stop_charging(true);
	if (successful) {
		status.control_state = Charge_Completed;
		event_callback_ptr.call(Event_Charge_Complete);
	} else {
		status.control_state = Charge_Stopped;
		event_callback_ptr.call(Event_Charge_Brake);
	}
}

void ChargeControlLayer::do_stop_charging(bool remeasure_voltage)
{
	if (status.control_state != Battery_Charging_CC && status.control_state != Battery_Charging_CV) return;
	
	if (comm.is_connected() && status.dac_voltage > 0) comm.dac_output(0);
	status.dac_voltage = 0;
	status.t_charge_stop = system_clock::now();
	
	if (comm.is_connected() && remeasure_voltage) {
		this_thread::sleep_for(milliseconds(1000)); wait_for_new_data();
	}
	status.bat_voltage_final = status.bat_voltage;
}

void ChargeControlLayer::data_callback(float udiv, float usamp)
{
	if ((status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV)
	&&  !flag_start) {
		float dur_sec = ms_since(status.t_last_update) / 1000.0;
		status.bat_charge += status.bat_current * dur_sec;
		status.bat_energy += (status.bat_power - status.bat_current * status.ir) * dur_sec;
	}
	
	status.bat_current = usamp / conf.r_samp;
	status.bat_voltage = (conf.v_ext_power - udiv / conf.div_per) - status.bat_current * conf.r_extra;
	
	status.mos_power = (udiv / conf.div_per - usamp) * status.bat_current;
	status.bat_power = status.bat_voltage * status.bat_current;
	
	if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV) {
		if (status.bat_voltage > status.bat_voltage_max) {
			status.bat_voltage_max = status.bat_voltage;
			status.t_bat_voltage_max = steady_clock::now();
		}
		if (status.bat_current > status.bat_current_max) {
			status.bat_current_max = status.bat_current;
			status.t_bat_current_max = steady_clock::now();
		}
	}
	
	status.t_last_update = steady_clock::now();
	flag_new_data = true;
	event_callback_ptr.call(Event_New_Data);
}
