// Stopwatch, Timer, Alarm and the full-screen "ringing" activity.
#include "Apps.h"
#include <cmath>
#include <cstdio>

using namespace ui;

namespace {

// ---- Background state --------------------------------------------------------------
// Lives outside the activities so it survives the screen turning off.

struct StopwatchState {
	bool running = false;
	uint32_t startedAt = 0;
	uint32_t accumulated = 0;
	uint32_t laps[6] = {};
	int lapCount = 0;

	uint32_t elapsed() const { return accumulated + (running ? hal::millis() - startedAt : 0); }
} stopwatch;

class TimerService : public os::Service {
public:
	bool running = false;
	bool paused = false;
	uint32_t endsAt = 0;
	uint32_t remainingWhenPaused = 0;
	uint32_t duration = 5 * 60 * 1000;

	uint32_t remaining() const {
		if(paused) return remainingWhenPaused;
		if(!running) return duration;
		const uint32_t now = hal::millis();
		return now >= endsAt ? 0 : endsAt - now;
	}

	void poll() override {
		if(running && !paused && hal::millis() >= endsAt){
			running = false;
			os::wakeScreen();
			os::notify("timer", "Timer", "Time's up!", false);
			os::startActivity({ "ringing", 1 });
		}
	}

	uint32_t msUntilNext() override {
		return running && !paused ? remaining() : UINT32_MAX;
	}
} timerService;

class AlarmService : public os::Service {
public:
	int hour = 7, minute = 0;
	bool enabled = false;
	int32_t firedKey = -1;
	uint32_t snoozeUntil = 0;
	bool snoozed = false;

	void load(){
		hour = hal::prefGet("alH", 7);
		minute = hal::prefGet("alM", 0);
		enabled = hal::prefGet("alOn", 0);
	}

	void save(){
		hal::prefSet("alH", hour);
		hal::prefSet("alM", minute);
		hal::prefSet("alOn", enabled);
	}

	void poll() override {
		if(snoozed && hal::millis() >= snoozeUntil){
			snoozed = false;
			ring();
			return;
		}
		if(!enabled) return;
		const auto t = os::localTime();
		const int32_t key = t.year * 10000 + t.month * 100 + t.day;
		if(t.hour == hour && t.minute == minute && firedKey != key){
			firedKey = key;
			ring();
		}
	}

	uint32_t msUntilNext() override {
		if(snoozed) return snoozeUntil > hal::millis() ? snoozeUntil - hal::millis() : 0;
		if(!enabled) return UINT32_MAX;
		const auto t = os::localTime();
		int nowSec = t.hour * 3600 + t.minute * 60 + t.second;
		int alarmSec = hour * 3600 + minute * 60;
		int diff = alarmSec - nowSec;
		if(diff < 0) diff += 86400;
		return diff * 1000u;
	}

	void ring(){
		os::wakeScreen();
		os::startActivity({ "ringing", 0 });
	}
} alarmService;

void formatDuration(char* buf, size_t len, uint32_t ms, bool centis){
	const uint32_t totalSec = ms / 1000;
	const uint32_t h = totalSec / 3600, m = (totalSec / 60) % 60, s = totalSec % 60;
	if(h > 0) snprintf(buf, len, "%lu:%02lu:%02lu", (unsigned long) h, (unsigned long) m, (unsigned long) s);
	else if(centis) snprintf(buf, len, "%02lu:%02lu.%02lu", (unsigned long) m, (unsigned long) s, (unsigned long) (ms / 10) % 100);
	else snprintf(buf, len, "%02lu:%02lu", (unsigned long) m, (unsigned long) s);
}

// ---- Stopwatch ---------------------------------------------------------------------

class Stopwatch : public os::Activity {
public:
	bool keepScreenOn() const override { return stopwatch.running; }

	bool onKey(const os::KeyEvent& e) override {
		if(e.action != os::KeyAction::Click) return false;
		auto& s = stopwatch;
		if(e.key == os::Key::Select){
			if(s.running){
				s.accumulated = s.elapsed();
				s.running = false;
			}else{
				s.startedAt = hal::millis();
				s.running = true;
			}
			os::play(os::Sound::Click);
			return true;
		}
		if(e.key == os::Key::Down){
			if(s.running){
				for(int i = 5; i > 0; i--) s.laps[i] = s.laps[i - 1];
				s.laps[0] = s.elapsed();
				if(s.lapCount < 99) s.lapCount++;
			}else{
				s = StopwatchState();
			}
			os::play(os::Sound::Click);
			return true;
		}
		return false;
	}

