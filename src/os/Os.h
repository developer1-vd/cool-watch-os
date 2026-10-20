#pragma once
#include <LovyanGFX.hpp>
#include <cstdint>
#include <memory>
#include "hal/Hal.h"

// StarDroid: a tiny Android-inspired OS. Apps are Activities on a back stack,
// started with Intents and drawn into a 128x128 canvas every frame.
namespace os {

using Canvas = LGFX_Sprite;

constexpr int W = 128;
constexpr int H = 128;
constexpr int StatusBarH = 12;

constexpr const char* Name = "StarDroid";
constexpr const char* Version = "1.0";
constexpr const char* Codename = "Cupcake";

// ---- Input -----------------------------------------------------------------

enum class Key : uint8_t { Up, Down, Select, Back };

enum class KeyAction : uint8_t {
	Down,      // pressed
	Up,        // released
	Click,     // released before a long press
	LongPress, // held for LongPressMs
	Repeat,    // auto-repeat while held (Up/Down only)
};

struct KeyEvent {
	Key key;
	KeyAction action;
};

// ---- Activities ------------------------------------------------------------

struct Intent {
	const char* component; // app id
	int32_t extra = 0;
};

class Activity {
public:
	virtual ~Activity() = default;

	virtual void onCreate(const Intent&){}
	virtual void onResume(){}
	virtual void onPause(){}
	virtual void onDestroy(){}

	virtual void onUpdate(uint32_t dtMs){}
	virtual void onDraw(Canvas& c) = 0;
	virtual bool onKey(const KeyEvent& e){ return false; }

	virtual bool showStatusBar() const { return true; }
	virtual bool keepScreenOn() const { return false; }
	virtual int8_t brightnessOverride() const { return -1; } // 0..100, -1 = user setting
	virtual uint16_t background() const;
};

using ActivityFactory = std::unique_ptr<Activity> (*)();
using IconFn = void (*)(Canvas& c, int cx, int cy, uint16_t fg);

// Hardware an app can't work without (AppInfo::needs).
enum Needs : uint8_t {
	NeedsBacklight = 1,   // e.g. the flashlight
	NeedsFastDisplay = 2, // real-time games; not on e-paper
};

struct AppInfo {
	const char* id;
	const char* label;
	uint16_t color;
	IconFn icon;
	ActivityFactory create;
	bool inDrawer;
	uint8_t needs = 0;
};

template<typename T>
std::unique_ptr<Activity> make(){ return std::unique_ptr<Activity>(new T()); }

void registerApp(const AppInfo& app);
void registerApps(); // the built-in app list, apps/Apps.cpp
int appCount();
const AppInfo& app(int i);
const AppInfo* findApp(const char* id);
bool appAvailable(const AppInfo& app); // its Needs are met on this watch

void startActivity(const Intent& intent);
void replaceActivity(const Intent& intent); // finish() + start, with a fade
void finish();  // pops the current activity (Back)
void goHome();  // clears the stack down to the launcher
Activity* current();
const char* currentId(); // app id of the top activity
int stackDepth();

// ---- System services -------------------------------------------------------

struct Notification {
	const char* appId;
	char title[24];
	char text[48];
	time_t when;
	bool unread;
};

void notify(const char* appId, const char* title, const char* text, bool headsUp = true);
int notificationCount();
const Notification& notification(int i); // 0 = newest
void dismissNotification(int i);
void clearNotifications();
int unreadCount();
void markAllRead();

void toast(const char* text);

// Sounds respect the "Sound" quick setting.
void beep(uint16_t freq, uint16_t ms);
enum class Sound : uint8_t { Click, Notify, Alarm, Success, Error, Boot };
void play(Sound s);

void wakeScreen();
void sleepScreen();
void userActivity(); // resets the screen-off timer
bool screenOn();
// E-paper keeps showing the watch face while the CPU sleeps, redrawn once a
// minute. Faces should leave out seconds while this is true.
bool ambient();

// Background work that keeps running while the screen is off (timers,
// alarms). The device sleeps until the earliest msUntilNext().
class Service {
public:
	virtual ~Service() = default;
	virtual void poll(){}
	virtual uint32_t msUntilNext(){ return UINT32_MAX; }
};
void addService(Service* s);

// ---- Settings ----------------------------------------------------------------

struct Settings {
	uint8_t brightness;   // 10..100
	uint8_t screenTimeout; // seconds
	bool sound;
	bool dnd;
	uint8_t watchFace;
	uint8_t accent;       // index into accent palette
	uint16_t stepGoal;
	bool use24h;
	bool developer;
	bool showFps;
	bool paper; // e-paper: black ink on white instead of the dark theme
};

Settings& settings();
void saveSettings();

// ---- Time & health ---------------------------------------------------------------

struct DateTime {
	int year, month, day; // month 1..12
	int hour, minute, second;
	int weekday;          // 0 = Sunday
};

DateTime localTime();
time_t makeTime(const DateTime& dt);
const char* weekdayName(int wd, bool shortName = true);
const char* monthName(int m, bool shortName = true); // 1..12

uint32_t stepsToday();
uint32_t stepsOnDay(int daysAgo); // 1..6, from history

namespace systemui {
void openShade(); // quick settings
}

// ---- Main loop -------------------------------------------------------------

void begin();
void tick();

}
