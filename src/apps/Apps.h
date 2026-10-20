#pragma once
#include "os/Os.h"
#include "os/Ui.h"

namespace apps {

extern const os::AppInfo Boot, Setup, Launcher, FacePicker, Drawer, Notifications;
extern const os::AppInfo Settings, Fit, Heart, Stopwatch, Timer, Alarm, Ringing;
extern const os::AppInfo Flashlight, Level, Calendar, Snake, Runner, HwTest;

// Watch faces (Faces.cpp)
struct FaceInfo {
	const char* name;
	void (*draw)(os::Canvas& c, const os::DateTime& t, float secFrac);
};
int faceCount();
const FaceInfo& face(int i);

// Background services
void startTimerService();
void startAlarmService();

}
