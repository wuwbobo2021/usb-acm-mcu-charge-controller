// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "comm_layer.h"

#include <cstdlib>
#include <cmath>
#include <string>

#ifdef dbg_print
	#undef dbg_print
#endif
#ifdef DEBUG
	#include <iostream>
	#include <iomanip>

	#define dbg_print(str) {cout << "(Comm) " << (str) << endl;}
	
	static inline void dbg_print_bytes(const string& head, const void* ptr, unsigned int cnt) {
		cout << "(Comm) "  << head << ": ";
		for (unsigned int i = 0; i < cnt; i++)
			cout << setw(2) << setfill('0') << hex << int(((uint8_t*)ptr)[i]) << ' ';
		cout << endl;
	}
#else
	#define dbg_print(str)
	#define dbg_print_bytes(head, ptr, cnt)
#endif

using namespace std::chrono;
using namespace std::this_thread;

CommLayer::CommLayer(): buf_vdda(64) {}

bool CommLayer::connect(DataCallbackPtr cb_ptr)
{
	string str_port_name; int open_result;
	
	// scan in all serial ports
	for (int i = 1; i <= 256; i++) {
		#ifdef _WIN32
			str_port_name = "\\\\.\\COM" + to_string(i);
		#else
			str_port_name = "/dev/ttyACM" + to_string(i - 1);
		#endif
		int open_result = serial.OpenDevice(str_port_name.c_str(), 9600);
		if (open_result != 1) { //error
			if (open_result != -1 && open_result != -2)
				serial.CloseDevice();
			continue;
		}
		
		if (! apply_cmd(Cmd_ID_Check, &hard_param.resp)
		||  ! hard_param.dac_support //TODO: support MCUs without DAC
		||  ! adc_config()) {
			serial.CloseDevice(); continue;
		}
		
		flag_connected = true; break;
	}
	
	if (! flag_connected) return false;
	
	thread_comm = new thread(&CommLayer::comm_loop, this);
	thread_proc = new thread(&CommLayer::process_loop, this);
	callback_ptr = cb_ptr;
	return true;
}

void CommLayer::disconnect()
{
	if (! flag_connected) return;
	
	flag_close = true;
	thread_comm->join(); thread_proc->join();
	delete thread_comm; delete thread_proc;
	flag_close = false;
	serial.CloseDevice();
	flag_connected = false;
	
	delete[] adc_raw_data;
	delete[] adc1_raw_data; delete[] adc2_raw_data;
	delete[] adc1_values; delete[] adc2_values;
}

bool CommLayer::shake()
{
	if (! flag_connected) return false;
	
	flag_shake = true;
	unsigned int cnt_ms = 0;
	while (flag_shake && cnt_ms < 2*Timeout_Data_Max) {
		if (!flag_connected || flag_close) return false;
		this_thread::sleep_for(milliseconds(30)); cnt_ms++;
	}
	
	return flag_shake_success;
}

bool CommLayer::dac_output(float val)
{
	if (!flag_connected || val < 0 || val > vdda) return false;
	vdac = val; dac_new_val = from_voltage(val);
	return flag_dac_output = true;
}

/*------------------------------ private functions ------------------------------*/

bool CommLayer::adc_config()
{
	adc_conf.cmd = comm_cmd(Cmd_ID_ADC_Start);
	adc_conf.discontinous_mode = false;
	adc_conf.adc_clock_cycles_opt =
		choose_adc_clock_cycles_opt(&hard_param, Raw_Data_Interval);
	bulk_interval_ms = hard_param.adc_bulk_data_amount
					 * adc_raw_data_interval_ms(&hard_param, adc_conf.adc_clock_cycles_opt);
	if (! apply_cmd(adc_conf.cmd)) return false;
	
	if (! flag_override_vrefint)
		vrefint = (float)hard_param.adc_vrefint / 1000.0;
	buf_vdda.clear(true);
	
	data_amount_per_av_first = Data_Amount_Per_Av_First;
	if (data_amount_per_av_first > hard_param.adc_bulk_data_amount)
		data_amount_per_av_first = hard_param.adc_bulk_data_amount;
	data_amount_per_av_second = hard_param.adc_bulk_data_amount
	                          / data_amount_per_av_first;
	
	adc_raw_data = new uint8_t[adc_bulk_data_size(&hard_param)];
	adc1_raw_data = new uint16_t[hard_param.adc_bulk_data_amount];
	adc2_raw_data = new uint16_t[hard_param.adc_bulk_data_amount];
	adc1_values = new float[data_amount_per_av_second];
	adc2_values = new float[data_amount_per_av_second];
	return true;
}

