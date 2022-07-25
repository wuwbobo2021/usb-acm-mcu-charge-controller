// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "ui_layer.h"

#include <ctime>
#include <chrono>
#include <iomanip>

#include <glibmm/stringutils.h>
#include <gtkmm/grid.h>
#include <gtkmm/separator.h>
#include <gtkmm/messagedialog.h>

using namespace SimpleCairoPlot;

UILayer::UILayer() {}

UILayer::UILayer(ChargeControlLayer* ctrl, unsigned int buf_size)
{
	this->init(ctrl, buf_size);
}

UILayer::~UILayer()
{
	this->close();
}

void UILayer::init(ChargeControlLayer* ctrl, unsigned int buf_size)
{
	if (ctrl == NULL)
		throw std::runtime_error("UILayer::init(): invalid parameter.");
	
	this->ctrl = ctrl; this->buf_size = buf_size;
	this->ctrl->set_event_callback_ptr(MemberFuncEventCallbackPtr<UILayer, &UILayer::control_event_callback>(this));
	
	sst.setf(ios::fixed); sst.precision(3);
}

void UILayer::open()
{
	if (this->thread_gtk || this->window) return;
	this->thread_gtk = new thread(&UILayer::app_run, this);
	
	while (! this->window)
		this_thread::sleep_for(chrono::milliseconds(10));
	this_thread::sleep_for(chrono::milliseconds(10));
}

Recorder& UILayer::recorder() const
{
	steady_clock::time_point t = steady_clock::now();
	while (!this->window && ms_since(t) < 5000)
		std::this_thread::sleep_for(chrono::milliseconds(10));
	if (! this->window)
		throw std::runtime_error("UILayer::recorder(): frontend is not opened.");
	return *this->rec;
}

void UILayer::run()
{
	if (this->thread_gtk) {
		this->thread_gtk->join();
		delete this->thread_gtk; this->thread_gtk = NULL;
	} else
		this->app_run();
}

void UILayer::close()
{
	if (! this->window) return;
	
	if (this->rec) this->rec->stop();
	
	this->dispatcher_close->emit(); //eventually deletes itself
	
	if (this->thread_gtk) {
		this->thread_gtk->join();
		delete this->thread_gtk; this->thread_gtk = NULL;
	} else {
		while (this->window)
			this_thread::sleep_for(chrono::milliseconds(1));
	}
}

/*------------------------------ private functions ------------------------------*/

void UILayer::app_run()
{
	Glib::RefPtr<Gtk::Application> app = Gtk::Application::create(UILayer::App_Name);
	this->dispatcher_refresh = new Glib::Dispatcher;
	this->dispatcher_close = new Glib::Dispatcher;
	this->create_window(); this->create_file_dialog();
	this->dispatcher_refresh->connect(sigc::mem_fun(*this, &UILayer::refresh_ui));
	this->dispatcher_close->connect(sigc::mem_fun(*this, &UILayer::close_window));
	app->run(*this->window);
	
	this->window = NULL; //the window is already destructed when the thread exits Application::run()
	delete this->file_dialog;
	delete this->dispatcher_refresh; delete this->dispatcher_close;
} // Unsolved problem on windows platform when running in a new thread: Segmentation fault received here.

