// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#ifndef UI_LAYER_H
#define UI_LAYER_H

#include "control_layer.h"
#include "simple-cairo-plot/recorder.h"

#include <gtkmm/window.h>
#include <gtkmm/button.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/infobar.h>
#include <gtkmm/filechooserdialog.h>

using namespace std;

const unsigned int UI_Refresh_Interval_Min = 500; //ms

class UILayer: public sigc::trackable
{
	ChargeControlLayer* ctrl;
	SimpleCairoPlot::Recorder* rec; unsigned int buf_size;
	
	thread* thread_gtk = NULL;
	Glib::Dispatcher* dispatcher_refresh = NULL; ChargeControlEvent last_event;
	Glib::Dispatcher* dispatcher_close = NULL;
	
	Gtk::Window* window = NULL;
	Gtk::FileChooserDialog* file_dialog = NULL;
	Gtk::InfoBar* infobar = NULL; Gtk::Label* label_infobar = NULL;
	Gtk::Button* button_on_off = NULL;
	Gtk::Button* button_adjust = NULL, * button_calibrate = NULL;
	Gtk::Button* button_open = NULL, * button_save = NULL;
	Gtk::Label* label_status = NULL;
	
	bool flag_event = false;
	steady_clock::time_point t_status_refresh;
	
	void create_window();
	void create_file_dialog();
	void app_run();
	
	void control_event_callback(ChargeControlEvent ev);
	void refresh_ui();
	
	void on_buffers_full();
	void on_button_on_off_clicked();
	void on_button_open_clicked();
	void on_button_save_clicked();
	void on_button_adjust_clicked();
	void on_button_calibrate_clicked();
	void on_infobar_response(int response);
	
	bool user_input_value(const string& str_prompt, float* p_val, float val_default = 0); 
	
	void close_window();
	
public:
	const std::string App_Name = "org.usb-vcp-mcu-charge-controller.monitor";
	string title = "Charge Monitor";
	
	UILayer(); void init(ChargeControlLayer* ctrl, unsigned int buf_size = 2*12*3600);
	UILayer(ChargeControlLayer* ctrl, unsigned int buf_size = 2*12*3600);
	UILayer(const UILayer&) = delete;
	UILayer& operator=(const UILayer&) = delete;
	virtual ~UILayer();
	
	void open(); //create a new thread to run the frontend
	SimpleCairoPlot::Recorder& recorder() const; //notice: don't keep the returned reference when you need to close the frontend
	void run(); //run in current thread or join the existing frontend thread, blocks
	void close();
};

#endif

