// Fit (step counter) and Heart rate.
#include "Apps.h"
#include <cmath>
#include <cstdio>

using namespace ui;

namespace {

// ---- Fit -------------------------------------------------------------------------
// Steps come from the LSM6DS3TR-C's built-in hardware pedometer, so counting
// carries on while the watch is asleep.

class Fit : public os::Activity {
public:
	void onUpdate(uint32_t dt) override {
		shown = approach(shown, (float) os::stepsToday(), dt, 6);
		pageAnim = approach(pageAnim, (float) page, dt, 16);
	}

	bool onKey(const os::KeyEvent& e) override {
		if(e.action != os::KeyAction::Down) return false;
		if(e.key == os::Key::Down && page < 1) page++;
		else if(e.key == os::Key::Up && page > 0) page--;
		else return false;
		return true;
	}

	void onDraw(os::Canvas& c) override {
		const int off = (int) (pageAnim * os::H);
		drawToday(c, -off);
		drawWeek(c, os::H - off);
	}

private:
	int page = 0;
	float pageAnim = 0;
	float shown = 0;

	void drawToday(os::Canvas& c, int y0){
		if(y0 <= -os::H) return;
		const auto& s = os::settings();
		const auto& a = accent();
		const uint32_t steps = (uint32_t) lroundf(shown);
		const float frac = s.stepGoal ? steps / (float) s.stepGoal : 0;
		ring(c, 64, y0 + 60, 40, 7, fminf(frac, 1.0f), a.primary, col::Surface2);
		if(frac > 1) ring(c, 64, y0 + 60, 40, 7, frac - 1, col::Yellow, a.primary);
		icon::steps(c, 64, y0 + 38, a.primary);

		char buf[24];
		snprintf(buf, sizeof(buf), "%lu", (unsigned long) steps);
		text(c, buf, 64, y0 + 60, Title, col::Text, textdatum_t::middle_center);
		snprintf(buf, sizeof(buf), "of %u", s.stepGoal);
		text(c, buf, 64, y0 + 77, Small, col::TextDim, textdatum_t::middle_center);

		const float km = steps * 0.00075f;
		const int kcal = (int) (steps * 0.04f);
		snprintf(buf, sizeof(buf), "%.2f km", km);
		text(c, buf, 32, y0 + 112, Small, col::Teal, textdatum_t::middle_center);
		snprintf(buf, sizeof(buf), "%d kcal", kcal);
		text(c, buf, 96, y0 + 112, Small, col::Orange, textdatum_t::middle_center);
		if(!hal::imuPresent()) text(c, "No motion sensor", 64, y0 + 98, Small, col::Red, textdatum_t::middle_center);
		c.fillTriangle(60, y0 + 122, 68, y0 + 122, 64, y0 + 126, col::TextFaint);
	}

