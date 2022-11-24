// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef UI_LAYER_H
#define UI_LAYER_H

#include "control_layer.h"
#include "ui_locale.h"
#include "simple-cairo-plot/recorder.h"

#include <iostream>
#include <sstream>

#include <gtkmm/window.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/filechooserdialog.h>

using namespace std;

const unsigned int Recorder_Interval   = 100, //ms
                   UI_Refresh_Interval = 1000,
                   
                   Buffer_Size_Default = 12 * 3600 * 1000 / Recorder_Interval;

class UILayer: public sigc::trackable
{
	ChargeControlLayer* ctrl;
	SimpleCairoPlot::Recorder* rec; unsigned int buf_size;
	
	thread* thread_gtk = NULL;
	Glib::Dispatcher* dispatcher_refresh = NULL;
	Glib::Dispatcher* dispatcher_close = NULL;
	
	Locale locale_str; stringstream sst;
	
	Gtk::Window* window = NULL;
	Gtk::MessageDialog* notify_dialog = NULL;
	Gtk::FileChooserDialog* file_dialog = NULL;
	
	Gtk::Button* button_on_off = NULL,
	           * button_config = NULL, * button_calibrate = NULL,
	           * button_open = NULL, * button_save = NULL,
	           * button_apply = NULL;
	
	Gtk::Entry* entry_exp_current = NULL,
	          * entry_exp_voltage = NULL,
	          * entry_exp_voltage_oc = NULL,
	          * entry_exp_charge = NULL,
	          * entry_min_current = NULL,
			  * entry_time_limit = NULL;
	
	Gtk::CheckButton* chk_stage_const_v = NULL;
	
	Gtk::Label* label_status = NULL;
	
	volatile bool flag_event = false; volatile ChargeControlEvent last_event;
	volatile bool flag_event_notify = false;
	steady_clock::time_point t_status_refresh;
	
	void create_window();
	void create_notify_dialog();
	void create_file_dialog();
	void app_run();
	
	void control_event_callback(ChargeControlEvent ev);
	void refresh_ui();
	
	void on_buffers_full();
	
	void on_notify_dialog_response(int response_id);
	
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
	
	UILayer(); void init(ChargeControlLayer* ctrl, unsigned int buf_size = Buffer_Size_Default);
	UILayer(ChargeControlLayer* ctrl, unsigned int buf_size = Buffer_Size_Default);
	UILayer(const UILayer&) = delete;
	UILayer& operator=(const UILayer&) = delete;
	virtual ~UILayer();
	
	void run(); //run in current thread or join the existing frontend thread, blocks
	void close();
};

#endif