void CommLayer::comm_loop()
{
	// make sure there is no data to be received while sending shake command for the first time.
	rec_data(10 * bulk_interval_ms);
	
	while (true) {
		if (flag_close) {
			apply_cmd(Cmd_ID_ADC_Stop); return;
		}
		
		if (flag_dac_output) {
			pwm_dac_conf.dac_val = dac_new_val;
			if (dac_new_val > 0)
				apply_cmd(pwm_dac_conf.cmd);
			else
				apply_cmd(Cmd_ID_Disable_Output);
			flag_dac_output = false;
		}
		else if (flag_shake) {
			flag_shake_success = apply_cmd(Cmd_ID_Shake);
			flag_shake = false;
		}
		
		if (rec_data(bulk_interval_ms + Timeout_Data_Max)) {
			flag_data_ready = true; //dbg_print("data received");
		} else {
			dbg_print("failed to receive data");
			if (apply_cmd(adc_conf.cmd)) continue;
			thread th_disconnect(&CommLayer::disconnect, this);
			th_disconnect.detach(); return;
		}
	}
}

void CommLayer::process_loop()
{
	while (true) {
		this_thread::sleep_for(milliseconds(30));
		
		if (flag_close || !flag_connected) return;
		if (! flag_data_ready) continue;
		
		flag_data_ready = false;
		process_data();
		
		if (adc1_value == 0 && adc2_value == 0) {
			cnt_zero++;
			if (cnt_zero < 3) continue;
		} else
			cnt_zero = 0;
		
		callback_ptr.call(get_voltage(adc1_value), get_voltage(adc2_value));
	}
}

bool CommLayer::apply_cmd(const CommCmd& cmd, CommResp* rec_data)
{
	uint8_t l_resp = resp_length(cmd.cmd_id);
	uint8_t* tmp_data = new uint8_t[l_resp];
	
	bool suc = false;
	for (int cnt_try = 5; cnt_try > 0; cnt_try--) {
		if (serial.WriteBytes((void*)&cmd, cmd_length(cmd.cmd_id)) != 1) {
			this_thread::sleep_for(milliseconds(100)); continue;
		}
		
		// dbg_print_bytes("S", &cmd, cmd_length(cmd.cmd_id));
		if (cmd.cmd_id == Cmd_ID_PWM_DAC && ((Cmd_PWM_DAC*)&cmd)->no_resp)
			return true;
		
		if (serial.ReadBytes((void*)tmp_data,  l_resp, Timeout_Comm_Max, 1000) < l_resp
		||  !is_valid_resp((uint8_t*)tmp_data, l_resp)) {
			rec_discard_in_ms(Timeout_Comm_Max); continue;
		}
		
		// dbg_print_bytes("R", tmp_data, l_resp);
		suc = (((CommResp*)tmp_data)->resp_val == Resp_OK); break;
	}
	
	if (suc && rec_data)
		memcpy(rec_data, tmp_data, l_resp);
	delete[] tmp_data;
	
	return suc;
}

bool CommLayer::rec_data(uint32_t timeout_ms)
{
	//look for the header of ADC data block
	static uint32_t data_header(Data_Header);
	if (! rec_until((uint8_t*)&data_header, Data_Header_Length, timeout_ms)) return false;
	
	int l_rec = serial.ReadBytes((void*)&ad_refint, sizeof(ad_refint), timeout_ms, 1000);
	if (l_rec < sizeof(ad_refint)) return false;
	
	l_rec = serial.ReadBytes((void*)adc_raw_data, adc_bulk_data_size(&hard_param),
	                         timeout_ms, 1000);
	return l_rec == adc_bulk_data_size(&hard_param);
}

static float get_stable_average(const uint16_t* raw_data, unsigned int cnt, float diff_max);
static float get_average(const float* data, unsigned int cnt);

void CommLayer::process_data()
{
	if (ad_refint) {
		buf_vdda.push(vrefint * (float)ADC_Raw_Value_Max / ad_refint);
		vdda = buf_vdda.get_average();
	}
	
	uint16_t* praw = (uint16_t*)adc_raw_data;
	for (int i = 0; i < hard_param.adc_bulk_data_amount; i++) {
		adc1_raw_data[i] = praw[2*i];
		adc2_raw_data[i] = praw[2*i + 1];
	}
	
	uint16_t* p1 = adc1_raw_data, * p2 = adc2_raw_data;
	for (int i = 0; i < data_amount_per_av_second; i++) {
		adc1_values[i] = get_stable_average(p1, data_amount_per_av_first, Oversampling_Radius);
		adc2_values[i] = get_stable_average(p2, data_amount_per_av_first, Oversampling_Radius);
		p1 += data_amount_per_av_first; p2 += data_amount_per_av_first;
	}
	
	adc1_value = get_average(adc1_values, data_amount_per_av_second);
	adc2_value = get_average(adc2_values, data_amount_per_av_second);
}