	void onDraw(os::Canvas& c) override {
		const auto& s = stopwatch;
		const auto& a = accent();
		const uint32_t ms = s.elapsed();

		// Sweeping seconds ring
		const float frac = (ms % 60000) / 60000.0f;
		c.drawCircle(64, 52, 36, col::Surface2);
		const float ang = frac * 2 * (float) M_PI - (float) M_PI / 2;
		c.fillArc(64, 52, 34, 37, -90, -90 + frac * 360, s.running ? a.primary : col::TextDim);
		c.fillSmoothCircle(64 + (int) (cosf(ang) * 35.5f), 52 + (int) (sinf(ang) * 35.5f), 3, s.running ? a.primary : col::TextDim);

		char buf[16];
		formatDuration(buf, sizeof(buf), ms, true);
		text(c, buf, 64, 52, Title, col::Text, textdatum_t::middle_center);
		text(c, s.running ? "SEL pause   DN lap" : (ms ? "SEL resume   DN reset" : "SELECT start"), 64, 124, Small, col::TextFaint, textdatum_t::bottom_center);

		for(int i = 0; i < 2 && i < s.lapCount; i++){
			const uint32_t lap = s.laps[i] - (i + 1 < s.lapCount ? s.laps[i + 1] : 0);
			char lb[24], tb[16];
			formatDuration(tb, sizeof(tb), lap, true);
			snprintf(lb, sizeof(lb), "Lap %d   %s", s.lapCount - i, tb);
			text(c, lb, 64, 96 + i * 11, Small, i == 0 ? col::Text : col::TextDim, textdatum_t::top_center);
		}
	}
};

// ---- Timer -------------------------------------------------------------------------

class Timer : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		const uint32_t sec = timerService.duration / 1000;
		picker.set({ { (int) (sec / 60), 0, 99, ":" }, { (int) (sec % 60), 0, 59, nullptr } });
		editing = !timerService.running;
	}

	bool onKey(const os::KeyEvent& e) override {
		auto& t = timerService;
		if(editing){
			bool cancelled;
			if(picker.onKey(e, cancelled)){
				const uint32_t ms = (picker.value(0) * 60 + picker.value(1)) * 1000u;
				if(ms == 0){
					os::play(os::Sound::Error);
					picker.field = 0;
					return true;
				}
				t.duration = ms;
				t.running = true;
				t.paused = false;
				t.endsAt = hal::millis() + ms;
				editing = false;
				os::play(os::Sound::Click);
			}else if(cancelled){
				os::finish();
			}
			return true;
		}
		if(e.action != os::KeyAction::Click) return false;
		if(e.key == os::Key::Select){
			if(t.paused){
				t.paused = false;
				t.endsAt = hal::millis() + t.remainingWhenPaused;
			}else{
				t.remainingWhenPaused = t.remaining();
				t.paused = true;
			}
			os::play(os::Sound::Click);
			return true;
		}
		if(e.key == os::Key::Down){
			t.running = false;
			t.paused = false;
			editing = true;
			os::toast("Timer cancelled");
			return true;
		}
		return false;
	}

	void onDraw(os::Canvas& c) override {
		const auto& a = accent();
		if(editing){
			header(c, "Timer");
			picker.draw(c, 62, &fonts::DejaVu24);
			text(c, "min       sec", 64, 84, Small, col::TextFaint, textdatum_t::top_center);
			hint(c, "SELECT start");
			return;
		}
		const auto& t = timerService;
		const uint32_t rem = t.remaining();
		ring(c, 64, 60, 44, 6, rem / (float) t.duration, t.paused ? col::TextDim : a.primary, col::Surface2);
		char buf[16];
		formatDuration(buf, sizeof(buf), rem + 999, false);
		text(c, buf, 64, 58, &fonts::DejaVu24, col::Text, textdatum_t::middle_center);
		text(c, t.paused ? "Paused" : "", 64, 80, Small, col::TextDim, textdatum_t::middle_center);
		hint(c, t.paused ? "SEL resume   DN stop" : "SEL pause   DN stop");
	}

private:
	Picker picker;
	bool editing = true;
};

// ---- Alarm -------------------------------------------------------------------------

class Alarm : public os::Activity {
public:
	bool onKey(const os::KeyEvent& e) override {
		auto& al = alarmService;
		if(editing){
			bool cancelled;
			if(picker.onKey(e, cancelled)){
				al.hour = picker.value(0);
				al.minute = picker.value(1);
				al.enabled = true;
				al.save();
				editing = false;
				char buf[32];
				const uint32_t mins = al.msUntilNext() / 60000;
				snprintf(buf, sizeof(buf), "Alarm in %luh %lum", (unsigned long) mins / 60, (unsigned long) mins % 60);
				os::toast(buf);
			}else if(cancelled) editing = false;
			return true;
		}
		if(e.action != os::KeyAction::Click) return false;
		if(e.key == os::Key::Select){
			al.enabled = !al.enabled;
			al.save();
			return true;
		}
		if(e.key == os::Key::Down || e.key == os::Key::Up){
			picker.set({ { al.hour, 0, 23, ":" }, { al.minute, 0, 59, nullptr } });
			editing = true;
			return true;
		}
		return false;
	}

