// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "ui_layer.h"

#include <chrono>
#include <iostream>
#include <sstream>

#include <glibmm/stringutils.h>
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
	//initialize its recorder
	vector<VariableAccessPtr> ptrs;
	VariableAccessPtr ptr1(ctrl->battery_voltage_ptr()),
	                  ptr2(ctrl->battery_current_ptr());
	ptr1.color_plot.set_rgba(0.0, 0.5, 0.5);
	ptr2.color_plot.set_rgba(1.0, 0.0, 0.0);
	ptr1.name_csv = "V"; ptr1.name_friendly = "Battery Voltage (V)";
	ptr2.name_csv = "I"; ptr2.name_friendly = "Charge Current (A)";
	ptrs.push_back(ptr1); ptrs.push_back(ptr2);
	
	this->rec = Gtk::manage(new Recorder(ptrs, this->buf_size));
	this->rec->signal_full().connect(sigc::mem_fun(*this, &UILayer::on_buffers_full));
	this->rec->set_interval(UI_Refresh_Interval_Min);
	this->rec->set_index_range(5 * 60);
	if (this->ctrl->control_status().control_state != Device_Disconnected)
		this->rec->start();
	
	// initialize infobar
	infobar = Gtk::manage(new Gtk::InfoBar);
	label_infobar = Gtk::manage(new Gtk::Label);
	((Gtk::Container*)this->infobar->get_content_area())->add(*label_infobar); //strange Gtk::InfoBar!
	infobar->set_show_close_button(true);
	infobar->signal_response().connect(sigc::mem_fun(*this, &UILayer::on_infobar_response));
	
	// initialize controls at right
	button_on_off = Gtk::manage(new Gtk::Button("ON"));
	button_on_off->signal_clicked().connect(
		sigc::mem_fun(*this, &UILayer::on_button_on_off_clicked));
	button_on_off->set_sensitive(false);
	
	button_adjust = Gtk::manage(new Gtk::Button("   Adjust   "));
	button_calibrate = Gtk::manage(new Gtk::Button("Calibrate"));
	button_adjust->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_adjust_clicked));
	button_calibrate->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_calibrate_clicked));
	
	button_open = Gtk::manage(new Gtk::Button("Open")),
	button_save = Gtk::manage(new Gtk::Button("Save"));
	button_open->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_open_clicked));
	button_save->signal_clicked().connect(sigc::mem_fun(*this, &UILayer::on_button_save_clicked));
	
	label_status = Gtk::manage(new Gtk::Label);
	label_status->set_width_chars(18); label_status->set_selectable(true);
	
	// build framework
	
	Gtk::Box* bar_adj_cal   = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL)),
	        * bar_open_save = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));

	bar_adj_cal->set_spacing(2);
	bar_adj_cal->pack_start(*button_adjust);
	bar_adj_cal->pack_start(*button_calibrate);
	
	bar_open_save->set_spacing(2);
	bar_open_save->pack_start(*button_open);
	bar_open_save->pack_start(*button_save);
	
	Gtk::Box* box_with_infobar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL)),
	        * box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL)),
	        * bar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
	
	bar->set_border_width(5); bar->set_spacing(5);
	bar->pack_start(*button_on_off, Gtk::PACK_SHRINK);
	bar->pack_start(*bar_adj_cal, Gtk::PACK_SHRINK);
	bar->pack_start(*bar_open_save, Gtk::PACK_SHRINK);
	bar->pack_start(*label_status, Gtk::PACK_SHRINK);
	
	box->set_border_width(5);
	box->pack_start(*this->rec);
	box->pack_start(*bar, Gtk::PACK_SHRINK);
	
	box_with_infobar->set_border_width(0);
	box_with_infobar->pack_start(*infobar, Gtk::PACK_SHRINK);
	box_with_infobar->pack_start(*box);
	
	// create window
	this->window = new Gtk::Window();
	this->window->set_title(this->title);
	this->window->set_default_size(800, 480);
	this->window->add(*box_with_infobar);
	
	// show all children, except infobar
	this->refresh_ui();
	this->window->show_all_children();
	this->infobar->hide();
}

