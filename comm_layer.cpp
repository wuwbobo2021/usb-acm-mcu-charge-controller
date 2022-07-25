// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "comm_layer.h"

#include <cstdlib>
#include <cmath>
#include <string>

using namespace std::chrono;
using namespace std::this_thread;

bool CommLayer::connect(DataCallbackPtr cb_ptr)
{
	string str_port_name; int open_result;
	
	//scan
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
		
		if (! send_cmd(Cmd_ID_Check)) {serial.CloseDevice(); continue;}
		
		uint32_t resp;
		if (rec_resp((char*)&resp, 4) == Resp_OK && resp == Resp_Check) { //Resp_Check is the key
			flag_connected = true; break;
		}
		
		serial.CloseDevice();
	}
	
	if (! flag_connected) return false;
	
	vdac = 0;
	if (! read_ad_vrefint(true)) {
		serial.CloseDevice(); return flag_connected = false;
	}
	
	thread_comm = new thread(&CommLayer::comm_loop, this);
	thread_proc = new thread(&CommLayer::process_loop, this);
	callback_ptr = cb_ptr;
	return true;
}

// note: `recover` can also be true when ADC is not converting if the caller
//       expect it to start regular conversion after reading AD_VRefInt.
bool CommLayer::read_ad_vrefint(bool recover) 
{
	if (recover) {
		if (! send_cmd_rec_resp(Cmd_ID_ADC_Stop)) return false;
		rec_data(bulk_interval_ms + 2000);
	}
	
	// config for VRefInt
	adc_conf.Use_VRefInt_For_ADC2 = true;
	if (! send_cmd_rec_resp(Cmd_ID_ADC_Config, (char*)&adc_conf, sizeof(adc_conf))) return false;
	
	// read VRefInt
	if (! send_cmd_rec_resp(Cmd_ID_ADC_Start)) return false;
	if (! rec_data(bulk_interval_ms + 2000)) return false;
	if (! send_cmd_rec_resp(Cmd_ID_ADC_Stop)) return false;
	process_data();
	vdda = vrefint * ADC_Raw_Value_Max / adc2_value;
	t_read_ad_vrefint = steady_clock::now();
	
	// config normally
	adc_conf.Use_VRefInt_For_ADC2 = false;
	send_cmd(Cmd_ID_ADC_Config, (char*)&adc_conf, sizeof(adc_conf));
	if (! send_cmd_rec_resp(Cmd_ID_ADC_Config, (char*)&adc_conf, sizeof(adc_conf))) return false;
	
	if (recover) {
		if (! send_cmd_rec_resp(Cmd_ID_ADC_Start)) return false;
		rec_data(bulk_interval_ms + 2000);
		dac_output(vdac);
	}
	
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
}

bool CommLayer::shake()
{
	if (! flag_connected) return false;
	
	flag_shake = true;
	while (flag_shake) this_thread::sleep_for(milliseconds(1));
	
	return flag_shake_success;
}

void CommLayer::dac_output(float val)
{
	if (!flag_connected || val < 0 || val > vdda) return;
	vdac = val; dac_new_val = from_voltage(val); flag_dac_output = true;
}

void CommLayer::comm_loop()
{
	// make sure there is no data to be received while sending shake command for the first time.
	rec_data(10 * bulk_interval_ms);
	
	while (true) {
		if (flag_close) {
			send_cmd_rec_resp(Cmd_ID_ADC_Stop);
			this_thread::sleep_for(milliseconds(100)); return;
		}
		
		if (flag_dac_output) {
			send_cmd(Cmd_ID_DAC_Output, (char*)&dac_new_val, 2);
			flag_dac_output = false;
		}
		else if (flag_shake) {
			flag_shake_success = send_cmd_rec_resp(Cmd_ID_Shake);
			flag_shake = false;
		}
		
		if (steady_clock::now() - t_read_ad_vrefint > milliseconds(Interval_Read_VRefInt))
			read_ad_vrefint(true);
		
		if (rec_data(bulk_interval_ms + 2000))
			flag_data_ready = true;
		else {
			if (send_cmd_rec_resp(Cmd_ID_ADC_Start) == true) continue;
			flag_connected = false; serial.CloseDevice(); return;
		}
	}
}

