// First-boot setup wizard, in the spirit of Android's "Welcome" flow.
#include "Apps.h"
#include <cmath>
#include <cstdio>

using namespace ui;

namespace {

enum Step { Welcome, Time, Date, Format, Theme, Face, Goal, Tour, Done, StepCount };

class SetupWizard : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		start = hal::millis();
		enter(Welcome);
	}

	bool showStatusBar() const override { return false; }
	bool keepScreenOn() const override { return true; }

	void onUpdate(uint32_t dt) override {
		list.update(dt);
		anim += dt;
		if(step == Done && anim > 2200 && !finished){
			finished = true;
			hal::prefSet("setup", 1);
			os::saveSettings();
			os::replaceActivity({ "launcher" });
			os::notify("settings", "Welcome!", "Your watch is ready. Press UP for quick settings.", false);
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		auto& s = os::settings();
		const bool click = e.action == os::KeyAction::Click;
		bool cancelled = false;

		switch(step){
			case Welcome:
			case Tour:
				if(click && e.key == os::Key::Select) next();
				else if(click && e.key == os::Key::Back && step != Welcome) back();
				return true;

			case Time:
				if(picker.onKey(e, cancelled)){
					auto t = os::localTime();
					t.hour = picker.value(0);
					t.minute = picker.value(1);
					t.second = 0;
					hal::setTime(os::makeTime(t));
					next();
				}else if(cancelled) back();
				return true;

			case Date:
				if(picker.onKey(e, cancelled)){
					auto t = os::localTime();
					t.day = picker.value(0);
					t.month = picker.value(1);
					t.year = picker.value(2);
					clampDay(t);
					hal::setTime(os::makeTime(t));
					next();
				}else if(cancelled) back();
				return true;

			case Format:
			case Theme:
				if(list.onKey(e)){
					if(step == Theme){
						s.accent = list.selected;
					}
					os::play(os::Sound::Click);
				}else if(click && e.key == os::Key::Select){
					if(step == Format) s.use24h = list.selected == 0;
					next();
				}else if(click && e.key == os::Key::Back) back();
				return true;

			case Face:
				if(e.action == os::KeyAction::Down && e.key == os::Key::Up) s.watchFace = (s.watchFace + apps::faceCount() - 1) % apps::faceCount();
				else if(e.action == os::KeyAction::Down && e.key == os::Key::Down) s.watchFace = (s.watchFace + 1) % apps::faceCount();
				else if(click && e.key == os::Key::Select) next();
				else if(click && e.key == os::Key::Back) back();
				return true;

			case Goal:
				if(picker.onKey(e, cancelled)){
					s.stepGoal = picker.value(0) * 1000;
					next();
				}else if(cancelled) back();
				return true;

			default:
				return true;
		}
	}

	void onDraw(os::Canvas& c) override {
		switch(step){
			case Welcome: drawWelcome(c); break;
			case Time:
				title(c, "Set the time");
				picker.draw(c, 62, &fonts::DejaVu24);
				text(c, "SELECT to confirm", 64, 92, Small, col::TextFaint, textdatum_t::top_center);
				break;
			case Date:
				title(c, "Set the date");
				picker.draw(c, 62, Body);
				text(c, "day / month / year", 64, 88, Small, col::TextFaint, textdatum_t::top_center);
				break;
			case Format:
				title(c, "Time format");
				list.draw(c, 30, 110);
				break;
			case Theme:
				title(c, "Pick your colour");
				list.draw(c, 30, 110);
				break;
			case Face: drawFace(c); break;
			case Goal:
				title(c, "Daily step goal");
				picker.draw(c, 62, &fonts::DejaVu24);
				text(c, "thousand steps", 64, 86, Small, col::TextDim, textdatum_t::top_center);
				break;
			case Tour: drawTour(c); break;
			case Done: drawDone(c); break;
			default: break;
		}
		if(step != Welcome && step != Done && step != Face) drawProgress(c);
	}

private:
	Step step = Welcome;
	uint32_t start = 0;
	uint32_t anim = 0;
	bool finished = false;
	ListView list;
	Picker picker;

	static void clampDay(os::DateTime& t){
		static const int days[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		int max = days[t.month - 1];
		const bool leap = (t.year % 4 == 0 && t.year % 100 != 0) || t.year % 400 == 0;
		if(t.month == 2 && !leap) max = 28;
		if(t.day > max) t.day = max;
	}

	// Accent colours mean nothing in black and white, so e-paper skips them.
	static bool skipped(int s){ return s == Theme && mono(); }

	void next(){
		os::play(os::Sound::Click);
		int n = step + 1;
		if(skipped(n)) n++;
		enter((Step) n);
	}

	void back(){
		int n = step - 1;
		if(skipped(n)) n--;
		if(n >= Welcome) enter((Step) n);
	}

	void enter(Step s){
		step = s;
		anim = 0;
		const auto t = os::localTime();
		auto& cfg = os::settings();
		switch(s){
			case Time:
				picker.set({ { t.hour, 0, 23, ":" }, { t.minute, 0, 59, nullptr } });
				break;
			case Date:
				picker.set({ { t.day, 1, 31, "/" }, { t.month, 1, 12, "/" }, { t.year < 2025 ? 2026 : t.year, 2024, 2099, nullptr } });
				break;
			case Format:
				list.set(2, [](void*, int i){
					return ListView::Row{ i == 0 ? "24-hour  13:00" : "12-hour  1:00", nullptr, 0, nullptr, -1 };
				}, nullptr);
				list.selected = cfg.use24h ? 0 : 1;
				break;
			case Theme:
				list.set(accentCount(), [](void*, int i){
					return ListView::Row{ accentAt(i).name, nullptr, accentAt(i).primary, icon::palette, -1 };
				}, nullptr);
				list.selected = cfg.accent;
				break;
			case Goal:
				picker.set({ { cfg.stepGoal / 1000, 1, 30, nullptr } });
				break;
			default:
				break;
		}
	}

	void title(os::Canvas& c, const char* s){
		text(c, s, 64, 8, Body, accent().primary, textdatum_t::top_center);
	}

	void drawProgress(os::Canvas& c){
		const int n = StepCount - 2;
		const int x0 = 64 - (n - 1) * 4;
		for(int i = 0; i < n; i++){
			const bool on = i + 1 <= step;
			c.fillCircle(x0 + i * 8, 120, on ? 2 : 1, on ? accent().primary : col::TextFaint);
		}
	}

	void drawWelcome(os::Canvas& c){
		const float t = anim / 1000.0f;
		// Waving bugdroid
		const int cx = 64, cy = 46;
		const uint16_t g = col::AndroidGreen;
		c.fillArc(cx, cy - 4, 0, 14, 180, 360, g);
		c.fillCircle(cx - 5, cy - 10, 2, col::Bg);
		c.fillCircle(cx + 5, cy - 10, 2, col::Bg);
		c.drawWideLine(cx - 8, cy - 15, cx - 11, cy - 21, 1, g);
		c.drawWideLine(cx + 8, cy - 15, cx + 11, cy - 21, 1, g);
		c.fillSmoothRoundRect(cx - 14, cy - 1, 28, 22, 5, g);
		c.fillRect(cx - 14, cy - 1, 28, 6, g);
		c.fillSmoothRoundRect(cx - 9, cy + 18, 6, 10, 3, g);
		c.fillSmoothRoundRect(cx + 3, cy + 18, 6, 10, 3, g);
		c.fillSmoothRoundRect(cx - 22, cy, 6, 16, 3, g);
		const float wave = mono() ? -0.9f : sinf(t * 7) * 0.5f - 0.9f;
		const int ax = cx + 19, ay = cy + 3;
		c.drawWideLine(ax, ay, ax + (int) (cosf(wave) * 12), ay + (int) (sinf(wave) * 12), 3, g);

		const char* hello[] = { "Hi there", "Hola", "Bonjour", "Hallo", "Ciao", "Zdravo" };
		const int idx = (anim / 1400) % 6;
		text(c, hello[idx], 64, 88, Title, col::Text, textdatum_t::middle_center);
		c.fillSmoothRoundRect(34, 102, 60, 18, 9, accent().primary);
		text(c, "Start", 64, 111, Body, accent().container, textdatum_t::middle_center);
	}

	void drawFace(os::Canvas& c){
		auto t = os::localTime();
		const auto& f = apps::face(os::settings().watchFace);
		f.draw(c, t, (hal::millis() % 1000) / 1000.0f);
		c.fillSmoothRoundRect(14, 2, 100, 16, 8, col::Surface2);
		char buf[32];
		snprintf(buf, sizeof(buf), "< %s >", f.name);
		text(c, buf, 64, 10, Small, col::Text, textdatum_t::middle_center);
		c.fillSmoothRoundRect(24, 110, 80, 16, 8, accent().primary);
		text(c, "Use this face", 64, 118, Small, accent().container, textdatum_t::middle_center);
	}

	void drawTour(os::Canvas& c){
		title(c, "Your buttons");
		struct Row { const char* key; const char* what; } rows[] = {
			{ "UP", "Quick tiles" },
			{ "DOWN", "Notifications" },
			{ "SELECT", "Apps" },
			{ "BACK", "Back, sleep" },
			{ "HOLD", "Power menu" },
		};
		for(int i = 0; i < 5; i++){
			const int y = 27 + i * 17;
			c.fillSmoothRoundRect(4, y, 44, 14, 7, accent().container);
			text(c, rows[i].key, 26, y + 7, Small, accent().primary, textdatum_t::middle_center);
			text(c, rows[i].what, 52, y + 7, Small, col::Text, textdatum_t::middle_left);
		}
	}

	void drawDone(os::Canvas& c){
		const float p = fminf(1.0f, anim / 600.0f);
		const auto& a = accent();
		c.fillSmoothCircle(64, 52, (int) (8 + p * 18), a.primary);
		if(p >= 1) icon::check(c, 64, 52, a.container);
		text(c, "You're all set", 64, 92, Body, col::Text, textdatum_t::middle_center);
		text(c, "Enjoy StarDroid", 64, 108, Small, col::TextDim, textdatum_t::middle_center);
	}
};

}

const os::AppInfo apps::Setup = { "setup", "Setup", col::AndroidGreen, nullptr, os::make<SetupWizard>, false };