void UILayer::create_file_dialog()
{
	Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
	filter->set_name("CSV files (.csv)");
	filter->add_pattern("*.csv"); filter->add_mime_type("text/csv");
	file_dialog = new Gtk::FileChooserDialog("Open .csv file", Gtk::FILE_CHOOSER_ACTION_OPEN);
	file_dialog->set_select_multiple(false);
	file_dialog->add_filter(filter);
	file_dialog->add_button("_Cancel", Gtk::RESPONSE_CANCEL);
	file_dialog->add_button("_OK", Gtk::RESPONSE_OK);
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
	if (this->flag_event) {
		this->flag_event = false;
		if (this->last_event == Event_New_Data) {
			if (ms_since(t_status_refresh) < UI_Refresh_Interval_Min) return;
			this->label_status->set_label("\r\n" + (string)(ctrl->control_status()));
		} else {
			switch (this->last_event) {
				case Event_Device_Connect:
					if (! this->rec->is_recording()) this->rec->start();
					break;
				
				case Event_Device_Disconnect:
					if (this->rec->is_recording()) this->rec->stop();
					break;
				
				default: break;
			}
			ostringstream oss; oss << this->last_event;
			this->label_infobar->set_text("  " + oss.str() + ".");
			this->infobar->show();
		}
	}
	
	ChargeControlState st = this->ctrl->control_status().control_state;
	switch (st) {
		case Device_Disconnected: case Battery_Disconnected:
			if (this->rec->is_recording()) this->rec->stop();
			this->button_on_off->set_label("ON");
			this->button_on_off->set_sensitive(false);
			this->button_adjust->set_sensitive(true);
			this->button_calibrate->set_sensitive(st == Battery_Disconnected);
			this->button_open->set_sensitive(true); this->button_save->set_sensitive(true);
			break;
		
		case Battery_Connected: case Charge_Completed: case Charge_Stopped:
			if (! this->rec->is_recording()) this->rec->start();
			this->button_on_off->set_label("ON");
			this->button_on_off->set_sensitive(true);
			this->button_adjust->set_sensitive(true);
			this->button_calibrate->set_sensitive(true);
			this->button_open->set_sensitive(true); this->button_save->set_sensitive(true);
			break;
		
		case Battery_Charging_CC: case Battery_Charging_CV:
			if (! this->rec->is_recording()) this->rec->start();
			this->button_on_off->set_label("OFF");
			this->button_adjust->set_sensitive(st != Battery_Charging_CV);
			this->button_calibrate->set_sensitive(true);
			this->button_open->set_sensitive(false); this->button_save->set_sensitive(false);
			break;
	}
	
	t_status_refresh = steady_clock::now();
}

void UILayer::on_buffers_full()
{
	this->window->set_title(this->title + " - Record buffer is full...");
}

void UILayer::on_button_on_off_clicked()
{
	this->infobar->hide();
	
	ChargeControlState st = this->ctrl->control_status().control_state;
	if (st == Device_Disconnected || st == Battery_Disconnected) return;
	
	if (st != Battery_Charging_CC && st != Battery_Charging_CV)
		this->ctrl->start_charging();
	else
		this->ctrl->stop_charging();
	
	this_thread::sleep_for(milliseconds(200));
	this->refresh_ui();

}

