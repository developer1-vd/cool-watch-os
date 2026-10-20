// Flashlight, bubble level and calendar.
#include "Apps.h"
#include <cmath>
#include <cstdio>

using namespace ui;

namespace {

class Flashlight : public os::Activity {
public:
	bool showStatusBar() const override { return false; }
	bool keepScreenOn() const override { return true; }
	int8_t brightnessOverride() const override { return 100; }
	uint16_t background() const override { return Colors[mode]; }

	bool onKey(const os::KeyEvent& e) override {
		if(e.action == os::KeyAction::Down && (e.key == os::Key::Up || e.key == os::Key::Down || e.key == os::Key::Select)){
			mode = (mode + (e.key == os::Key::Up ? 2 : 1)) % 3;
			return true;
		}
		return false;
	}

	void onDraw(os::Canvas& c) override {
		const char* names[] = { "", "Night light", "Warm" };
		if(mode) text(c, names[mode], 64, 120, Small, col::Bg, textdatum_t::bottom_center);
	}

private:
	static constexpr uint16_t Colors[3] = { rgb(255, 255, 255), rgb(255, 0, 0), rgb(255, 190, 110) };
	int mode = 0;
};

class Level : public os::Activity {
public:
	bool keepScreenOn() const override { return true; }

	void onUpdate(uint32_t dt) override {
		const auto a = hal::accel();
		x = approach(x, a.x, dt, 10);
		y = approach(y, a.y, dt, 10);
	}

	void onDraw(os::Canvas& c) override {
		const auto& acc = accent();
		const int cx = 64, cy = 68, R = 48;
		c.fillSmoothCircle(cx, cy, R, col::Surface);
		c.drawCircle(cx, cy, R / 2, col::Surface2);
		c.drawFastHLine(cx - R, cy, R * 2, col::Surface2);
		c.drawFastVLine(cx, cy - R, R * 2, col::Surface2);

		float bx = x * R * 1.6f, by = y * R * 1.6f;
		const float d = sqrtf(bx * bx + by * by);
		if(d > R - 9){
			bx *= (R - 9) / d;
			by *= (R - 9) / d;
		}
		const float tilt = atan2f(sqrtf(x * x + y * y), 1.0f) * 180 / (float) M_PI;
		const bool level = tilt < 1.5f;
		c.drawCircle(cx, cy, 10, level ? acc.primary : col::TextFaint);
		c.fillSmoothCircle(cx + (int) bx, cy + (int) by, 8, level ? acc.primary : col::Yellow);

		char buf[12];
		snprintf(buf, sizeof(buf), "%.1f deg", tilt);
		text(c, level ? "Level" : buf, 64, 15, Small, level ? acc.primary : col::TextDim, textdatum_t::top_center);
	}

private:
	float x = 0, y = 0;
};

class Calendar : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		const auto t = os::localTime();
		year = t.year;
		month = t.month;
	}

	bool onKey(const os::KeyEvent& e) override {
		if(e.action != os::KeyAction::Down && e.action != os::KeyAction::Repeat) return false;
		if(e.key == os::Key::Up) shift(-1);
		else if(e.key == os::Key::Down) shift(1);
		else if(e.key == os::Key::Select){
			const auto t = os::localTime();
			year = t.year;
			month = t.month;
		}else return false;
		return true;
	}

	void onDraw(os::Canvas& c) override {
		const auto& a = accent();
		char buf[24];
		snprintf(buf, sizeof(buf), "%s %d", os::monthName(month, false), year);
		text(c, buf, 64, 14, Body, a.primary, textdatum_t::top_center);

		const char* heads = "SMTWTFS";
		for(int i = 0; i < 7; i++){
			const char h[2] = { heads[i], 0 };
			text(c, h, 10 + i * 18, 30, Small, col::TextFaint, textdatum_t::top_center);
		}

		os::DateTime first = { year, month, 1, 0, 0, 0, 0 };
		const time_t ft = os::makeTime(first);
		const int startWd = (int) (((ft / 86400) + 4) % 7); // 1970-01-01 was a Thursday
		const int days = daysIn(year, month);
		const auto today = os::localTime();

		for(int d = 1; d <= days; d++){
			const int idx = startWd + d - 1;
			const int x = 10 + (idx % 7) * 18, y = 47 + (idx / 7) * 13;
			snprintf(buf, sizeof(buf), "%d", d);
			const bool isToday = today.year == year && today.month == month && today.day == d;
			if(isToday) c.fillSmoothCircle(x, y + 3, 7, a.primary);
			const bool weekend = idx % 7 == 0 || idx % 7 == 6;
			text(c, buf, x, y + 3, Small, isToday ? a.container : (weekend ? col::TextDim : col::Text), textdatum_t::middle_center);
		}
	}

private:
	int year = 2026, month = 1;

	void shift(int by){
		month += by;
		if(month < 1){ month = 12; year--; }
		if(month > 12){ month = 1; year++; }
	}

	static int daysIn(int y, int m){
		static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
		return m == 2 && leap ? 29 : d[m - 1];
	}
};

}

const os::AppInfo apps::Flashlight = { "flashlight", "Flashlight", col::Yellow, icon::flashlight, os::make<::Flashlight>, true, os::NeedsBacklight };
const os::AppInfo apps::Level = { "level", "Level", col::Teal, icon::level, os::make<::Level>, true };
const os::AppInfo apps::Calendar = { "calendar", "Calendar", col::Blue, icon::calendar, os::make<::Calendar>, true };
