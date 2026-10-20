// Diagnostics: a guided hardware self-test for a freshly flashed watch.
// Automatic checks (RTC, IMU, battery) are scored by the app; things only a
// person can judge (colours, sound, LEDs) ask "SELECT = OK, DOWN = problem".
#include "Apps.h"
#include <cmath>
#include <cstdio>

using namespace ui;

namespace {

enum Stage { Buttons, Display, Backlight, Buzzer, Leds, Sensors, Summary, StageCount };

// Stages for hardware this watch doesn't have are skipped.
bool applicable(int s){
	const auto& f = hal::features();
	if(s == Backlight) return f.backlight;
	if(s == Leds) return f.rgbLed;
	return s < Summary;
}

const char* stageName(int s){
	static const char* const names[] = { "Buttons", "Display", "Backlight", "Buzzer", "LEDs", "Sensors" };
	if(s == Buzzer && !hal::features().buzzer) return "Vibration";
	return names[s];
}

int applicableCount(){
	int n = 0;
	for(int s = 0; s < Summary; s++) n += applicable(s);
	return n;
}

enum class Result : uint8_t { Untested, Pass, Fail };

class HwTest : public os::Activity {
public:
	void onCreate(const os::Intent&) override { enter(Buttons); }

	void onDestroy() override { restore(); }

	bool showStatusBar() const override { return stage == Summary; }
	bool keepScreenOn() const override { return stage != Summary; }
	int8_t brightnessOverride() const override {
		if(stage != Backlight) return -1;
		const float t = (hal::millis() - stageStart) / 2500.0f;
		return (int8_t) (10 + 90 * (0.5f - 0.5f * cosf(t * 2 * (float) M_PI)));
	}
	uint16_t background() const override {
		if(stage != Display) return col::Bg;
		if(mono()) return ((hal::millis() - stageStart) / 700) % 2 ? col::Text : col::Bg;
		static const uint16_t colors[] = { rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), rgb(255, 255, 255), rgb(0, 0, 0) };
		return colors[((hal::millis() - stageStart) / 700) % 5];
	}

