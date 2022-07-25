// by wuwbobo2021 <https://github.com/wuwbobo2021>, <wuwbobo@outlook.com>
// If you have found bugs in this program, please pull an issue, or contact me.

#include "control_layer.h"
#include "ui_layer.h"

using namespace std;

ChargeControlLayer ctrl;
UILayer ui;

int main(int argc, char** argv)
{
	ChargeControlConfig conf = ctrl.hard_config();
	conf.div_prop = 5.41 / (3.04 + 5.41);
	ctrl.set_hard_config(conf);
	
	ui.init(&ctrl);
	ui.run(); //blocks
	
	return 0;
}