void CommLayer::process_loop()
{
	while (true) {
		this_thread::sleep_for(milliseconds(1));
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

bool CommLayer::send_cmd_rec_resp(char cmd_id, const char* data_extra, uint32_t sz_extra)
{
	for (int cnt_try = 5; cnt_try > 0; cnt_try--) {
		if (! send_cmd(cmd_id, data_extra, sz_extra)) continue;
		if (rec_resp() != Resp_OK) {
			rec_discard_in_ms(500); continue;
		}
		return true;
	}
	return false;
}

bool CommLayer::send_cmd(char cmd_id, const char* data_extra, uint32_t sz_extra)
{
	this_thread::sleep_for(milliseconds(10));
	
	char* data = new char[Cmd_Header_Length + 1 + sz_extra];
	*(uint32_t*)(data) = Cmd_Header;
	data[Cmd_Header_Length + 1 - 1] = cmd_id;
	
	if (sz_extra != 0)
		memcpy(data + Cmd_Header_Length + 1, data_extra, sz_extra);
	
	int result = serial.WriteBytes((void*)data, Cmd_Header_Length + 1 + sz_extra);
	delete[] data;
	return result == 1;
}

int CommLayer::rec_resp(char* data_extra, uint32_t sz_extra, uint32_t timeout_ms)
{
	uint32_t head;
	if (serial.ReadBytes((void*)&head, Cmd_Header_Length, timeout_ms, 10) < Cmd_Header_Length) return -1;
	if (head != Cmd_Header) return -1;
	
	char resp;
	if (serial.ReadChar(&resp, timeout_ms) != 1) return -1;
	if (data_extra != NULL && sz_extra != 0)
		if (serial.ReadBytes(data_extra, sz_extra, timeout_ms, 10) < sz_extra) return -1;
	return (unsigned char)resp;
}

void CommLayer::rec_discard_in_ms(uint32_t ms)
{
	char ch; steady_clock::time_point t_end = steady_clock::now() + milliseconds(ms);
	while (steady_clock::now() < t_end)
		serial.ReadChar(&ch, 1);
}

static bool array_equal(char* target, const char* source, uint32_t sz);
static void bytes_shift_left(char* bytes, uint32_t sz);

bool CommLayer::rec_until(const char* exp_data, uint32_t sz_data, uint32_t timeout_ms)
{
	if (sz_data == 0) return true;
	
	steady_clock::time_point t_start = steady_clock::now();
	
	char* data = new char[sz_data];
	if (serial.ReadBytes((void*)data, sz_data, timeout_ms, 10) < sz_data) {delete data; return false;}
	if (array_equal(data, exp_data, sz_data)) {delete data; return true;}
	
	uint32_t cnt_bytes = 0;
	
	while (steady_clock::now() - t_start <= milliseconds(timeout_ms)) {
		bytes_shift_left(data, sz_data);
		if (serial.ReadChar(& data[sz_data - 1], timeout_ms) != 1) break;
		if (array_equal(data, exp_data, sz_data)) {delete data; return true;}
	}
	delete data; return false;
}

static bool array_equal(char* target, const char* source, uint32_t sz)
{
	for (uint32_t i = 0; i < sz; i++)
		if (target[i] != source[i]) return false;
	return true;
}

static void bytes_shift_left(char* bytes, uint32_t sz)
{
	for (uint32_t i = 0; i < sz - 1; i++)
		bytes[i] = bytes[i + 1];
	
	bytes[sz - 1] = 0x00;
}

bool CommLayer::rec_data(uint32_t timeout_ms)
{
	//look for the header of ADC data block
	static uint32_t data_header(Data_Header);
	if (! rec_until((char*)&data_header, Data_Header_Length, timeout_ms)) return false;
	
	int read_result = serial.ReadBytes(adc_raw_data, ADC_Bulk_Size, timeout_ms, 10);
	return read_result == ADC_Bulk_Size;
}

static float get_stable_average(const uint16_t* raw_data, unsigned int cnt, float diff_max);
static float get_average(const float* data, unsigned int cnt);

void CommLayer::process_data()
{
	uint16_t* praw = (uint16_t*)adc_raw_data;
	for (int i = 0; i < ADC_Buffer_Data_Amount; i++) {
		adc1_raw_data[i] = praw[2*i];
		adc2_raw_data[i] = praw[2*i + 1];
	}
	
	uint16_t* p1 = adc1_raw_data, * p2 = adc2_raw_data;
	for (int i = 0; i < Data_Amount_Per_Av_Second; i++) {
		adc1_values[i] = get_stable_average(p1, Data_Amount_Per_Av_First, Oversampling_Radius);
		adc2_values[i] = get_stable_average(p2, Data_Amount_Per_Av_First, Oversampling_Radius);
		p1 += Data_Amount_Per_Av_First; p2 += Data_Amount_Per_Av_First;
	}
	
	adc1_value = get_average(adc1_values, Data_Amount_Per_Av_Second);
	adc2_value = get_average(adc2_values, Data_Amount_Per_Av_Second);
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
