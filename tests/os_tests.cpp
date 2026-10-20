// StarDroid test program.
//
// Runs the real OS and apps on the headless simulator with a virtual clock and
// scripted buttons. Each test runs in its own forked process, so global OS
// state is fresh every time and a crash fails one test instead of the run.
//
//   stardroid-tests            run everything
//   stardroid-tests timer      run tests whose name contains "timer"
#include "hal/SimControl.h"
#include "os/Os.h"
#include "os/Ui.h"
#include "hal/Mono.h"
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// ---- Minimal test framework -------------------------------------------------------

namespace {

struct TestCase {
	const char* name;
	void (*fn)();
};

std::vector<TestCase>& registry(){
	static std::vector<TestCase> tests;
	return tests;
}

struct Registrar {
	Registrar(const char* name, void (*fn)()){ registry().push_back({ name, fn }); }
};

[[noreturn]] void fail(const char* file, int line, const std::string& what){
	printf("    %s:%d: %s\n", file, line, what.c_str());
	fflush(stdout);
	_exit(1);
}

}

#define TEST(name) \
	static void test_##name(); \
	static Registrar reg_##name(#name, test_##name); \
	static void test_##name()

#define CHECK(cond) do { if(!(cond)) fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); } while(0)

#define CHECK_EQ(a, b) do { \
	const auto va = (a); const auto vb = (b); \
	if(!(va == vb)) fail(__FILE__, __LINE__, std::string(#a " == " #b ": got ") + std::to_string(va) + " vs " + std::to_string(vb)); \
} while(0)

#define CHECK_NEAR(a, b, tol) do { \
	const double va = (a), vb = (b); \
	if(fabs(va - vb) > (tol)) fail(__FILE__, __LINE__, std::string(#a " ~ " #b ": got ") + std::to_string(va) + " vs " + std::to_string(vb)); \
} while(0)

#define CHECK_STR(a, b) do { \
	const char* va = (a); const char* vb = (b); \
	if(!va || strcmp(va, vb) != 0) fail(__FILE__, __LINE__, std::string(#a " == \"") + vb + "\": got \"" + (va ? va : "(null)") + "\""); \
} while(0)

// ---- Harness ---------------------------------------------------------------------

namespace {

using hal::Button;

constexpr uint32_t FrameMs = 25;
constexpr time_t StartEpoch = 1792318170; // Sun 2026-10-18 10:09:30

void run(uint32_t ms){
	for(uint32_t t = 0; t < ms; t += FrameMs){
		sim::advance(FrameMs);
		os::tick();
	}
}

void press(Button b, uint32_t holdMs = 75){
	sim::setButton(b, true);
	run(holdMs);
	sim::setButton(b, false);
	run(150);
}

void hold(Button b, uint32_t ms){ press(b, ms); }

// Boots to the watch face. With firstRun the setup wizard is left to run.
void boot(bool firstRun = false){
	sim::setHeadless(StartEpoch);
	if(!firstRun) hal::prefSet("setup", 1);
	os::begin();
	run(3500);
}

void open(const char* id){
	os::startActivity({ id });
	run(400);
	CHECK_STR(os::currentId(), id);
}

uint32_t frameChecksum(){
	const uint16_t* px = sim::lastFrame();
	uint32_t h = 2166136261u;
	for(int i = 0; i < 128 * 128; i++) h = (h ^ px[i]) * 16777619u;
	return h;
}

// An activity that records what happens to it.
std::vector<std::string> events;

class Probe : public os::Activity {
public:
	void onCreate(const os::Intent& i) override { events.push_back("create:" + std::to_string(i.extra)); }
	void onResume() override { events.push_back("resume"); }
	void onPause() override { events.push_back("pause"); }
	void onDestroy() override { events.push_back("destroy"); }
	void onDraw(os::Canvas& c) override { c.fillRect(10, 20, 30, 30, ui::col::Red); }
	bool onKey(const os::KeyEvent& e) override {
		static const char* keys[] = { "up", "down", "select", "back" };
		static const char* acts[] = { "down", "up", "click", "long", "repeat" };
		events.push_back(std::string(keys[(int) e.key]) + "." + acts[(int) e.action]);
		return e.key != os::Key::Back;
	}
};

void registerProbe(){
	os::registerApp({ "probe", "Probe", 0, nullptr, os::make<Probe>, false });
}

int count(const std::string& what){
	int n = 0;
	for(auto& e : events) n += e == what;
	return n;
}

}

// ---- Time --------------------------------------------------------------------------

TEST(make_time_matches_gmtime){
	const os::DateTime dates[] = {
		{ 1970, 1, 1, 0, 0, 0, 0 }, { 2000, 2, 29, 12, 0, 0, 0 }, { 2026, 10, 18, 10, 9, 30, 0 },
		{ 2028, 2, 29, 23, 59, 59, 0 }, { 2099, 12, 31, 23, 59, 59, 0 },
	};
	for(const auto& d : dates){
		const time_t t = os::makeTime(d);
		tm p;
		gmtime_r(&t, &p);
		CHECK_EQ(p.tm_year + 1900, d.year);
		CHECK_EQ(p.tm_mon + 1, d.month);
		CHECK_EQ(p.tm_mday, d.day);
		CHECK_EQ(p.tm_hour, d.hour);
		CHECK_EQ(p.tm_min, d.minute);
		CHECK_EQ(p.tm_sec, d.second);
	}
}

TEST(local_time_and_names){
	sim::setHeadless(StartEpoch);
	const auto t = os::localTime();
	CHECK_EQ(t.year, 2026);
	CHECK_EQ(t.month, 10);
	CHECK_EQ(t.day, 18);
	CHECK_EQ(t.weekday, 0);
	CHECK_STR(os::weekdayName(t.weekday), "Sun");
	CHECK_STR(os::weekdayName(3, false), "Wednesday");
	CHECK_STR(os::monthName(10), "Oct");
	CHECK_STR(os::monthName(1, false), "January");
	CHECK_STR(os::monthName(12), "Dec");
}

TEST(set_time_moves_the_clock){
	boot();
	os::DateTime d = { 2027, 3, 4, 5, 6, 7, 0 };
	hal::setTime(os::makeTime(d));
	run(2000);
	const auto t = os::localTime();
	CHECK_EQ(t.year, 2027);
	CHECK_EQ(t.month, 3);
	CHECK_EQ(t.day, 4);
	CHECK_EQ(t.hour, 5);
	CHECK_EQ(t.minute, 6);
	CHECK(t.second >= 8 && t.second <= 10);
}

// ---- Widgets -------------------------------------------------------------------------

TEST(picker_wraps_confirms_and_cancels){
	ui::Picker p;
	p.set({ { 5, 0, 10, ":" }, { 0, 0, 59, nullptr } });
	bool cancelled = false;
	p.onKey({ os::Key::Up, os::KeyAction::Down }, cancelled);
	CHECK_EQ(p.value(0), 6);
	for(int i = 0; i < 7; i++) p.onKey({ os::Key::Down, os::KeyAction::Down }, cancelled);
	CHECK_EQ(p.value(0), 10); // 6 -> 0 -> wraps to 10
	p.onKey({ os::Key::Down, os::KeyAction::Repeat }, cancelled);
	CHECK_EQ(p.value(0), 9);

	CHECK(!p.onKey({ os::Key::Select, os::KeyAction::Click }, cancelled));
	CHECK_EQ(p.field, 1);
	p.onKey({ os::Key::Down, os::KeyAction::Down }, cancelled);
	CHECK_EQ(p.value(1), 59);
	CHECK(p.onKey({ os::Key::Select, os::KeyAction::Click }, cancelled));

	p.field = 1;
	p.onKey({ os::Key::Back, os::KeyAction::Click }, cancelled);
	CHECK(!cancelled);
	CHECK_EQ(p.field, 0);
	p.onKey({ os::Key::Back, os::KeyAction::Click }, cancelled);
	CHECK(cancelled);
}

TEST(list_view_wraps){
	ui::ListView l;
	l.set(3, [](void*, int){ return ui::ListView::Row{ "row", nullptr, 0, nullptr, -1 }; }, nullptr);
	CHECK(l.onKey({ os::Key::Up, os::KeyAction::Down }));
	CHECK_EQ(l.selected, 2);
	CHECK(l.onKey({ os::Key::Down, os::KeyAction::Down }));
	CHECK_EQ(l.selected, 0);
	CHECK(!l.onKey({ os::Key::Down, os::KeyAction::Up }));
	CHECK(!l.onKey({ os::Key::Select, os::KeyAction::Down }));
}

// ---- Boot & setup --------------------------------------------------------------------

TEST(first_boot_runs_setup){
	boot(true);
	CHECK_STR(os::currentId(), "setup");
}

TEST(later_boots_go_to_watch_face){
	boot();
	CHECK_STR(os::currentId(), "launcher");
	CHECK(os::screenOn());
}

TEST(setup_wizard_completes){
	boot(true);
	// welcome, time (2 fields), date (3), format, theme, face, goal, tour
	for(int i = 0; i < 11; i++) press(Button::Select);
	run(3000);
	CHECK_STR(os::currentId(), "launcher");
	CHECK_EQ(hal::prefGet("setup", 0), 1);
	CHECK_EQ(os::notificationCount(), 1); // the welcome notification
}

TEST(setup_wizard_sets_time_and_choices){
	boot(true);
	press(Button::Select);                                 // welcome
	for(int i = 0; i < 3; i++) press(Button::Up);          // hour 10 -> 13
	press(Button::Select);
	press(Button::Down);                                   // minute 09 -> 08
	press(Button::Select);
	for(int i = 0; i < 3; i++) press(Button::Select);      // date unchanged
	press(Button::Down);                                   // 12-hour
	press(Button::Select);
	press(Button::Down);                                   // accent 1
	press(Button::Select);
	press(Button::Down);                                   // face 1
	press(Button::Select);
	press(Button::Up);                                     // goal 9000
	press(Button::Select);
	press(Button::Select);                                 // tour
	run(3000);
	CHECK_STR(os::currentId(), "launcher");
	const auto t = os::localTime();
	CHECK_EQ(t.hour, 13);
	CHECK_EQ(t.minute, 8);
	const auto& s = os::settings();
	CHECK(!s.use24h);
	CHECK_EQ(s.accent, 1);
	CHECK_EQ(s.watchFace, 1);
	CHECK_EQ(s.stepGoal, 9000);
	CHECK_EQ(hal::prefGet("goal", 0), 9000);
}

// ---- Input -------------------------------------------------------------------------------

TEST(click_long_press_and_repeat){
	boot();
	registerProbe();
	open("probe");
	events.clear();

	press(Button::Select);
	CHECK_EQ(count("select.down"), 1);
	CHECK_EQ(count("select.up"), 1);
	CHECK_EQ(count("select.click"), 1);
	CHECK_EQ(count("select.long"), 0);

	events.clear();
	hold(Button::Select, 900);
	CHECK_EQ(count("select.long"), 1);
	CHECK_EQ(count("select.click"), 0); // a long press is not also a click
	CHECK_EQ(count("select.repeat"), 0); // only Up/Down repeat

	events.clear();
	hold(Button::Down, 1000);
	const int repeats = count("down.repeat");
	CHECK(repeats >= 5 && repeats <= 7);
}

TEST(short_glitches_are_debounced){
	boot();
	registerProbe();
	open("probe");
	events.clear();
	sim::setButton(Button::Up, true);
	sim::advance(5); // shorter than one frame and the debounce window
	os::tick();
	sim::setButton(Button::Up, false);
	run(200);
	CHECK_EQ(count("up.down"), 0);
}

// ---- Activities ------------------------------------------------------------------------

TEST(activity_lifecycle_and_back_stack){
	boot();
	registerProbe();
	const int base = os::stackDepth();
	events.clear();
	os::startActivity({ "probe", 7 });
	run(400);
	CHECK_EQ(os::stackDepth(), base + 1);
	CHECK_EQ(count("create:7"), 1);
	CHECK_EQ(count("resume"), 1);

	open("drawer");
	CHECK_EQ(count("pause"), 1);
	press(Button::Alt); // Back
	CHECK_STR(os::currentId(), "probe");
	CHECK_EQ(count("resume"), 2);

	press(Button::Alt); // Probe lets Back through
	CHECK_STR(os::currentId(), "launcher");
	CHECK_EQ(count("destroy"), 1);
	CHECK_EQ(os::stackDepth(), base);
}

TEST(quick_double_back_is_not_dropped){
	boot();
	open("drawer");
	open("settings");
	// Second press lands while the first slide-out animation is still running.
	sim::setButton(Button::Alt, true); run(50); sim::setButton(Button::Alt, false); run(50);
	sim::setButton(Button::Alt, true); run(50); sim::setButton(Button::Alt, false); run(400);
	CHECK_STR(os::currentId(), "launcher");
}

TEST(go_home_clears_the_stack){
	boot();
	open("drawer");
	open("settings");
	open("notifications");
	os::goHome();
	run(400);
	CHECK_STR(os::currentId(), "launcher");
	CHECK_EQ(os::stackDepth(), 1);
}

TEST(unknown_app_is_ignored){
	boot();
	os::startActivity({ "does-not-exist" });
	run(400);
	CHECK_STR(os::currentId(), "launcher");
}

TEST(watch_face_buttons){
	boot();
	press(Button::Select);
	run(300);
	CHECK_STR(os::currentId(), "drawer");
	press(Button::Alt);
	run(300);
	press(Button::Down);
	run(300);
	CHECK_STR(os::currentId(), "notifications");
	press(Button::Alt);
	run(300);
	hold(Button::Select, 900);
	run(300);
	CHECK_STR(os::currentId(), "facepicker");
	press(Button::Down);
	press(Button::Select);
	run(300);
	CHECK_STR(os::currentId(), "launcher");
	CHECK_EQ(os::settings().watchFace, 1);
}

TEST(drawer_lists_every_app){
	boot();
	int inDrawer = 0;
	for(int i = 0; i < os::appCount(); i++) inDrawer += os::app(i).inDrawer;
	CHECK_EQ(inDrawer, 11);
	// Walk the drawer and open each entry
	for(int i = 0; i < inDrawer; i++){
		open("drawer");
		for(int j = 0; j < i; j++) press(Button::Down);
		press(Button::Select);
		run(400);
		CHECK(os::stackDepth() == 3);
		os::goHome();
		run(400);
	}
}

TEST(every_app_survives_input){
	boot();
	for(int i = 0; i < os::appCount(); i++){
		const auto& app = os::app(i);
		if(!strcmp(app.id, "boot") || !strcmp(app.id, "setup")) continue;
		os::startActivity({ app.id });
		run(400);
		for(Button b : { Button::Up, Button::Down, Button::Select, Button::Up, Button::Select }) press(b);
		hold(Button::Down, 700);
		run(500);
		os::goHome();
		run(400);
		CHECK_STR(os::currentId(), "launcher");
	}
}

TEST(random_button_mashing){
	boot();
	srand(1234);
	for(int i = 0; i < 1500; i++){
		const Button b = (Button) (rand() % 4);
		// Avoid the power menu's Power off / Restart, which end the process.
		if(b == Button::Alt && rand() % 4 == 0) continue;
		press(b, 20 + rand() % 120);
		if(rand() % 50 == 0) run(rand() % 3000);
		CHECK(os::stackDepth() >= 1);
		if(!os::screenOn()) press(Button::Up);
	}
}

// ---- System UI ------------------------------------------------------------------------

TEST(notifications_are_capped_and_tracked){
	boot();
	for(int i = 0; i < 10; i++) os::notify("timer", "Title", "Text", false);
	CHECK_EQ(os::notificationCount(), 8);
	CHECK_EQ(os::unreadCount(), 8);
	os::dismissNotification(0);
	CHECK_EQ(os::notificationCount(), 7);
	os::markAllRead();
	CHECK_EQ(os::unreadCount(), 0);
	os::clearNotifications();
	CHECK_EQ(os::notificationCount(), 0);
}

TEST(newest_notification_first){
	boot();
	os::notify("timer", "first", "", false);
	os::notify("alarm", "second", "", false);
	CHECK_STR(os::notification(0).title, "second");
	CHECK_STR(os::notification(1).title, "first");
}

TEST(notification_wakes_screen_unless_dnd){
	boot();
	os::sleepScreen();
	run(200);
	os::notify("timer", "Hi", "", true);
	run(200);
	CHECK(os::screenOn());

	os::settings().dnd = true;
	os::sleepScreen();
	run(200);
	os::notify("timer", "Quiet", "", true);
	run(200);
	CHECK(!os::screenOn());
	CHECK_EQ(os::notificationCount(), 2); // still delivered
}

TEST(opening_notifications_marks_them_read){
	boot();
	os::notify("timer", "Hi", "", false);
	CHECK_EQ(os::unreadCount(), 1);
	press(Button::Down);
	run(300);
	press(Button::Alt);
	run(300);
	CHECK_EQ(os::unreadCount(), 0);
}

TEST(quick_settings_toggles_sound){
	boot();
	const bool before = os::settings().sound;
	press(Button::Up);           // open the shade
	run(300);
	press(Button::Down);         // brightness
	press(Button::Down);         // sound
	press(Button::Select);
	CHECK(os::settings().sound != before);
	CHECK_EQ(hal::prefGet("sound", -1), (int) os::settings().sound);
	press(Button::Alt);
	CHECK_STR(os::currentId(), "launcher");
}

TEST(power_menu_cancels){
	boot();
	const uint32_t face = frameChecksum();
	hold(Button::Alt, 900);
	run(400);
	CHECK(frameChecksum() != face); // menu drawn over the face
	press(Button::Alt);
	run(400);
	CHECK_STR(os::currentId(), "launcher");
	CHECK(os::screenOn());
}

// ---- Power -------------------------------------------------------------------------------

TEST(screen_turns_off_after_timeout){
	boot();
	os::settings().screenTimeout = 5;
	os::userActivity();
	run(4000);
	CHECK(os::screenOn());
	run(1500);
	CHECK(!os::screenOn());
}

TEST(wake_press_is_swallowed){
	boot();
	os::sleepScreen();
	run(500);
	press(Button::Select);
	run(300);
	CHECK(os::screenOn());
	CHECK_STR(os::currentId(), "launcher"); // did not open the drawer
}

TEST(sleep_returns_to_watch_face){
	boot();
	open("settings");
	os::sleepScreen();
	run(300);
	press(Button::Up);
	run(300);
	CHECK_STR(os::currentId(), "launcher");
}

TEST(user_input_postpones_timeout){
	boot();
	os::settings().screenTimeout = 5;
	os::userActivity();
	for(int i = 0; i < 5; i++){
		run(3000);
		press(Button::Up);
		press(Button::Alt);
	}
	CHECK(os::screenOn());
}

TEST(keep_screen_on_activities){
	boot();
	os::settings().screenTimeout = 5;
	open("flashlight");
	run(20000);
	CHECK(os::screenOn());
	CHECK_STR(os::currentId(), "flashlight");
}

// ---- Clock apps -------------------------------------------------------------------------

TEST(timer_rings_while_asleep){
	boot();
	open("timer");
	for(int i = 0; i < 4; i++) press(Button::Down); // 5 -> 1 minute
	press(Button::Select);
	press(Button::Select);                           // start
	os::sleepScreen();
	run(30000);
	CHECK(!os::screenOn());
	run(32000);
	CHECK(os::screenOn());
	CHECK_STR(os::currentId(), "ringing");
	CHECK_EQ(os::notificationCount(), 1);
	press(Button::Select);
	run(400);
	CHECK(strcmp(os::currentId(), "ringing") != 0);
}

TEST(timer_rejects_zero){
	boot();
	open("timer");
	for(int i = 0; i < 5; i++) press(Button::Down); // 0 minutes
	press(Button::Select);
	press(Button::Select);
	run(5000);
	CHECK_STR(os::currentId(), "timer");
}

TEST(alarm_fires_and_snoozes){
	sim::setHeadless(StartEpoch);
	hal::prefSet("setup", 1);
	hal::prefSet("alH", 10);
	hal::prefSet("alM", 10);
	hal::prefSet("alOn", 1);
	os::begin();
	run(3500);
	os::sleepScreen();
	run(20000);
	CHECK(!os::screenOn());
	run(20000); // past 10:10:00
	CHECK(os::screenOn());
	CHECK_STR(os::currentId(), "ringing");

	press(Button::Down); // snooze
	run(500);
	CHECK(strcmp(os::currentId(), "ringing") != 0);
	os::sleepScreen();
	run(4 * 60000);
	CHECK(!os::screenOn());
	run(70000);
	CHECK_STR(os::currentId(), "ringing");
}

TEST(alarm_does_not_ring_twice_in_a_minute){
	sim::setHeadless(StartEpoch);
	hal::prefSet("setup", 1);
	hal::prefSet("alH", 10);
	hal::prefSet("alM", 10);
	hal::prefSet("alOn", 1);
	os::begin();
	run(35000);
	CHECK_STR(os::currentId(), "ringing");
	press(Button::Select); // dismiss
	run(5000);
	CHECK(strcmp(os::currentId(), "ringing") != 0);
}

// ---- Health ------------------------------------------------------------------------------

TEST(steps_count_from_boot_and_roll_over_at_midnight){
	sim::setStepCounter(1000);
	boot();
	CHECK_EQ(os::stepsToday(), 0u);
	sim::setStepCounter(1500);
	run(100);
	CHECK_EQ(os::stepsToday(), 500u);

	os::DateTime nextDay = { 2026, 10, 19, 0, 0, 5, 0 };
	hal::setTime(os::makeTime(nextDay));
	run(100);
	CHECK_EQ(os::stepsToday(), 0u);
	CHECK_EQ(os::stepsOnDay(1), 500u);
	sim::setStepCounter(1600);
	run(100);
	CHECK_EQ(os::stepsToday(), 100u);
}

TEST(steps_survive_pedometer_reset){
	sim::setStepCounter(2000);
	boot();
	sim::setStepCounter(2300);
	run(100);
	sim::setStepCounter(40); // IMU lost power and restarted from zero
	run(100);
	CHECK_EQ(os::stepsToday(), 340u);
}

TEST(step_history_shifts_daily){
	sim::setStepCounter(0);
	boot();
	for(int day = 0; day < 3; day++){
		sim::setStepCounter((day + 1) * 1000);
		run(100);
		os::DateTime d = { 2026, 10, 19 + day, 0, 0, 1, 0 };
		hal::setTime(os::makeTime(d));
		run(100);
	}
	CHECK_EQ(os::stepsOnDay(1), 1000u);
	CHECK_EQ(os::stepsOnDay(2), 1000u);
	CHECK_EQ(os::stepsOnDay(3), 1000u);
	CHECK_EQ(hal::prefGet("st1", 0), 1000);
}

TEST(heart_rate_from_taps){
	boot();
	open("heart");
	for(int i = 0; i < 12; i++){
		sim::setButton(Button::Select, true);
		run(75);
		sim::setButton(Button::Select, false);
		run(675); // 750 ms between beats = 80 bpm
	}
	run(500);
	CHECK_NEAR(hal::prefGet("hr0", 0), 80, 2);
}

TEST(heart_rate_needs_enough_taps){
	boot();
	open("heart");
	press(Button::Select);
	press(Button::Select);
	run(4000);
	CHECK_EQ(hal::prefGet("hr0", 0), 0);
}

// ---- Settings ------------------------------------------------------------------------------

TEST(settings_persist){
	boot();
	auto& s = os::settings();
	s.brightness = 35;
	s.screenTimeout = 30;
	s.watchFace = 3;
	s.accent = 4;
	s.stepGoal = 12000;
	os::saveSettings();
	CHECK_EQ(hal::prefGet("bright", 0), 35);
	CHECK_EQ(hal::prefGet("timeout", 0), 30);
	CHECK_EQ(hal::prefGet("face", 0), 3);
	CHECK_EQ(hal::prefGet("accent", 0), 4);
	CHECK_EQ(hal::prefGet("goal", 0), 12000);
}

TEST(settings_are_loaded_at_boot){
	sim::setHeadless(StartEpoch);
	hal::prefSet("setup", 1);
	hal::prefSet("face", 2);
	hal::prefSet("24h", 0);
	os::begin();
	CHECK_EQ(os::settings().watchFace, 2);
	CHECK(!os::settings().use24h);
}

TEST(build_number_unlocks_developer_options){
	boot();
	open("settings");
	// Brightness .. Battery, Diagnostics, About
	for(int i = 0; i < 12; i++) press(Button::Down);
	press(Button::Select);
	for(int i = 0; i < 4; i++) press(Button::Down); // Build number
	for(int i = 0; i < 6; i++) press(Button::Select);
	CHECK(!os::settings().developer);
	press(Button::Select);
	CHECK(os::settings().developer);
	CHECK_EQ(hal::prefGet("dev", 0), 1);
}

// ---- Diagnostics ---------------------------------------------------------------------------

TEST(diagnostics_button_stage_keeps_last_click){
	boot();
	open("hwtest");
	const uint32_t buttons = frameChecksum();
	press(Button::Up);
	press(Button::Down);
	press(Button::Select);
	press(Button::Alt); // the last button is Back: must not exit the test
	run(600);
	CHECK_STR(os::currentId(), "hwtest");
	CHECK(frameChecksum() != buttons);
}

TEST(diagnostics_full_run){
	boot();
	open("hwtest");
	for(Button b : { Button::Up, Button::Down, Button::Select, Button::Alt }) press(b);
	run(600);
	for(int i = 0; i < 4; i++) press(Button::Select); // display, backlight, buzzer, LEDs ok
	run(3500);                                        // sensors score themselves
	press(Button::Select);
	run(300);
	CHECK_STR(os::currentId(), "hwtest");
	press(Button::Alt);
	run(400);
	CHECK_STR(os::currentId(), "launcher");
}

TEST(diagnostics_flags_missing_imu){
	sim::setImuPresent(false);
	boot();
	open("hwtest");
	for(Button b : { Button::Up, Button::Down, Button::Select, Button::Alt }) press(b);
	run(600);
	for(int i = 0; i < 4; i++) press(Button::Select);
	run(3500);
	press(Button::Select);
	run(300);
	const uint32_t withoutImu = frameChecksum();
	CHECK_STR(os::currentId(), "hwtest");
	CHECK(withoutImu != 0);
}

// ---- Watchy (e-paper) -----------------------------------------------------------------

namespace {

void bootWatchy(bool firstRun = false){
	sim::setTarget(sim::Target::Watchy);
	boot(firstRun);
}

bool paperPixel(int x, int y){ return mono::pixel(sim::lastMono(), x, y); }

}

TEST(watchy_reports_its_hardware){
	bootWatchy();
	const auto& f = hal::features();
	CHECK(!f.color);
	CHECK(f.persistentDisplay);
	CHECK(!f.backlight);
	CHECK(ui::mono());
	CHECK_STR(os::currentId(), "launcher");
}

TEST(watchy_drawer_hides_apps_it_cannot_run){
	bootWatchy();
	int available = 0;
	for(int i = 0; i < os::appCount(); i++){
		const auto& a = os::app(i);
		if(a.inDrawer && os::appAvailable(a)) available++;
	}
	CHECK_EQ(available, 9); // no Flashlight (no backlight), no Dino (too fast for e-paper)
	CHECK(!os::appAvailable(*os::findApp("flashlight")));
	CHECK(!os::appAvailable(*os::findApp("dino")));
	CHECK(os::appAvailable(*os::findApp("snake")));
}

TEST(watchy_only_refreshes_when_the_image_changes){
	bootWatchy();
	const int before = sim::refreshCount();
	run(5000); // Pixel face has no seconds: nothing should change
	CHECK_EQ(sim::refreshCount(), before);
	press(Button::Select); // open the drawer
	run(500);
	const int afterOpen = sim::refreshCount();
	CHECK(afterOpen > before);
	CHECK(afterOpen - before <= 2); // no animation frames
}

TEST(watchy_ambient_mode_updates_once_a_minute){
	bootWatchy();
	open("drawer");
	os::sleepScreen();
	run(200);
	CHECK(!os::screenOn());
	CHECK(os::ambient());
	CHECK_STR(os::currentId(), "launcher"); // the face stays on screen
	const int before = sim::refreshCount();
	run(3 * 60000);
	const int updates = sim::refreshCount() - before;
	CHECK(updates >= 2 && updates <= 4);
	press(Button::Down);
	CHECK(os::screenOn());
	CHECK(!os::ambient());
}

TEST(watchy_setup_skips_the_colour_step){
	bootWatchy(true);
	// welcome, time (2), date (3), format, face, goal, tour: one less than colour screens
	for(int i = 0; i < 10; i++) press(Button::Select);
	run(3000);
	CHECK_STR(os::currentId(), "launcher");
	CHECK_EQ(os::settings().accent, 0);
}

TEST(watchy_paper_mode_flips_the_screen){
	bootWatchy();
	CHECK(os::settings().paper);
	CHECK(paperPixel(2, 198)); // paper: white background
	const int fulls = sim::fullRefreshCount();
	press(Button::Up);         // quick settings
	press(Button::Down);       // Paper mode tile
	press(Button::Select);
	press(Button::Alt);
	run(300);
	CHECK(!os::settings().paper);
	CHECK(!paperPixel(2, 198)); // dark background
	CHECK(sim::fullRefreshCount() > fulls); // a full refresh clears the inverted ghost
	CHECK_EQ(hal::prefGet("paper", 1), 0);
}

TEST(watchy_refresh_tile_does_a_full_refresh){
	bootWatchy();
	const int fulls = sim::fullRefreshCount();
	press(Button::Up);
	press(Button::Select); // first tile: Refresh screen
	run(300);
	CHECK_EQ(sim::fullRefreshCount(), fulls + 1);
}

TEST(watchy_every_app_survives_input){
	bootWatchy();
	for(int i = 0; i < os::appCount(); i++){
		const auto& app = os::app(i);
		if(!strcmp(app.id, "boot") || !strcmp(app.id, "setup")) continue;
		os::startActivity({ app.id });
		run(400);
		for(Button b : { Button::Up, Button::Down, Button::Select, Button::Up, Button::Select }) press(b);
		run(500);
		os::goHome();
		run(400);
		CHECK_STR(os::currentId(), "launcher");
	}
}

TEST(watchy_random_button_mashing){
	bootWatchy();
	srand(99);
	for(int i = 0; i < 800; i++){
		const Button b = (Button) (rand() % 4);
		if(b == Button::Alt && rand() % 4 == 0) continue;
		press(b, 20 + rand() % 120);
		CHECK(os::stackDepth() >= 1);
		if(!os::screenOn()) press(Button::Up);
	}
}

TEST(watchy_heart_rate_screen_stays_still_while_tapping){
	bootWatchy();
	open("heart");
	press(Button::Select);
	run(300);
	const int before = sim::refreshCount();
	for(int i = 0; i < 6; i++){
		sim::setButton(Button::Select, true);
		run(75);
		sim::setButton(Button::Select, false);
		run(675);
	}
	// Redraws would block input on e-paper and skew the timing
	CHECK_EQ(sim::refreshCount(), before);
}

TEST(watchy_diagnostics_skips_missing_hardware){
	bootWatchy();
	open("hwtest");
	for(Button b : { Button::Up, Button::Down, Button::Select, Button::Alt }) press(b);
	run(600);
	press(Button::Select); // display
	press(Button::Select); // vibration; no backlight or LED stages
	run(3500);             // sensors
	press(Button::Select); // to the summary
	run(300);
	// From the summary, SELECT restarts the test; after the Buttons stage
	// BACK on the Display stage leaves the app. Had a stage not been skipped,
	// we would still be in the Sensors stage, which swallows every key.
	press(Button::Select);
	for(Button b : { Button::Up, Button::Down, Button::Select, Button::Alt }) press(b);
	run(600);
	press(Button::Alt);
	run(400);
	CHECK_STR(os::currentId(), "launcher");
}

// ---- Runner --------------------------------------------------------------------------------

int main(int argc, char** argv){
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int passed = 0, failed = 0;
	std::vector<const char*> failures;

	for(const auto& t : registry()){
		if(filter && !strstr(t.name, filter)) continue;
		fflush(stdout);
		const pid_t pid = fork();
		if(pid == 0){
			alarm(120); // a hung test is a failed test
			t.fn();
			fflush(stdout);
			_exit(0);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if(!ok && WIFSIGNALED(status)) printf("    crashed: %s\n", strsignal(WTERMSIG(status)));
		printf("%s %s\n", ok ? "  ok  " : "  FAIL", t.name);
		if(ok) passed++;
		else{
			failed++;
			failures.push_back(t.name);
		}
	}

	printf("\n%d passed, %d failed\n", passed, failed);
	for(auto* f : failures) printf("  failed: %s\n", f);
	return failed ? 1 : 0;
}
