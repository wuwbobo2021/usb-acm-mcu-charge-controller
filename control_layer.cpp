// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "control_layer.h"

using namespace std::this_thread;

ChargeControlLayer::ChargeControlLayer()
{
	data_callback_ptr = MemberFuncDataCallbackPtr<ChargeControlLayer, &ChargeControlLayer::data_callback>(this);
	thread_control = new thread(&ChargeControlLayer::control_loop, this);
}

ChargeControlLayer::~ChargeControlLayer()
{
	flag_close = true;
	thread_control->join();
	delete thread_control;
}

bool ChargeControlLayer::set_hard_config(ChargeControlConfig new_conf)
{
	if (new_conf.v_refint < 0.1 || new_conf.v_refint >= 4.8
	||  new_conf.v_ext_power < 0.1
	||  new_conf.div_prop < 0.01 || new_conf.div_prop > 1.0
	||  new_conf.r_samp <= 0.0
	||  new_conf.r_extra < 0.0
	||  new_conf.i_max < 0.1 || new_conf.i_max > 3.0
	||  new_conf.p_mos_max < 0.4 || new_conf.p_mos_max > 5.0
	
	||  new_conf.v_bat_detect_th < 0.1 || new_conf.v_bat_detect_th >= 3.0
	||  new_conf.v_dac_adj_step < 0.001 || new_conf.v_dac_adj_step > 0.1
	||  new_conf.v_bat_dec_th < 0.001 || new_conf.v_bat_dec_th > 0.005)
		return false;
	
	conf = new_conf;
	comm.set_voltage_vrefint(conf.v_refint);
	return true;
}

bool ChargeControlLayer::set_charge_param(ChargeParameters new_param)
{
	if (new_param.exp_current < 0.009
	||  new_param.exp_voltage < status.bat_voltage
	||  new_param.exp_charge < 1.0)
		return false;
	
	param = new_param;
	
	if (param.exp_current > conf.i_max)
		param.exp_current = conf.i_max;
	if (param.exp_voltage > conf.v_ext_power - 0.1)
		param.exp_voltage = conf.v_ext_power - 0.1;
	
	return true;
}

void ChargeControlLayer::calibrate(float v_bat_actual)
{
	float v_adc1_actual = (conf.v_ext_power - v_bat_actual - status.bat_current * conf.r_extra)
	                    * conf.div_prop;
	conf.v_refint = comm.vrefint_calibrate(v_adc1_actual);
}

bool ChargeControlLayer::dac_scan()
{
	if (status.control_state != Battery_Connected
	&&  status.control_state != Charge_Completed
	&&  status.control_state != Charge_Stopped)
		return false;
	
	return flag_dac_scan = true;
}

void ChargeControlLayer::stop_dac_scan()
{
	if (status.control_state == DAC_Scanning)
		flag_stop_dac_scan = true;
}

bool ChargeControlLayer::start_charging()
{
	if (status.control_state != Battery_Connected
	&&  status.control_state != Charge_Completed
	&&  status.control_state != Charge_Stopped)
		return false;
	
	return flag_start = true;
}

void ChargeControlLayer::stop_charging()
{
	if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV)
		flag_stop = true;
}

/*------------------------------ private functions ------------------------------*/

