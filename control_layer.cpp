// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "control_layer.h"

using namespace std::this_thread;
using namespace SimpleCairoPlot; //Range, CircularBuffer

ChargeControlLayer::ChargeControlLayer():
	buf_bat_voltage(20), buf_bat_current(30)
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
	||  new_conf.v_bat_dec_th < 0.001 || new_conf.v_bat_dec_th > 1.0)
		return false;
	
	conf = new_conf;
	comm.set_voltage_vrefint(conf.v_refint);
	return true;
}

bool ChargeControlLayer::set_charge_param(ChargeParameters new_param)
{
	if (new_param.exp_current < 0.001
	||  new_param.exp_voltage < status.bat_voltage_oc - 0.1
	||  new_param.exp_charge < 1.0)
		return false;
	
	param = new_param;
	
	if (param.exp_current > conf.i_max)
		param.exp_current = conf.i_max;
	if (param.exp_voltage > conf.v_ext_power)
		param.exp_voltage = conf.v_ext_power;
	
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
			status.set_state(Battery_Charging_CC);
			status.bat_voltage_max = status.bat_voltage_initial = status.bat_voltage;
			status.t_charge_start = status.t_bat_voltage_max = steady_clock::now();
			disable_scrolling_average(); //for the adjustment at first
		}
		else if (flag_stop) {
			do_stop_charging(true); flag_stop = false;
			status.control_state = Charge_Stopped;
		}
		else if (flag_dac_scan) {
			flag_dac_scan = false;
			status.set_state(DAC_Scanning);
			dac_output(0); disable_scrolling_average();
		}
		else if (flag_stop_dac_scan) {
			flag_stop_dac_scan = false;
			dac_output(0); enable_scrolling_average();
			status.set_state(Battery_Connected);
		}
		else if (status.control_state == DAC_Scanning) {
			if (status.dac_voltage >= comm.voltage_vdda()) {
				dac_output(0); enable_scrolling_average();
				status.set_state(Battery_Connected);
				event_callback_ptr.call(Event_Scan_Complete); continue;
			}
			status.dac_voltage += conf.v_dac_adj_step;
			comm.dac_output(status.dac_voltage);
		}
		
		else if (status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV) {
			// check for emergency stop
			if (status.bat_current > 1.1 * conf.i_max || status.mos_power > 1.1 * conf.p_mos_max) {
				complete_charging(false); continue;
			}
			
			// internal resistance (DC) measuring
			if (!status.flag_ir_measured &&  ms_since(status.t_charge_start) >= 30 * 1000
			||   status.flag_ir_measured &&  param.opt_stage_const_v && ms_since(status.t_ir_measure) >=  180 * 1000
			||   status.flag_ir_measured && !param.opt_stage_const_v && ms_since(status.t_ir_measure) >= 1800 * 1000)
				measure_ir();
			
			// check for expected charge
			if (status.bat_charge >= param.exp_charge) {
				complete_charging(); dbg_print("complete: exp_charge"); continue;
			}
			
			// new DAC voltage to be determined
			float dac_voltage = status.dac_voltage;
			int cnt_steps = 0;
			
			// check for MOS dissipated power
			bool p_mos_reached_max = flag_scrolling_average && (status.mos_power > conf.p_mos_max);
			if (p_mos_reached_max)
				cnt_steps = -3.0 * status.mos_power / conf.p_mos_max;
			
			bool averaged = flag_scrolling_average && buf_bat_voltage.is_full();
			
			// const current stage
			if (status.control_state == Battery_Charging_CC) {
				// check for expected voltage
				if (averaged && status.flag_ir_measured
				&&  status.bat_voltage_oc >= param.exp_voltage_oc) {
					complete_charging(); dbg_print("complete: exp_voltage_oc"); continue;
				}
				if (averaged && status.bat_voltage >= param.exp_voltage
				|| !averaged && status.bat_voltage >= param.exp_voltage + 0.05) {
					if (! param.opt_stage_const_v) {
						complete_charging(); dbg_print("complete: exp_voltage");
					} else {
						status.control_state = Battery_Charging_CV;
						enable_scrolling_average(); //make sure of this
					}
					continue;
				}
				
				// check for voltage decline
				if (status.bat_voltage_max - status.bat_voltage >= conf.v_bat_dec_th
				&&  averaged && buf_bat_current.get_value_range().length() < 0.005) {
					complete_charging(); dbg_print("complete: v_bat_dec_th"); continue;
				}
				
				// current adjustment
				float diff_current = status.bat_current - param.exp_current;
				if (! p_mos_reached_max)
					cnt_steps = -diff_current / 0.003;
			}
			
			// const voltage stage
			else {
				// check for low current threshold
				if (dac_voltage == 0 || status.bat_current < param.min_current) {
					complete_charging(); dbg_print("complete: min_current"); continue;
				} else {
					 // current should be lower when the internal resistance of the battery become higher
					float diff_voltage = status.bat_voltage - param.exp_voltage;
					if (!p_mos_reached_max && diff_voltage > 0.002)
						cnt_steps = -1;
				}
			}
			
			// apply DAC voltage adjustment
			cnt_steps = Range(-15, 15).fit_value(cnt_steps);
			dac_voltage += cnt_steps * conf.v_dac_adj_step;
			dac_voltage = Range(conf.v_dac_adj_step, comm.voltage_vdda()).fit_value(dac_voltage);
			dac_output(dac_voltage);
			
			float diff_dac_voltage = dac_voltage - status.dac_voltage;
			if (diff_dac_voltage) {
				if (Range(-0.01, 0.01).contain(diff_dac_voltage))
					enable_scrolling_average();
				else
					disable_scrolling_average();
			}
		}
		else {
			dac_output(0);
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
		if (! conf.v_refint) conf.v_refint = comm.voltage_vrefint();
		
		// connect event
		dac_output(0);
		enable_scrolling_average();
		status.reset(); cnt_shake_failed = 0; wait_for_new_data();
		if (check_bat_connection())
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
	
	// send handshake command, otherwise the mcu will stop charging
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
	
	if (Range(conf.v_bat_detect_th, conf.v_ext_power - conf.v_bat_detect_th).contain(status.bat_voltage)) {
		if (status.control_state == Battery_Disconnected) {
			// battery connect event
			for (int i = 0; i < 5; i++) wait_for_new_data();
			buf_bat_voltage.clear(); buf_bat_voltage.push(status.bat_voltage);
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
	
	disable_scrolling_average();
	
	float bat_voltage_prev = status.bat_voltage, bat_current_prev = status.bat_current;
	steady_clock::time_point t = steady_clock::now();
	
	while (status.bat_current > bat_current_prev / 5.0) {
		check_shake(); comm.dac_output(0);
		if (!wait_for_new_data() || ms_since(t) > 3000) {
			enable_scrolling_average(); return false;
		}
	}
	
	status.ir =   (bat_voltage_prev - status.bat_voltage)
	            / (bat_current_prev - status.bat_current);
	status.flag_ir_measured = true;
	status.t_ir_measure = steady_clock::now();
	
	t = steady_clock::now();
	while (status.bat_voltage < 0.99 * bat_voltage_prev) {
		check_shake(); comm.dac_output(status.dac_voltage);
		if (!wait_for_new_data() || ms_since(t) > 3000) break;
	}
	wait_for_new_data(); wait_for_new_data();
	
	enable_scrolling_average();
	return true;
}

void ChargeControlLayer::complete_charging(bool successful)
{
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
	
	if (comm.is_connected() && status.dac_voltage > 0)
		dac_output(0);
	
	status.control_state = Charge_Stopped;
	status.t_charge_stop = steady_clock::now();
	
	if (comm.is_connected() && remeasure_voltage) {
		disable_scrolling_average();
		this_thread::sleep_for(milliseconds(1000));
		wait_for_new_data();
	}
	status.bat_voltage_final = status.bat_voltage;
	enable_scrolling_average(); //make sure of this
}

void ChargeControlLayer::data_callback(float udiv, float usamp)
{
	if ((status.control_state == Battery_Charging_CC || status.control_state == Battery_Charging_CV)
	&&  !flag_start) {
		float dur_sec = ms_since(status.t_last_update) / 1000.0;
		status.bat_charge += status.bat_current * dur_sec;
		status.bat_energy += (status.bat_power - status.bat_current * status.ir) * dur_sec;
	}
	
	float bat_current = usamp / conf.r_samp,
	      bat_voltage = (conf.v_ext_power - udiv / conf.div_prop) - status.bat_current * conf.r_extra;
	
	if (flag_scrolling_average) {
		buf_bat_current.push(bat_current); buf_bat_voltage.push(bat_voltage);
		status.bat_current = buf_bat_current.get_average();
		status.bat_voltage = buf_bat_voltage.get_average();
	} else {
		status.bat_current = bat_current;
		status.bat_voltage = bat_voltage;
	}
	
	if (status.flag_ir_measured)
		status.bat_voltage_oc = status.bat_voltage - status.bat_current * status.ir;
	else
		status.bat_voltage_oc = status.bat_voltage;
	
	status.mos_power = (udiv / conf.div_prop - usamp) * status.bat_current;
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