void CommLayer::rec_discard_in_ms(uint32_t ms)
{
	steady_clock::time_point t_end = steady_clock::now() + milliseconds(ms);
	
	char ch;
	while (steady_clock::now() < t_end)
		serial.ReadChar(&ch, 1);
	serial.FlushReceiver();
}

static bool array_equal(uint8_t* target, const uint8_t* source, uint32_t sz);
static void bytes_shift_left(uint8_t* bytes, uint32_t sz);

bool CommLayer::rec_until(const uint8_t* exp_data, uint32_t sz_data, uint32_t timeout_ms)
{
	if (sz_data == 0) return true;
	
	steady_clock::time_point t_start = steady_clock::now();
	
	uint8_t* tmp_data = new uint8_t[sz_data];
	if (serial.ReadBytes((void*)tmp_data, sz_data, timeout_ms, 1000) < sz_data) {
		delete tmp_data; return false;
	}
	
	while (steady_clock::now() - t_start <= milliseconds(timeout_ms)) {
		if (array_equal(tmp_data, exp_data, sz_data)) {
			delete tmp_data; return true;
		}
		bytes_shift_left(tmp_data, sz_data);
		if (serial.ReadChar((char*)&tmp_data[sz_data - 1], timeout_ms) != 1) break;
	}
	
	delete tmp_data; return false;
}

static bool array_equal(uint8_t* target, const uint8_t* source, uint32_t sz)
{
	for (uint32_t i = 0; i < sz; i++)
		if (target[i] != source[i]) return false;
	return true;
}

static void bytes_shift_left(uint8_t* bytes, uint32_t sz)
{
	for (uint32_t i = 0; i < sz - 1; i++)
		bytes[i] = bytes[i + 1];
	bytes[sz - 1] = 0x00;
}

static float get_stable_average(const uint16_t* raw_data, unsigned int cnt, float diff_max)
{
	if (raw_data == NULL || cnt == 0) return 0;
	
	// get the reference average. (without influence of extreme values)
	
	uint16_t* data = new uint16_t[cnt];
	uint32_t sum = 0;
	for (uint16_t i = 0; i < cnt; i++) {
		data[i] = raw_data[i];
		sum += raw_data[i];
	}
	
	uint16_t border = cnt / 8; //2*border items will be removed 
	uint16_t cnt_rem = cnt;
	uint16_t curr_min = 0, curr_max = 0;
	uint16_t pmin, pmax;
	for (uint16_t i = 0; i <= border; i++) {
		curr_min = UINT16_MAX; curr_max = 0;
		pmin = pmax = UINT16_MAX;
		
		for (uint16_t j = 0; j < cnt; j++) {
			if (data[j] == UINT16_MAX) continue;
			if (data[j] < curr_min) {
				pmin = j;
				curr_min = data[j];
			}
			if (data[j] > curr_max) {
				pmax = j;
				curr_max = data[j];
			}
		}
		
		if (i == border) break; //at the time curr_min and curr_max are of the remaining data
		if (pmin == UINT16_MAX || pmax == UINT16_MAX) break; //pmax is invalid in case of all values are 0
		sum -= raw_data[pmin]; sum -= raw_data[pmax];
		cnt_rem -= 2;
		
		data[pmin] = data[pmax] = UINT16_MAX;
	}
	delete[] data;
	
	if ((curr_max - curr_min) / 2 > 8 * diff_max) return 0; //invalid data
	float av1 = (float)sum / cnt_rem;
	
	// set the over sampling boundary depending on the reference average calculated above.
	float av2 = 0;
	sum = 0; cnt_rem = 0;
	for (uint16_t i = 0; i < cnt; i++) {
		if (fabs(raw_data[i] - av1) <= diff_max) {
			sum += raw_data[i];
			cnt_rem++;
		}
	}
	if (cnt_rem == 0) return 0;
	av2 = (float)sum / cnt_rem;
	return av2;
}

static float get_average(const float* data, unsigned int cnt)
{
	if (data == NULL || cnt == 0) return 0;
	
	float sum = 0; unsigned int cnt_empty = 0;
	for (unsigned int i = 0; i < cnt; i++) {
		if (data[i] != 0)
			sum += data[i];
		else
			cnt_empty++;
	}
	
	cnt -= cnt_empty; if (cnt == 0) return 0;
	return sum / cnt;
}