void UILayer::create_window()
{
	//initialize the recorder
	vector<VariableAccessPtr> ptrs;
	VariableAccessPtr ptr1(ctrl->battery_voltage_ptr()),
	                  ptr2(ctrl->battery_current_ptr());
	ptr1.color_plot.set_rgba(0.0, 0.5, 0.5);
	ptr2.color_plot.set_rgba(1.0, 0.0, 0.0);
	ptr1.name_csv = "bat_voltage"; ptr1.unit_name = "V"; ptr1.name_friendly = locale_str.name_bat_voltage;
	ptr2.name_csv = "bat_current"; ptr2.unit_name = "A"; ptr2.name_friendly = locale_str.name_bat_current;
	ptrs.push_back(ptr1); ptrs.push_back(ptr2);
	
	this->rec = Gtk::manage(new Recorder(ptrs, this->buf_size));
	this->rec->set_index_range((unsigned int)(30 * 60 * 1000.0 / this->ctrl->data_interval()) - 1);
	this->rec->set_interval(Recorder_Interval);
	this->rec->set_redraw_interval(UI_Refresh_Interval);
	this->rec->set_option_auto_set_zero_bottom(false);
	this->rec->set_axis_y_range_length_min(0, 0.2); this->rec->set_axis_y_range_length_min(1, 0.1);
	this->rec->signal_full().connect(sigc::mem_fun(*this, &UILayer::on_buffers_full));
	this->st_last = this->ctrl->control_status().control_state;
	if (this->st_last != Device_Disconnected) this->rec->start();
	
	// initialize controls in the right panel
	button_on_off = Gtk::manage(new Gtk::Button(locale_str.caption_button_on));
	button_config = Gtk::manage(new Gtk::Button(locale_str.caption_button_config));
	button_calibrate = Gtk::manage(new Gtk::Button(locale_str.caption_button_calibrate));
	button_open = Gtk::manage(new Gtk::Button(locale_str.caption_button_open)),
	button_save = Gtk::manage(new Gtk::Button(locale_str.caption_button_save));
	button_apply = Gtk::manage(new Gtk::Button(locale_str.caption_button_apply));
	
	button_on_off->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_on_off_clicked));
	button_config->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_config_clicked));
	button_calibrate->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_calibrate_clicked));
	button_open->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_open_clicked));
	button_save->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_save_clicked));
	button_apply->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_apply_clicked));
	
	entry_exp_current = Gtk::manage(new Gtk::Entry()); entry_exp_current->set_width_chars(6);
	entry_exp_voltage = Gtk::manage(new Gtk::Entry()); entry_exp_voltage->set_width_chars(6);
	entry_exp_charge = Gtk::manage(new Gtk::Entry()); entry_exp_charge->set_width_chars(6);
	chk_stage_const_v = Gtk::manage(new Gtk::CheckButton(locale_str.name_opt_stage_const_v));
	entry_min_current = Gtk::manage(new Gtk::Entry()); entry_min_current->set_width_chars(6);
	chk_stage_const_v->signal_toggled().connect(sigc::mem_fun(*this, &UILayer::on_chk_stage_const_v_toggled));
	show_param_values();
	
	Gtk::Label* label_exp_current = Gtk::manage(new Gtk::Label(locale_str.name_exp_current, Gtk::ALIGN_START));
	Gtk::Label* label_exp_voltage = Gtk::manage(new Gtk::Label(locale_str.name_exp_voltage, Gtk::ALIGN_START));
	Gtk::Label* label_exp_charge = Gtk::manage(new Gtk::Label(locale_str.name_exp_charge, Gtk::ALIGN_START));
	Gtk::Label* label_min_current = Gtk::manage(new Gtk::Label(locale_str.name_min_current, Gtk::ALIGN_START));
	
	label_status = Gtk::manage(new Gtk::Label);
	label_status->set_width_chars(18); label_status->set_selectable(true);
	
	// build framework
	Gtk::Box* bar_conf_cal  = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL)),
	        * bar_open_save = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));

	bar_conf_cal->set_spacing(3); bar_open_save->set_spacing(3);
	bar_conf_cal->pack_start(*button_config); bar_conf_cal->pack_start(*button_calibrate);
	bar_open_save->pack_start(*button_open); bar_open_save->pack_start(*button_save);
	
	Gtk::Grid* grid_param = new Gtk::Grid();
	grid_param->set_halign(Gtk::ALIGN_CENTER);
	grid_param->set_row_spacing(4); grid_param->set_column_spacing(1);
	grid_param->attach(*label_exp_current, 0, 0, 1, 1); grid_param->attach(*entry_exp_current, 1, 0, 1, 1);
	grid_param->attach(*label_exp_voltage, 0, 1, 1, 1); grid_param->attach(*entry_exp_voltage, 1, 1, 1, 1);
	grid_param->attach(*label_exp_charge,  0, 2, 1, 1); grid_param->attach(*entry_exp_charge,  1, 2, 1, 1);
	grid_param->attach(*chk_stage_const_v, 0, 3, 2, 1);
	grid_param->attach(*label_min_current, 0, 4, 1, 1); grid_param->attach(*entry_min_current, 1, 4, 1, 1);
	grid_param->attach(*button_apply, 0, 5, 2, 1);
	
	Gtk::Box* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL)),
	        * bar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
	
	bar->set_border_width(10); bar->set_spacing(10);
	bar->pack_start(*button_on_off, Gtk::PACK_SHRINK);
	bar->pack_start(*bar_conf_cal, Gtk::PACK_SHRINK);
	bar->pack_start(*bar_open_save, Gtk::PACK_SHRINK);
	bar->pack_start(*Gtk::manage(new Gtk::Separator), Gtk::PACK_SHRINK);
	bar->pack_start(*grid_param, Gtk::PACK_SHRINK);
	bar->pack_start(*Gtk::manage(new Gtk::Separator), Gtk::PACK_SHRINK);
	bar->pack_start(*label_status, Gtk::PACK_SHRINK);
	
	box->set_border_width(2);
	box->pack_start(*this->rec);
	box->pack_start(*bar, Gtk::PACK_SHRINK);
	
	// create window
	this->window = new Gtk::Window();
	this->window->set_default_size(900, 700);
	this->window->add(*box);
	this->refresh_window_title();
	this->refresh_ui();
	this->window->show_all_children();
}