void UILayer::on_button_adjust_clicked()
{
	this->infobar->hide();
		
	float vol, cur, chg;
	if (! user_input_value("Input expected current (mA):", &cur,
	                       this->ctrl->control_status().exp_current * 1000.0)) return;
	if (! user_input_value("Input expected voltage (V):", &vol,
	                       this->ctrl->control_status().exp_voltage)) return;
	if (! user_input_value("Input expected increment of battery charge (mAh):", &chg,
	                       this->ctrl->control_status().exp_charge * 1000.0 / 3600.0)) return;
	
	Gtk::MessageDialog msg_dlg(*this->window, "Enable const voltage stage?",
							   false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
	Gtk::ResponseType resp = (Gtk::ResponseType) msg_dlg.run();
	msg_dlg.close();
	
	this->ctrl->conf.opt_stage_const_v = (resp == Gtk::RESPONSE_YES);
	this->ctrl->set_charge_exp(cur / 1000.0, vol, chg * 3600.0 / 1000.0);
}

void UILayer::on_button_calibrate_clicked()
{
	this->infobar->hide();
	
	float v_adc1_actual, v_bat_actual, v_ext_power, r_extra;
	if (user_input_value("Input your measurement of current ADC1 input voltage (V):",
		                 &v_adc1_actual, this->ctrl->control_status().bat_voltage)) {
		this->ctrl->calibrate(v_adc1_actual);
	} else {
		if (! user_input_value("Well, input your measurement of battery voltage (V) instead:",
			                   &v_bat_actual, this->ctrl->control_status().bat_voltage)) return;
		if (! user_input_value("Input charge supply voltage (V):",
		                       &v_ext_power, this->ctrl->conf.v_ext_power)) return;
		if (! user_input_value("Input extra resistance of battery (and charge supply) connection (Ohm):",
		                       &r_extra, this->ctrl->conf.r_extra)) return;
		
		this->ctrl->calibrate(v_bat_actual, v_ext_power, r_extra);
	}
	
	Gtk::MessageDialog msg_dlg(*this->window,
	                           "On-chip VRefInt is calculated: " + to_string(this->ctrl->conf.v_refint) + " V.",
							   false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
	msg_dlg.run(); msg_dlg.close();
}

void UILayer::on_button_open_clicked()
{
	this->infobar->hide();
	
	if (this->rec->is_recording()) this->on_button_on_off_clicked();
	
	this->file_dialog->set_title("Open .csv file");
	this->file_dialog->set_action(Gtk::FILE_CHOOSER_ACTION_OPEN);
	Gtk::ResponseType resp = (Gtk::ResponseType) this->file_dialog->run();
	this->file_dialog->close();
	if (resp != Gtk::RESPONSE_OK) return;
	
	string path = this->file_dialog->get_file()->get_path();
	this->rec->open_csv(path);
}

void UILayer::on_button_save_clicked()
{
	this->infobar->hide();
	
	using Glib::str_has_suffix;
	#ifdef _WIN32
		const string Slash = "\\";
	#else
		const string Slash = "/";
	#endif
	
	if (this->rec->is_recording()) this->on_button_on_off_clicked();
	
	this->file_dialog->set_title("Save as .csv file");
	this->file_dialog->set_action(Gtk::FILE_CHOOSER_ACTION_SAVE);
	Gtk::ResponseType resp = (Gtk::ResponseType) this->file_dialog->run();
	this->file_dialog->close();
	if (resp != Gtk::RESPONSE_OK) return;
	
	string path = this->file_dialog->get_current_folder();
	if (! str_has_suffix(path, Slash)) path += Slash;
	path += this->file_dialog->get_current_name();
	if (! str_has_suffix(path, ".csv")) path += ".csv"; 
	
	bool suc = this->rec->save_csv(path);
	if (! suc) this->window->set_title(this->title + " - Failed to save as file");
}

void UILayer::on_infobar_response(int response)
{
	this->infobar->hide();
}

static void dlg_resp(Gtk::Dialog* dlg, int response_id)
{
	dlg->response(response_id);
}

bool UILayer::user_input_value(const string& str_prompt, float* p_val, float val_default)
{
	// strange: unlike the file dialog, reuse of the input dialog always cause problem
	Gtk::Dialog* input_dialog = new Gtk::Dialog("User Input", *this->window, true);
	Gtk::Label* label_input_dialog = Gtk::manage(new Gtk::Label);
	Gtk::Entry* entry_input = Gtk::manage(new Gtk::Entry);
	entry_input->signal_activate().connect(sigc::bind(sigc::ptr_fun(&dlg_resp), input_dialog, Gtk::RESPONSE_OK));
	input_dialog->get_content_area()->pack_start(*label_input_dialog);
	input_dialog->get_content_area()->pack_start(*entry_input);
	input_dialog->add_button("_OK", Gtk::RESPONSE_OK);
	input_dialog->add_button("_Cancel", Gtk::RESPONSE_CANCEL);
	input_dialog->set_default_size(320, 80);
	input_dialog->show_all_children();
	
	stringstream sst; sst.setf(ios::fixed); sst.precision(3);
	sst << val_default;
	label_input_dialog->set_label(str_prompt);
	entry_input->set_text(sst.str()); sst.clear();
	Gtk::ResponseType resp = (Gtk::ResponseType) input_dialog->run();
	input_dialog->close();
	if (resp == Gtk::RESPONSE_OK) {
		sst.str(entry_input->get_text()); sst >> *p_val;
	}
	delete input_dialog;
	return resp == Gtk::RESPONSE_OK && !sst.fail();
}

void UILayer::close_window() //used by dispatcher_close
{
	this->window->close();	
}