	void onDraw(os::Canvas& c) override {
		const auto& al = alarmService;
		if(editing){
			header(c, "Alarm time");
			picker.draw(c, 64, &fonts::DejaVu24);
			hint(c, "SELECT save");
			return;
		}
		const auto& a = accent();
		card(c, 8, 26, 112, 64, al.enabled ? a.container : col::Surface, 16);
		icon::alarm(c, 26, 44, al.enabled ? a.primary : col::TextDim);
		char buf[8];
		snprintf(buf, sizeof(buf), "%02d:%02d", al.hour, al.minute);
		text(c, buf, 64, 66, &fonts::DejaVu24, al.enabled ? col::Text : col::TextDim, textdatum_t::middle_center);
		toggle(c, 90, 38, al.enabled);
		text(c, al.enabled ? "Every day" : "Off", 64, 82, Small, al.enabled ? a.primary : col::TextFaint, textdatum_t::middle_center);
		hint(c, "SEL on/off   DN edit");
	}

private:
	Picker picker;
	bool editing = false;
};

// ---- Ringing ------------------------------------------------------------------------

class Ringing : public os::Activity {
public:
	void onCreate(const os::Intent& i) override {
		isTimer = i.extra == 1;
		start = hal::millis();
	}

	bool showStatusBar() const override { return false; }
	bool keepScreenOn() const override { return true; }
	int8_t brightnessOverride() const override { return 100; }

	void onUpdate(uint32_t) override {
		const uint32_t t = hal::millis() - start;
		if(t / 1000 != lastBeep){
			lastBeep = t / 1000;
			os::play(os::Sound::Alarm);
		}
		const bool flash = (t / 250) % 2;
		hal::rgb(flash, flash && isTimer, !isTimer && flash);
		if(t > 60000) dismiss();
	}

	void onDestroy() override { hal::rgb(false, false, false); }

	bool onKey(const os::KeyEvent& e) override {
		if(e.action != os::KeyAction::Click) return true;
		if(e.key == os::Key::Down && !isTimer){
			alarmService.snoozed = true;
			alarmService.snoozeUntil = hal::millis() + 5 * 60000;
			os::toast("Snoozed 5 min");
			os::finish();
		}else{
			dismiss();
		}
		return true;
	}

	void onDraw(os::Canvas& c) override {
		const uint32_t t = hal::millis() - start;
		const auto& a = accent();
		const float pulse = (sinf(t / 120.0f) + 1) / 2;
		c.fillSmoothCircle(64, 52, 26 + (int) (pulse * 8), a.container);
		c.fillSmoothCircle(64, 52, 24, a.primary);
		const int wobble = (int) (sinf(t / 40.0f) * 2);
		if(isTimer) icon::timer(c, 64 + wobble, 52, a.container);
		else icon::alarm(c, 64 + wobble, 52, a.container);

		const auto now = os::localTime();
		char buf[16];
		snprintf(buf, sizeof(buf), "%02d:%02d", now.hour, now.minute);
		text(c, isTimer ? "Time's up" : buf, 64, 94, Title, col::Text, textdatum_t::middle_center);
		hint(c, isTimer ? "Any button to stop" : "SEL stop   DN snooze");
	}

private:
	bool isTimer = false;
	uint32_t start = 0;
	uint32_t lastBeep = UINT32_MAX;

	void dismiss(){
		os::finish();
	}
};

}

void apps::startTimerService(){
	os::addService(&timerService);
}

void apps::startAlarmService(){
	alarmService.load();
	os::addService(&alarmService);
}

const os::AppInfo apps::Stopwatch = { "stopwatch", "Stopwatch", col::Teal, icon::stopwatch, os::make<::Stopwatch>, true };
const os::AppInfo apps::Timer = { "timer", "Timer", col::Orange, icon::timer, os::make<::Timer>, true };
const os::AppInfo apps::Alarm = { "alarm", "Alarm", col::Purple, icon::alarm, os::make<::Alarm>, true };
const os::AppInfo apps::Ringing = { "ringing", "Alarm", col::Purple, icon::alarm, os::make<::Ringing>, false };