void UILayer::create_file_dialog()
{
	Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
	filter->set_name(locale_str.name_file_filter_csv);
	filter->add_pattern("*.csv"); filter->add_mime_type("text/csv");
	file_dialog = new Gtk::FileChooserDialog(locale_str.title_dialog_open_file, Gtk::FILE_CHOOSER_ACTION_OPEN);
	file_dialog->set_select_multiple(false);
	file_dialog->add_filter(filter);
	file_dialog->add_button(locale_str.caption_button_cancel, Gtk::RESPONSE_CANCEL);
	file_dialog->add_button(locale_str.caption_button_ok, Gtk::RESPONSE_OK);
	file_dialog->set_transient_for(*this->window); file_dialog->set_modal(true);
}

void UILayer::control_event_callback(ChargeControlEvent ev)
{
	if (! this->window) return;
	if (! this->window->get_visible()) return;
	
	this->flag_event = true; this->last_event = ev;
	this->dispatcher_refresh->emit();
}

void UILayer::refresh_ui()
{
	ChargeControlState st = this->ctrl->control_status().control_state;
	
	if (this->flag_event) {
		this->flag_event = false;
		if (this->last_event == Event_New_Data) {
			if (ms_since(t_status_refresh) < UI_Refresh_Interval) return;
		} else {
			this->flag_show_event = true;
			switch (this->last_event) {
				case Event_Device_Connect: case Event_Battery_Connect:
					if (st != Battery_Disconnected && !this->rec->is_recording()) this->rec->start();
					break;
				
				case Event_Device_Disconnect: case Event_Battery_Disconnect:
					if (this->rec->is_recording()) this->rec->stop();
					break;
				
				default: break;
			}
		}
	}
		
	switch (st) {
		case Device_Disconnected: case Battery_Disconnected:
			if (this->rec->is_recording()) this->rec->stop();
			this->button_on_off->set_label(locale_str.caption_button_on);
			this->button_on_off->set_sensitive(false);
			this->button_calibrate->set_sensitive(st == Battery_Disconnected);
			this->button_open->set_sensitive(true); this->button_save->set_sensitive(true);
			break;
		
		case Battery_Connected: case Charge_Completed: case Charge_Stopped:
			if (! this->rec->is_recording()) this->rec->start();
			this->button_on_off->set_label(locale_str.caption_button_on);
			this->button_on_off->set_sensitive(true);
			this->button_calibrate->set_sensitive(true);
			this->button_open->set_sensitive(false); this->button_save->set_sensitive(true);
			break;
		
		case Battery_Charging_CC: case Battery_Charging_CV:
			if (! this->rec->is_recording()) this->rec->start();
			this->button_on_off->set_label(locale_str.caption_button_off);
			this->button_calibrate->set_sensitive(true);
			this->button_open->set_sensitive(false); this->button_save->set_sensitive(false);
			this->window->set_title(locale_str.window_title(st));
			break;
	}
	
	refresh_window_title();
	
	ustring str_st;
	locale_str.get_control_status_str(ctrl->control_status(), str_st);
	label_status->set_label(str_st);
	
	t_status_refresh = steady_clock::now();
}