void ChargeControlLayer::control_loop()
{
	while (true) {
		if (flag_close) {
			if (comm.is_connected()) comm.disconnect(); return;
		}
		
		if (!check_comm() || !check_shake()) continue;
		
		wait_for_new_data(); //wait for new data callback
		if (! check_bat_connection()) continue;
		
		if (flag_start) {
			flag_start = false;
			cnt_i_min = cnt_v_dec = cnt_v_max_detect = 0;
			float v = status.bat_voltage;
			status.reset();
			status.control_state = Battery_Charging_CC;
			status.bat_voltage_max = status.bat_voltage_initial = status.bat_voltage = v;
			status.t_bat_voltage_max = steady_clock::now();
			status.t_charge_start = system_clock::now();
		}
		else if (flag_stop) {
			do_stop_charging(true); flag_stop = false;
			status.control_state = Charge_Stopped;
		}
		else if (flag_dac_scan) {
			flag_dac_scan = false;
			float v = status.bat_voltage;
			status.reset();
			status.control_state = DAC_Scanning; status.bat_voltage = v;
			comm.opt_measure_vdda = false; //measuring of vdda causes interruption
			comm.dac_output(status.dac_voltage = 0);
		}
		else if (flag_stop_dac_scan) {
			flag_stop_dac_scan = false;
			comm.dac_output(status.dac_voltage = 0);
			comm.opt_measure_vdda = true;
			status.control_state = Battery_Connected;
		}
		else if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV) {
			// check for emergency stop
			if (status.bat_current > 1.1 * conf.i_max || status.mos_power > 1.1 * conf.p_mos_max) {
				complete_charging(false); continue;
			}
			
			// internal resistance (DC) measuring
			if (! status.flag_ir_measured && ms_since(status.t_charge_start) >= 60 * 1000) {
				measure_ir(); continue;
			}
			
			// check for expected charge
			if (status.bat_charge >= param.exp_charge) {
				complete_charging(); continue;
			}
			
			float dac_voltage = status.dac_voltage;
			int cnt_steps = 0;
			
			// check for MOS dissipated power
			bool p_mos_reached_max = (status.mos_power > conf.p_mos_max);
			if (p_mos_reached_max)
				cnt_steps = -3.0 * status.mos_power / conf.p_mos_max;
			
			// const current stage
			if (status.control_state == Battery_Charging_CC) {
				// check for expected voltage
				if (status.bat_voltage >= param.exp_voltage) {
					if (! param.opt_stage_const_v)
						complete_charging();
					else
						status.control_state = Battery_Charging_CV;
					continue;
				}
				
				// check for voltage decline
				if (status.bat_voltage_max - status.bat_voltage >= conf.v_bat_dec_th) {
					cnt_v_dec++;
					if (cnt_v_dec == 1)
						i_dec_start = status.bat_current;
					if (cnt_v_dec == 20)
						if (status.bat_current - i_dec_start < -0.01) cnt_v_dec = 0;
				} else
					cnt_v_dec = 0;
				if (cnt_v_dec >= 20) {
					complete_charging(); continue;
				}
				
				// current adjustment
				float diff_current = status.bat_current - param.exp_current;
				if (!p_mos_reached_max)
					cnt_steps = -diff_current / 0.003;
			}
			
			// const voltage stage
			else {
				// check for low current threshold
				if (status.bat_current < param.min_current)
					cnt_i_min++;
				else
					cnt_i_min = 0;
				
				if (dac_voltage == 0 || cnt_i_min >= 10) {
					complete_charging(); continue;
				} else {
					 // current should be lower when the internal resistance of the battery become higher
					float diff_voltage = status.bat_voltage - param.exp_voltage;
					if (!p_mos_reached_max && diff_voltage > 0.002)
						cnt_steps = -1;
				}
			}
			
			if (cnt_steps > 15) cnt_steps = 15; if (cnt_steps < -15) cnt_steps = -15;
			
			// apply DAC voltage adjustment
			dac_voltage += cnt_steps * conf.v_dac_adj_step;
			if (dac_voltage < conf.v_dac_adj_step) dac_voltage = conf.v_dac_adj_step;
			if (dac_voltage > comm.voltage_vdda()) dac_voltage = comm.voltage_vdda();
			if (dac_voltage != status.dac_voltage) {
				comm.dac_output(dac_voltage);
				status.dac_voltage = dac_voltage;
			}
		}
		else if (status.control_state == DAC_Scanning) {
			if (status.dac_voltage >= comm.voltage_vdda()) {
				comm.dac_output(status.dac_voltage = 0);
				comm.opt_measure_vdda = true;
				status.control_state = Battery_Connected;
				event_callback_ptr.call(Event_Scan_Complete);
			}
			status.dac_voltage += conf.v_dac_adj_step;
			comm.dac_output(status.dac_voltage);
		}
		else {
			comm.dac_output(status.dac_voltage = 0);
			this_thread::sleep_for(milliseconds(400)); //idle
		}
	}
}

