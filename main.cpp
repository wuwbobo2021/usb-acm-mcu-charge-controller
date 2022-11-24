// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "control_layer.h"
#include "ui_layer.h"

#include <fstream>

using namespace std;

ChargeControlLayer ctrl;
UILayer ui;

const string conf_file_name = "charge_control_config.bin";

int main(int argc, char** argv)
{
	string path; path = argv[0];
	path = path.substr(0, path.find_last_of("/\\") + 1);
	
	ChargeControlConfig conf;
	
	ifstream ifs(path + conf_file_name, ios::binary);
	if (ifs) {
		ifs.read((char*)&conf, sizeof(conf));
		if (ifs) ctrl.set_hard_config(conf);
		ifs.close();
	}
	
	ui.init(&ctrl);
	ui.run(); //blocks
	
	if (ctrl.hard_config() != conf) { //config changed
		conf = ctrl.hard_config();
		ofstream ofs(path + conf_file_name, ios::binary | ios::trunc);
		if (ofs) ofs.write((char*)&conf, sizeof(conf));
	}
	
	return 0;
}