void UILayer::on_buffers_full()
{
	this->window->set_title(locale_str.window_title(locale_str.event_buffer_full));
}

void UILayer::on_button_on_off_clicked()
{
	flag_show_event = false;
	
	ChargeControlState st = this->ctrl->control_status().control_state;
	if (st == Device_Disconnected || st == Battery_Disconnected) return;
	
	if (st != Battery_Charging_CC && st != Battery_Charging_CV)
		this->ctrl->start_charging();
	else
		this->ctrl->stop_charging();
	
	this_thread::sleep_for(milliseconds(200));
	this->refresh_ui();

}

inline std::string float_to_str(float val, std::stringstream& sst)
{
	sst.clear(); sst.str("");
	sst << val;
	return sst.str();
}

inline float str_to_float(const string& str, float val_default, std::stringstream& sst)
{
	sst.clear(); sst.str(str);
	float val; sst >> val;
	if (! sst) {
		sst.clear(); sst.sync();
		return val_default;
	}
	return val;
}

void UILayer::on_button_calibrate_clicked()
{
	flag_show_event = false;
	
	float v_bat_actual;
	if (! user_input_value(locale_str.message_input_v_bat, &v_bat_actual,
	                       this->ctrl->control_status().bat_voltage)) return;
	
	this->ctrl->calibrate(v_bat_actual);
	
	sst.precision(3);
	Gtk::MessageDialog msg_dlg(*this->window,
	                           locale_str.message_report_vrefint + ' ' +
							   float_to_str(this->ctrl->hard_config().v_refint, sst) + " V.",
							   false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
	msg_dlg.run(); msg_dlg.close();
}

void UILayer::on_button_apply_clicked()
{
	flag_show_event = false;
	
	ChargeParameters param;
	param.exp_current = str_to_float(entry_exp_current->get_text(), param.exp_current * 1000.0, sst) / 1000.0;
	param.exp_voltage = str_to_float(entry_exp_voltage->get_text(), param.exp_voltage, sst);
	param.exp_charge  = str_to_float(entry_exp_charge->get_text(), param.exp_charge / 3.6, sst) * 3.6;
	param.opt_stage_const_v = chk_stage_const_v->get_active();
	param.min_current = str_to_float(entry_min_current->get_text(), param.min_current * 1000.0, sst) / 1000.0;
	this->ctrl->set_charge_param(param);
	
	show_param_values(); //in case of some of the input values are invalid
}

void UILayer::on_button_open_clicked()
{
	flag_show_event = false;
	if (this->rec->is_recording()) this->rec->stop();
	
	this->file_dialog->set_title(locale_str.title_dialog_open_file);
	this->file_dialog->set_action(Gtk::FILE_CHOOSER_ACTION_OPEN);
	Gtk::ResponseType resp = (Gtk::ResponseType) this->file_dialog->run();
	this->file_dialog->close();
	if (resp != Gtk::RESPONSE_OK) return;
	
	string path = this->file_dialog->get_file()->get_path();
	this->rec->open_csv(path);
}

void UILayer::on_button_save_clicked()
{
	flag_show_event = false;
	
	using Glib::str_has_suffix;
	#ifdef _WIN32
		const string Slash = "\\";
	#else
		const string Slash = "/";
	#endif
	
	bool prev_recording = this->rec->is_recording();
	if (prev_recording) this->rec->stop();
	
	this->file_dialog->set_title(locale_str.title_dialog_save_file);
	this->file_dialog->set_action(Gtk::FILE_CHOOSER_ACTION_SAVE);
	
	const time_t t_c = system_clock::to_time_t(system_clock::now());
	sst.clear(); sst.str("");
	sst << put_time(localtime(&t_c), "%Y_%m_%d_%H_%M_%S");
	this->file_dialog->set_current_name(sst.str());
	
	Gtk::ResponseType resp = (Gtk::ResponseType) this->file_dialog->run();
	this->file_dialog->close();
	if (resp != Gtk::RESPONSE_OK) return;
	
	string path = this->file_dialog->get_current_folder();
	if (! str_has_suffix(path, Slash)) path += Slash;
	path += this->file_dialog->get_current_name();
	if (! str_has_suffix(path, ".csv")) path += ".csv"; 
	
	bool suc = this->rec->save_csv(path);
	if (! suc) {
		Gtk::MessageDialog msg_dlg(*this->window, locale_str.message_failed_to_save_file, 
	                               false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	    msg_dlg.run(); msg_dlg.close();
	}
	
	if (prev_recording) this->rec->start();
}

void UILayer::on_chk_stage_const_v_toggled()
{
	flag_show_event = false;
	entry_min_current->set_sensitive(chk_stage_const_v->get_active());
}

void UILayer::show_param_values()
{
	ChargeParameters param = this->ctrl->charge_param();
	sst.precision(3);
	entry_exp_voltage->set_text(float_to_str(param.exp_voltage, sst));
	sst.precision(0);
	entry_exp_current->set_text(float_to_str(param.exp_current * 1000.0, sst));
	entry_exp_charge->set_text(float_to_str(param.exp_charge / 3.6, sst));
	chk_stage_const_v->set_active(param.opt_stage_const_v);
	entry_min_current->set_sensitive(param.opt_stage_const_v);
	entry_min_current->set_text(float_to_str(param.min_current * 1000.0, sst));
}

static void dlg_resp(Gtk::Dialog* dlg, int response_id)
{
	dlg->response(response_id);
}

bool UILayer::user_input_value(const string& str_prompt, float* p_val, float val_default)
{
	// unlike the file dialog, reuse of the input dialog always cause problem
	Gtk::Dialog* input_dialog = new Gtk::Dialog(locale_str.title_dialog_input, *this->window, true);
	Gtk::Label* label_input_dialog = Gtk::manage(new Gtk::Label(str_prompt));
	Gtk::Entry* entry_input = Gtk::manage(new Gtk::Entry);
	entry_input->signal_activate().connect(sigc::bind(sigc::ptr_fun(&dlg_resp), input_dialog, Gtk::RESPONSE_OK));
	input_dialog->get_content_area()->pack_start(*label_input_dialog);
	input_dialog->get_content_area()->pack_start(*entry_input);
	input_dialog->add_button(locale_str.caption_button_ok, Gtk::RESPONSE_OK);
	input_dialog->add_button(locale_str.caption_button_cancel, Gtk::RESPONSE_CANCEL);
	input_dialog->set_default_size(320, 80);
	input_dialog->show_all_children();
	
	sst.precision(3);
	entry_input->set_text(float_to_str(val_default, sst));
	Gtk::ResponseType resp = (Gtk::ResponseType) input_dialog->run();
	input_dialog->close();
	
	bool suc = false;
	
	if (resp == Gtk::RESPONSE_OK) {
		sst.clear(); sst.sync();
		sst.str(entry_input->get_text()); sst >> *p_val;
		if (!sst.fail()) suc = true;
		else sst.clear();
	}
	delete input_dialog;
	return suc;
}

void UILayer::on_button_config_clicked()
{
	flag_show_event = false;
	
	ChargeControlConfig conf = this->ctrl->hard_config();
	
	Gtk::Dialog* config_dialog = new Gtk::Dialog(locale_str.caption_button_config, *this->window, true);
	
	Gtk::Label* label_v_refint = Gtk::manage(new Gtk::Label(locale_str.name_v_refint, Gtk::ALIGN_START));
	Gtk::Label* label_v_ext_power = Gtk::manage(new Gtk::Label(locale_str.name_v_ext_power, Gtk::ALIGN_START));
	Gtk::Label* label_div_prop = Gtk::manage(new Gtk::Label(locale_str.name_div_prop, Gtk::ALIGN_START));
	Gtk::Label* label_r_samp = Gtk::manage(new Gtk::Label(locale_str.name_r_samp, Gtk::ALIGN_START));
	Gtk::Label* label_r_extra = Gtk::manage(new Gtk::Label(locale_str.name_r_extra, Gtk::ALIGN_START));
	Gtk::Label* label_i_max = Gtk::manage(new Gtk::Label(locale_str.name_i_max, Gtk::ALIGN_START));
	Gtk::Label* label_p_mos_max = Gtk::manage(new Gtk::Label(locale_str.name_p_mos_max, Gtk::ALIGN_START));
	
	Gtk::Label* label_v_bat_detect_th = Gtk::manage(new Gtk::Label(locale_str.name_v_bat_detect_th, Gtk::ALIGN_START));
	Gtk::Label* label_v_dac_adj_step = Gtk::manage(new Gtk::Label(locale_str.name_v_dac_adj_step, Gtk::ALIGN_START));
	Gtk::Label* label_v_bat_dec_th = Gtk::manage(new Gtk::Label(locale_str.name_v_bat_dec_th, Gtk::ALIGN_START));
	
	Gtk::Entry* entry_v_refint = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_v_ext_power = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_div_prop = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_r_samp = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_r_extra = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_i_max = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_p_mos_max = Gtk::manage(new Gtk::Entry());
	
	Gtk::Entry* entry_v_bat_detect_th = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_v_dac_adj_step = Gtk::manage(new Gtk::Entry());
	Gtk::Entry* entry_v_bat_dec_th = Gtk::manage(new Gtk::Entry());
	
	sst.precision(3);
	entry_v_refint->set_text(float_to_str(conf.v_refint, sst));
	entry_v_ext_power->set_text(float_to_str(conf.v_ext_power, sst));
	entry_div_prop->set_text(float_to_str(conf.div_prop, sst));
	entry_r_samp->set_text(float_to_str(conf.r_samp, sst));
	entry_r_extra->set_text(float_to_str(conf.r_extra, sst));
	entry_i_max->set_text(float_to_str(conf.i_max, sst));
	sst.precision(1);
	entry_p_mos_max->set_text(float_to_str(conf.p_mos_max, sst));
	
	sst.precision(3);
	entry_v_bat_detect_th->set_text(float_to_str(conf.v_bat_detect_th, sst));
	sst.precision(0);
	entry_v_dac_adj_step->set_text(float_to_str(conf.v_dac_adj_step * 1000.0, sst));
	entry_v_bat_dec_th->set_text(float_to_str(conf.v_bat_dec_th * 1000.0, sst));

	Gtk::Grid* grid_config = Gtk::manage(new Gtk::Grid);
	grid_config->set_border_width(10);
	grid_config->set_row_spacing(4); grid_config->set_column_spacing(10);
	grid_config->attach(*label_v_refint,        0, 0, 1, 1); grid_config->attach(*entry_v_refint,        1, 0, 1, 1);
	grid_config->attach(*label_v_ext_power,     0, 1, 1, 1); grid_config->attach(*entry_v_ext_power,     1, 1, 1, 1);
	grid_config->attach(*label_div_prop,        0, 2, 1, 1); grid_config->attach(*entry_div_prop,        1, 2, 1, 1);
	grid_config->attach(*label_r_samp,          0, 3, 1, 1); grid_config->attach(*entry_r_samp,          1, 3, 1, 1);
	grid_config->attach(*label_r_extra,         0, 4, 1, 1); grid_config->attach(*entry_r_extra,         1, 4, 1, 1);
	grid_config->attach(*label_i_max,           0, 5, 1, 1); grid_config->attach(*entry_i_max,           1, 5, 1, 1);
	grid_config->attach(*label_p_mos_max,       0, 6, 1, 1); grid_config->attach(*entry_p_mos_max,       1, 6, 1, 1);
	grid_config->attach(*Gtk::manage(new Gtk::Separator), 0, 7, 2, 1);
	grid_config->attach(*label_v_bat_detect_th, 0, 8, 1, 1); grid_config->attach(*entry_v_bat_detect_th, 1, 8, 1, 1);
	grid_config->attach(*label_v_dac_adj_step,  0, 9, 1, 1); grid_config->attach(*entry_v_dac_adj_step,  1, 9, 1, 1);
	grid_config->attach(*label_v_bat_dec_th,    0,10, 1, 1); grid_config->attach(*entry_v_bat_dec_th,    1,10, 1, 1);
	
	config_dialog->get_content_area()->pack_start(*grid_config);
	config_dialog->add_button(locale_str.caption_button_ok, Gtk::RESPONSE_OK);
	config_dialog->add_button(locale_str.caption_button_cancel, Gtk::RESPONSE_CANCEL);
	config_dialog->show_all_children();
	
	while (true) {
		Gtk::ResponseType resp = (Gtk::ResponseType) config_dialog->run();
		if (resp != Gtk::RESPONSE_OK) break;
		
		conf.v_refint = str_to_float(entry_v_refint->get_text(), conf.v_refint, sst);
		conf.v_ext_power = str_to_float(entry_v_ext_power->get_text(), conf.v_ext_power, sst);
		conf.div_prop = str_to_float(entry_div_prop->get_text(), conf.div_prop, sst);
		conf.r_samp = str_to_float(entry_r_samp->get_text(), conf.r_samp, sst);
		conf.r_extra = str_to_float(entry_r_extra->get_text(), conf.r_extra, sst);
		conf.i_max = str_to_float(entry_i_max->get_text(), conf.i_max, sst);
		conf.p_mos_max = str_to_float(entry_p_mos_max->get_text(), conf.p_mos_max, sst);
		
		conf.v_bat_detect_th = str_to_float(entry_v_bat_detect_th->get_text(), conf.v_bat_detect_th, sst);
		conf.v_dac_adj_step = str_to_float(entry_v_dac_adj_step->get_text(), conf.v_dac_adj_step * 1000.0, sst) / 1000.0;
		conf.v_bat_dec_th = str_to_float(entry_v_bat_dec_th->get_text(), conf.v_bat_dec_th * 1000.0, sst) / 1000.0;
		
		if (this->ctrl->set_hard_config(conf))
			break;
		else {
			Gtk::MessageDialog msg_dlg(*config_dialog, locale_str.message_invalid_input, 
	        	                       false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
	    	msg_dlg.run(); msg_dlg.close();
		}
	}
	
	delete config_dialog;
}

void UILayer::close_window() //used by dispatcher_close
{
	this->window->close();	
}