	void onUpdate(uint32_t) override {
		const uint32_t t = hal::millis() - stageStart;
		switch(stage){
			case Buttons:
				for(int i = 0; i < 4; i++) seen[i] |= hal::button((hal::Button) i);
				if(anyHeld()) lastHeld = hal::millis();
				// Wait until everything is released, so the last button's
				// click is delivered here and not to the next stage.
				if(seen[0] && seen[1] && seen[2] && seen[3] && hal::millis() - lastHeld > 300) finishStage(Result::Pass);
				else if(t > 20000) finishStage(Result::Fail);
				break;
			case Buzzer: {
				if(!hal::features().buzzer){
					// Vibration motor: pulse it
					hal::tone(t % 600 < 300 ? 1 : 0);
					break;
				}
				// Rising sweep, repeating every 1.5 s
				const uint32_t p = t % 1500;
				hal::tone(p < 1200 ? (uint16_t) (400 + p * 2) : 0);
				break;
			}
			case Leds: {
				const int phase = (t / 300) % (3 + hal::ledCount());
				hal::rgb(phase == 0, phase == 1, phase == 2);
				for(int i = 0; i < hal::ledCount(); i++) hal::led(i, phase == 3 + i);
				break;
			}
			case Sensors:
				sample();
				if(t > 3000 && !sensorsScored) scoreSensors();
				break;
			default:
				break;
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		if(stage == Buttons) return true; // every press is part of the test
		if(e.action == os::KeyAction::LongPress && e.key == os::Key::Select){
			os::finish();
			return true;
		}
		if(e.action != os::KeyAction::Click) return true;

		if(stage == Summary){
			if(e.key == os::Key::Select) enter(Buttons);
			else if(e.key == os::Key::Back) os::finish();
			return true;
		}
		if(stage == Sensors){
			if(sensorsScored && e.key == os::Key::Select) enter(Summary);
			return true;
		}
		if(e.key == os::Key::Select) finishStage(Result::Pass);
		else if(e.key == os::Key::Down) finishStage(Result::Fail);
		else if(e.key == os::Key::Back) os::finish();
		return true;
	}

	void onDraw(os::Canvas& c) override {
		switch(stage){
			case Buttons: drawButtons(c); break;
			case Display: drawDisplay(c); break;
			case Backlight:
				title(c, "Backlight");
				icon::sun(c, 64, 52, col::Yellow);
				text(c, "Brightness should", 64, 76, Small, col::Text, textdatum_t::middle_center);
				text(c, "fade up and down", 64, 88, Small, col::Text, textdatum_t::middle_center);
				ask(c);
				break;
			case Buzzer: {
				const bool buzzer = hal::features().buzzer;
				title(c, stageName(Buzzer));
				icon::sound(c, 64, 52, col::Orange);
				text(c, buzzer ? "You should hear" : "You should feel", 64, 76, Small, col::Text, textdatum_t::middle_center);
				text(c, buzzer ? "a rising tone" : "it pulse", 64, 88, Small, col::Text, textdatum_t::middle_center);
				ask(c);
				break;
			}
			case Leds:
				title(c, "LEDs");
				text(c, "RGB: red, green, blue", 64, 52, Small, col::Text, textdatum_t::middle_center);
				text(c, "then each small LED", 64, 64, Small, col::Text, textdatum_t::middle_center);
				text(c, "lights in turn", 64, 76, Small, col::Text, textdatum_t::middle_center);
				ask(c);
				break;
			case Sensors: drawSensors(c); break;
			case Summary: drawSummary(c); break;
			default: break;
		}
	}

private:
	Stage stage = Buttons;
	uint32_t stageStart = 0;
	Result results[Summary] = {};
	bool seen[4] = {};
	uint32_t lastHeld = 0;

	// Sensors
	time_t firstTime = 0;
	hal::Accel accel{};
	float accelMag = 0;
	hal::Battery batt{};
	bool sensorsScored = false;
	bool rtcOk = false, imuOk = false, battOk = false;

	static bool anyHeld(){
		for(int i = 0; i < 4; i++) if(hal::button((hal::Button) i)) return true;
		return false;
	}

	void restore(){
		hal::tone(0);
		hal::rgb(false, false, false);
		for(int i = 0; i < hal::ledCount(); i++) hal::led(i, false);
	}

	void enter(Stage s){
		restore();
		stage = s;
		stageStart = hal::millis();
		if(s == Buttons){
			for(auto& r : results) r = Result::Untested;
			for(auto& b : seen) b = false;
			lastHeld = hal::millis();
		}
		if(s == Sensors){
			sensorsScored = false;
			firstTime = hal::now();
		}
		if(s == Summary){
			bool ok = true;
			for(int i = 0; i < Summary; i++) ok &= !applicable(i) || results[i] == Result::Pass;
			os::play(ok ? os::Sound::Success : os::Sound::Error);
		}
	}

	void finishStage(Result r){
		results[stage] = r;
		os::play(os::Sound::Click);
		int next = stage + 1;
		while(next < Summary && !applicable(next)) next++;
		enter((Stage) next);
	}

	void sample(){
		accel = hal::accel();
		accelMag = sqrtf(accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);
		batt = hal::battery();
	}

	void scoreSensors(){
		sensorsScored = true;
		rtcOk = hal::timeValid() && hal::now() > firstTime;
		// At rest the accelerometer should read about 1 g.
		imuOk = hal::imuPresent() && accelMag > 0.7f && accelMag < 1.3f;
		battOk = batt.millivolts > 3000 && batt.millivolts < 4500;
		results[Sensors] = rtcOk && imuOk && battOk ? Result::Pass : Result::Fail;
	}

	void title(os::Canvas& c, const char* s){
		int index = 1;
		for(int i = 0; i < stage; i++) index += applicable(i);
		char buf[32];
		snprintf(buf, sizeof(buf), "%d/%d  %s", index, applicableCount(), s);
		text(c, buf, 64, 6, Body, accent().primary, textdatum_t::top_center);
	}

	void ask(os::Canvas& c){
		c.fillSmoothRoundRect(8, 104, 54, 18, 9, col::Mint);
		text(c, "SEL ok", 35, 113, Small, col::Bg, textdatum_t::middle_center);
		c.fillSmoothRoundRect(66, 104, 54, 18, 9, col::Red);
		text(c, "DN bad", 93, 113, Small, col::Bg, textdatum_t::middle_center);
	}

	void drawButtons(os::Canvas& c){
		title(c, "Buttons");
		text(c, "Press every button", 64, 26, Small, col::TextDim, textdatum_t::top_center);
		const char* names[] = { "UP", "DOWN", "SELECT", "BACK" };
		for(int i = 0; i < 4; i++){
			const int x = 8 + (i % 2) * 58, y = 44 + (i / 2) * 36;
			const bool held = hal::button((hal::Button) i);
			const uint16_t fill = held ? accent().primary : (seen[i] ? accent().container : col::Surface2);
			card(c, x, y, 54, 30, fill, 10);
			text(c, names[i], x + 27, seen[i] ? y + 10 : y + 15, Small, held ? accent().container : col::Text, textdatum_t::middle_center);
			if(seen[i] && !held) icon::check(c, x + 27, y + 21, accent().primary);
		}
		const uint32_t left = 20 - (hal::millis() - stageStart) / 1000;
		char buf[16];
		snprintf(buf, sizeof(buf), "%lus", (unsigned long) left);
		text(c, buf, 64, 124, Small, col::TextFaint, textdatum_t::bottom_center);
	}

	void drawDisplay(os::Canvas& c){
		const uint32_t phase = ((hal::millis() - stageStart) / 700) % 5;
		const uint16_t fg = mono() ? (phase % 2 ? col::Bg : col::Text) : (phase == 3 ? col::Bg : col::Text);
		if(mono()){
			// Checkerboard bands catch stuck e-paper pixels.
			if(phase == 4){
				for(int y = 20; y < 100; y++){
					for(int x = (y & 1); x < os::W; x += 2) c.drawPixel(x, y, col::Text);
				}
			}
		}else if(phase == 4){
			// Gradient and a 1 px grid catch dead pixels and wrong offsets.
			for(int x = 0; x < os::W; x++){
				c.drawFastVLine(x, 0, 40, blend(col::Bg, col::Red, x * 2));
				c.drawFastVLine(x, 40, 40, blend(col::Bg, col::Mint, x * 2));
				c.drawFastVLine(x, 80, 48, blend(col::Bg, col::Blue, x * 2));
			}
			c.drawRect(0, 0, os::W, os::H, col::Text);
		}
		char buf[24];
		snprintf(buf, sizeof(buf), "2/%d  Display", applicableCount());
		text(c, buf, 64, 6, Body, fg, textdatum_t::top_center);
		text(c, mono() ? "Clean black & white," : "Even colours and", 64, 56, Small, fg, textdatum_t::middle_center);
		text(c, mono() ? "no stuck pixels" : "no dead pixels", 64, 68, Small, fg, textdatum_t::middle_center);
		ask(c);
	}

	void drawSensors(os::Canvas& c){
		title(c, "Sensors");
		char buf[40];
		const auto t = os::localTime();
		auto row = [&](int y, const char* label, const char* value, int state){
			text(c, label, 6, y, Small, col::TextDim);
			text(c, value, 122, y, Small, state < 0 ? col::Text : (state ? col::Mint : col::Red), textdatum_t::top_right);
		};
		const int sRtc = sensorsScored ? rtcOk : -1;
		const int sImu = sensorsScored ? imuOk : -1;
		const int sBat = sensorsScored ? battOk : -1;
		snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.hour, t.minute, t.second);
		row(26, "RTC", buf, sRtc);
		row(38, "RTC power", hal::timeValid() ? "ok" : "lost", sRtc);
		snprintf(buf, sizeof(buf), "%.1f %.1f %.1f", accel.x, accel.y, accel.z);
		row(52, "Accel", hal::imuPresent() ? buf : "not found", sImu);
		snprintf(buf, sizeof(buf), "%.2f g", accelMag);
		row(64, "|a|", buf, sImu);
		snprintf(buf, sizeof(buf), "%lu", (unsigned long) hal::stepCounter());
		row(76, "Steps", buf, -1);
		snprintf(buf, sizeof(buf), "%umV %u%%", batt.millivolts, batt.percent);
		row(90, "Batt", buf, sBat);
		row(102, "USB", !hal::features().usbDetect ? "n/a" : (batt.usb ? (batt.charging ? "charging" : "full") : "unplugged"), -1);
		text(c, sensorsScored ? "SELECT for results" : "Keep the watch still...", 64, 124, Small, col::TextFaint, textdatum_t::bottom_center);
	}

	void drawSummary(os::Canvas& c){
		int passed = 0;
		for(int i = 0; i < Summary; i++) passed += applicable(i) && results[i] == Result::Pass;
		const bool ok = passed == applicableCount();
		char buf[24];
		snprintf(buf, sizeof(buf), "%d/%d passed", passed, applicableCount());
		text(c, buf, 64, 15, Body, ok ? col::Mint : col::Red, textdatum_t::top_center);
		int row = 0;
		for(int i = 0; i < Summary; i++){
			if(!applicable(i)) continue;
			const int y = 33 + row++ * 13;
			text(c, stageName(i), 14, y, Small, col::Text);
			const bool pass = results[i] == Result::Pass;
			text(c, pass ? "PASS" : "FAIL", 114, y, Small, pass ? col::Mint : col::Red, textdatum_t::top_right);
		}
		hint(c, "SEL retest  BACK exit");
	}
};

}

const os::AppInfo apps::HwTest = { "hwtest", "Diagnostics", col::Teal, icon::check, os::make<::HwTest>, false };