bool ChargeControlLayer::check_comm()
{
	if (! comm.is_connected()) {
		if (status.control_state != Device_Disconnected) {
			// disconnect event
			do_stop_charging(false); status.control_state = Device_Disconnected;
			event_callback_ptr.call(Event_Device_Disconnect);
		}
		
		this_thread::sleep_for(milliseconds(500));
		if (! comm.connect(data_callback_ptr)) return false;
		
		// connect event
		comm.dac_output(0);
		status.reset(); cnt_shake_failed = 0; wait_for_new_data();
		if (status.bat_voltage >= conf.v_bat_detect_th)
			status.control_state = Battery_Connected;
		else
			status.control_state = Battery_Disconnected;
		event_callback_ptr.call(Event_Device_Connect);
	}
	
	return true;
}

bool ChargeControlLayer::check_shake()
{
	if (! comm.is_connected()) return false;
	if (ms_since(t_shake_suc) < Shake_Interval_Max / 2.0) return true;
	if (ms_since(t_shake) < 150) return false; //avoid short interval
	
	// send handshake command to avoid reset of microcontroller by the watchdog
	bool shake_suc = comm.shake(); t_shake = steady_clock::now();
	
	if (shake_suc) {
		cnt_shake_failed = 0; t_shake_suc = steady_clock::now();
	} else {
		if (++cnt_shake_failed > 4) {
			// disconnect event
			do_stop_charging(false); status.control_state = Device_Disconnected;
			comm.disconnect();
			event_callback_ptr.call(Event_Device_Disconnect);
		}
	}
	
	return shake_suc;
}

bool ChargeControlLayer::check_bat_connection()
{
	if (! comm.is_connected()) return false;
	
	if (status.bat_voltage >= conf.v_bat_detect_th && status.bat_voltage < conf.v_ext_power - 0.003) {
		if (status.control_state == Battery_Disconnected) {
			// battery connect event
			status.control_state = Battery_Connected;
			event_callback_ptr.call(Event_Battery_Connect);
		}
		return true;
	} else {
		if (status.control_state != Battery_Disconnected) {
			// battery disconnect event
			do_stop_charging(false);
			status.control_state = Battery_Disconnected;
			event_callback_ptr.call(Event_Battery_Disconnect);
		}
		return false;
	}
}

bool ChargeControlLayer::wait_for_new_data()
{
	flag_new_data = false;
	while (comm.is_connected() && !flag_new_data && !flag_close) {
		check_shake();
		this_thread::sleep_for(milliseconds(10));
	}
	return flag_new_data;
}

bool ChargeControlLayer::measure_ir()
{
	if (status.control_state != Battery_Charging_CC && status.control_state != Battery_Charging_CV) return false;
	if (status.bat_current < 0.01) return false;
	
	comm.opt_measure_vdda = false;
	comm.shake();
	float bat_voltage_prev = status.bat_voltage, bat_current_prev = status.bat_current;
	while (status.bat_current > bat_current_prev / 5.0) {
		check_shake(); comm.dac_output(0);
		if (! wait_for_new_data()) {
			comm.opt_measure_vdda = true; return false;
		}
	}
	
	status.ir =   (bat_voltage_prev - status.bat_voltage)
	            / (bat_current_prev - status.bat_current);
	status.flag_ir_measured = true;
	
	while (status.bat_current < bat_current_prev - 0.05) {
		check_shake(); comm.dac_output(status.dac_voltage);
		if (! wait_for_new_data()) break;
	}
	
	comm.opt_measure_vdda = true;
	return true;
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
	status.bat_voltage = (conf.v_ext_power - udiv / conf.div_prop) - status.bat_current * conf.r_extra;
	
	status.mos_power = (udiv / conf.div_prop - usamp) * status.bat_current;
	status.bat_power = status.bat_voltage * status.bat_current;
	
	if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV) {
		if (status.bat_voltage > status.bat_voltage_max) {
			cnt_v_max_detect++;
			if (cnt_v_max_detect > 10) {
				cnt_v_max_detect = 0;
				status.bat_voltage_max = status.bat_voltage;
				status.t_bat_voltage_max = steady_clock::now();
			}
		} else
			cnt_v_max_detect = 0;
		
		if (status.bat_current > status.bat_current_max) {
			status.bat_current_max = status.bat_current;
			status.t_bat_current_max = steady_clock::now();
		}
	}
	
	status.t_last_update = steady_clock::now();
	flag_new_data = true;
	event_callback_ptr.call(Event_New_Data);
}
