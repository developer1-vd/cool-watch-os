#include "Apps.h"

void os::registerApps(){
	using namespace apps;
	// System activities
	for(const auto* a : { &Boot, &Setup, &Launcher, &FacePicker, &Drawer, &Notifications, &Ringing, &HwTest }){
		registerApp(*a);
	}
	// App drawer, in display order
	for(const auto* a : { &Fit, &Heart, &Stopwatch, &Timer, &Alarm, &Calendar, &Flashlight, &Level, &Snake, &Dino, &apps::Settings }){
		registerApp(*a);
	}
	startTimerService();
	startAlarmService();
}
