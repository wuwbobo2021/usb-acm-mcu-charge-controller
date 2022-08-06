// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef UI_LAYER_H
#define UI_LAYER_H

#include "control_layer.h"
#include "locale.h"
#include "simple-cairo-plot/recorder.h"

#include <iostream>
#include <sstream>

#include <gtkmm/window.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/filechooserdialog.h>

using namespace std;

const unsigned int Recorder_Interval   = 100, //ms
                   UI_Refresh_Interval = 1000;

class UILayer: public sigc::trackable
{
	ChargeControlLayer* ctrl;
	SimpleCairoPlot::Recorder* rec; unsigned int buf_size;
	
	thread* thread_gtk = NULL;
	Glib::Dispatcher* dispatcher_refresh = NULL; ChargeControlEvent last_event;
	Glib::Dispatcher* dispatcher_close = NULL;
	
	Locale locale_str; stringstream sst;
	
	Gtk::Window* window = NULL;
	Gtk::FileChooserDialog* file_dialog = NULL;
	
	Gtk::Button* button_on_off = NULL,
	           * button_config = NULL, * button_calibrate = NULL,
	           * button_open = NULL, * button_save = NULL,
	           * button_apply = NULL;
	
	Gtk::Entry* entry_exp_current = NULL,
	          * entry_exp_voltage = NULL,
	          * entry_exp_charge = NULL,
	          * entry_min_current = NULL;
	
	Gtk::CheckButton* chk_stage_const_v = NULL;
	
	Gtk::Label* label_status = NULL;
	
	bool flag_event = false, flag_show_event = false; ChargeControlState st_last;
	steady_clock::time_point t_status_refresh;
	
	void create_window();
	void create_file_dialog();
	void app_run();
	
	void control_event_callback(ChargeControlEvent ev);
	void refresh_ui();
	void refresh_window_title();
	
	void on_buffers_full();
	
	void on_button_on_off_clicked();
	void on_button_config_clicked();
	void on_button_calibrate_clicked();
	void on_button_open_clicked();
	void on_button_save_clicked();
	void on_button_apply_clicked();
	void on_chk_stage_const_v_toggled();
	
	bool user_input_value(const string& str_prompt, float* p_val, float val_default = 0); 
	void show_param_values();
	
	bool open_file(SimpleCairoPlot::Recorder* recorder);
	bool save_as_file(SimpleCairoPlot::Recorder* recorder, const string& file_name_default, const string& comment);
	void dac_scan(Gtk::Window& parent_window);
	
	void close_window();
	
public:
	const std::string App_Name = "org.usb-vcp-mcu-charge-controller.monitor";
	
	UILayer(); void init(ChargeControlLayer* ctrl, unsigned int buf_size = 12 * 3600 * 1000 / Recorder_Interval);
	UILayer(ChargeControlLayer* ctrl, unsigned int buf_size = 12 * 3600 * 1000 / Recorder_Interval);
	UILayer(const UILayer&) = delete;
	UILayer& operator=(const UILayer&) = delete;
	virtual ~UILayer();
	
	void open(); //create a new thread to run the frontend
	SimpleCairoPlot::Recorder& recorder() const; //notice: don't keep the returned reference when you need to close the frontend
	void run(); //run in current thread or join the existing frontend thread, blocks
	void close();
};

inline void UILayer::refresh_window_title()
{
	if (flag_show_event) {
		if (last_event != Event_New_Data)
			this->window->set_title(locale_str.window_title(last_event));
	} else {	
		st_last = this->ctrl->control_status().control_state;
		this->window->set_title(locale_str.window_title(st_last));
	}
		
}

#endif