	void drawWeek(os::Canvas& c, int y0){
		if(y0 >= os::H) return;
		const auto& s = os::settings();
		const auto& a = accent();
		text(c, "Last 7 days", 64, y0 + 16, Body, a.primary, textdatum_t::top_center);

		uint32_t vals[7];
		uint32_t max = s.stepGoal;
		uint32_t total = 0;
		for(int i = 0; i < 7; i++){
			vals[i] = os::stepsOnDay(6 - i);
			if(vals[i] > max) max = vals[i];
			total += vals[i];
		}
		const int base = y0 + 100, top = y0 + 36;
		const int goalY = base - (int) ((base - top) * (s.stepGoal / (float) max));
		for(int x = 8; x < 120; x += 4) c.drawFastHLine(x, goalY, 2, col::TextFaint);

		const auto today = os::localTime();
		for(int i = 0; i < 7; i++){
			int h = (int) ((base - top) * (vals[i] / (float) max));
			if(vals[i] > 0 && h < 2) h = 2;
			const int x = 10 + i * 16;
			const bool hit = vals[i] >= s.stepGoal;
			if(h > 0) c.fillSmoothRoundRect(x, base - h, 11, h, 3, i == 6 ? a.primary : (hit ? a.container : col::Surface2));
			const char d[2] = { os::weekdayName((today.weekday + 7 - (6 - i)) % 7)[0], 0 };
			text(c, d, x + 5, base + 3, Small, i == 6 ? col::Text : col::TextFaint, textdatum_t::top_center);
		}
		char buf[24];
		snprintf(buf, sizeof(buf), "avg %lu / day", (unsigned long) (total / 7));
		text(c, buf, 64, y0 + 126, Small, col::TextDim, textdatum_t::bottom_center);
	}
};

// ---- Heart rate ------------------------------------------------------------------
// The Clockstar has no optical heart-rate sensor, so this measures your pulse
// the old-fashioned way: feel it on your wrist or neck and tap along.

class Heart : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		for(int i = 0; i < 5; i++){
			char key[4] = { 'h', 'r', (char) ('0' + i), 0 };
			history[i] = hal::prefGet(key, 0);
		}
	}

	bool keepScreenOn() const override { return state == State::Measuring; }

	void onUpdate(uint32_t dt) override {
		beatAnim = approach(beatAnim, 0, dt, 7);
		if(state == State::Measuring && taps > 0 && hal::millis() - lastTap > 3000){
			if(taps >= 5) finishMeasurement();
			else{
				state = State::Intro;
				os::toast("Too few taps");
			}
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		if(e.action == os::KeyAction::Down && e.key == os::Key::Select){
			if(state == State::Intro || state == State::Result){
				state = State::Measuring;
				taps = 0;
				bpm = 0;
			}
			tap();
			return true;
		}
		if(state == State::Measuring && e.action == os::KeyAction::Click && e.key == os::Key::Back){
			state = State::Intro;
			return true;
		}
		return e.key == os::Key::Select;
	}

	void onDraw(os::Canvas& c) override {
		const float pulse = 1 + beatAnim * 0.35f;
		const int cy = 46;
		// Scaled heart
		const int r = (int) (9 * pulse);
		c.fillSmoothCircle(64 - r * 2 / 3, cy - r / 3, r, col::Red);
		c.fillSmoothCircle(64 + r * 2 / 3, cy - r / 3, r, col::Red);
		c.fillTriangle(64 - r * 5 / 3, cy, 64 + r * 5 / 3, cy, 64, cy + r * 5 / 3, col::Red);
		c.fillRect(64 - r * 4 / 3, cy - r / 3, r * 8 / 3, r / 3 + 1, col::Red);

		drawTrace(c);

		char buf[32];
		switch(state){
			case State::Intro:
				text(c, "Find your pulse", 64, 86, Body, col::Text, textdatum_t::middle_center);
				text(c, "then tap SELECT on", 64, 101, Small, col::TextDim, textdatum_t::middle_center);
				text(c, "every beat", 64, 112, Small, col::TextDim, textdatum_t::middle_center);
				if(history[0]){
					snprintf(buf, sizeof(buf), "Last: %ld bpm", (long) history[0]);
					text(c, buf, 64, 18, Small, col::TextFaint, textdatum_t::top_center);
				}
				break;
			case State::Measuring:
				if(taps >= 3){
					snprintf(buf, sizeof(buf), "%d", liveBpm());
					text(c, buf, 64, 88, Title, col::Text, textdatum_t::middle_center);
				}else{
					text(c, "Keep tapping...", 64, 88, Body, col::Text, textdatum_t::middle_center);
				}
				snprintf(buf, sizeof(buf), "%d / %d beats", taps, MaxTaps);
				text(c, buf, 64, 108, Small, col::TextDim, textdatum_t::middle_center);
				break;
			case State::Result: {
				snprintf(buf, sizeof(buf), "%d", bpm);
				text(c, buf, 56, 88, &fonts::DejaVu24, col::Text, textdatum_t::middle_right);
				text(c, "bpm", 60, 92, Small, col::TextDim, textdatum_t::middle_left);
				const char* zone = bpm < 60 ? "Low" : (bpm <= 100 ? "Normal resting" : "Elevated");
				const uint16_t zc = bpm < 60 ? col::Blue : (bpm <= 100 ? col::AndroidGreen : col::Orange);
				text(c, zone, 64, 108, Small, zc, textdatum_t::middle_center);
				drawHistory(c);
				break;
			}
		}
	}

private:
	enum class State : uint8_t { Intro, Measuring, Result };
	static constexpr int MaxTaps = 12;

	State state = State::Intro;
	uint32_t tapTimes[MaxTaps];
	int taps = 0;
	uint32_t lastTap = 0;
	int bpm = 0;
	float beatAnim = 0;
	int32_t history[5] = {};

	void tap(){
		const uint32_t now = hal::millis();
		if(taps > 0 && now - lastTap < 250) return; // >240 bpm is a double tap
		lastTap = now;
		tapTimes[taps++] = now;
		beatAnim = 1;
		os::beep(880, 15);
		if(taps >= MaxTaps) finishMeasurement();
	}

	int liveBpm() const {
		if(taps < 2) return 0;
		const int first = taps > 6 ? taps - 6 : 0;
		const float avg = (tapTimes[taps - 1] - tapTimes[first]) / (float) (taps - 1 - first);
		return (int) lroundf(60000.0f / avg);
	}

	void finishMeasurement(){
		bpm = liveBpm();
		state = State::Result;
		for(int i = 4; i > 0; i--) history[i] = history[i - 1];
		history[0] = bpm;
		for(int i = 0; i < 5; i++){
			char key[4] = { 'h', 'r', (char) ('0' + i), 0 };
			hal::prefSet(key, history[i]);
		}
		os::play(os::Sound::Success);
	}

	void drawTrace(os::Canvas& c){
		// ECG-style line that spikes with each tap
		const int y = 70;
		const uint32_t now = hal::millis();
		int px = 0, py = y;
		for(int x = 0; x < os::W; x += 2){
			const uint32_t t = now - (os::W - x) * 12;
			int v = 0;
			for(int i = 0; i < taps; i++){
				const int dt = (int) (t - tapTimes[i]);
				if(dt >= 0 && dt < 60) v = dt < 20 ? -dt / 2 : (dt < 40 ? (dt - 30) : (60 - dt) / 4);
			}
			const int ny = y + v;
			if(x > 0) c.drawLine(px, py, x, ny, blend(col::Bg, col::Red, 140));
			px = x;
			py = ny;
		}
	}

	void drawHistory(os::Canvas& c){
		int x = 34;
		for(int i = 4; i >= 0; i--){
			if(!history[i]) continue;
			const int h = history[i] / 10;
			c.fillRect(x, 26 - (h > 14 ? 14 : h), 4, h > 14 ? 14 : h, i == 0 ? col::Red : col::Surface2);
			x += 6;
		}
	}
};

}

const os::AppInfo apps::Fit = { "fit", "Fit", col::AndroidGreen, icon::steps, os::make<::Fit>, true };
const os::AppInfo apps::Heart = { "heart", "Heart rate", col::Red, icon::heart, os::make<::Heart>, true };
