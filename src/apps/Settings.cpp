// Settings app
#include "Apps.h"
#include <cstdio>

using namespace ui;

namespace {

enum class Page : uint8_t { Main, Brightness, Timeout, Theme, Time, Date, Goal, Battery, About, Developer, Reset };

enum Item : uint8_t {
	IBrightness, ITimeout, IFace, ITheme, ISound, IDnd, I24h, ITime, IDate, IGoal, IBattery, IDiagnostics, IAbout, IDeveloper, IReset
};

const uint8_t Timeouts[] = { 5, 10, 15, 30, 60 };

class SettingsApp : public os::Activity {
public:
	void onCreate(const os::Intent&) override { open(Page::Main); }
	void onResume() override { if(page == Page::Main) buildMain(); }

	void onUpdate(uint32_t dt) override { list.update(dt); }

	bool showStatusBar() const override { return true; }

	void onDraw(os::Canvas& c) override {
		char buf[32];
		switch(page){
			case Page::Brightness: {
				header(c, "Brightness");
				const auto& a = accent();
				const int pct = os::settings().brightness;
				icon::sun(c, 64, 46, a.primary);
				c.fillSmoothRoundRect(14, 64, 100, 12, 6, col::Surface2);
				c.fillSmoothRoundRect(14, 64, 12 + pct * 88 / 100, 12, 6, a.primary);
				snprintf(buf, sizeof(buf), "%d%%", pct);
				text(c, buf, 64, 92, Title, col::Text, textdatum_t::middle_center);
				hint(c, "UP/DOWN adjust");
				return;
			}
			case Page::Time:
				header(c, "Set time");
				picker.draw(c, 66, &fonts::DejaVu24);
				hint(c, "SELECT next, BACK prev");
				return;
			case Page::Date:
				header(c, "Set date");
				picker.draw(c, 62, Body);
				hint(c, "day / month / year");
				return;
			case Page::Goal:
				header(c, "Step goal");
				picker.draw(c, 62, &fonts::DejaVu24);
				text(c, "thousand steps", 64, 86, Small, col::TextDim, textdatum_t::top_center);
				return;
			case Page::Battery: {
				header(c, "Battery");
				const auto b = hal::battery();
				ring(c, 64, 62, 30, 6, b.percent / 100.0f, b.percent <= 15 ? col::Red : accent().primary, col::Surface2);
				snprintf(buf, sizeof(buf), "%d%%", b.percent);
				text(c, buf, 64, 62, Title, col::Text, textdatum_t::middle_center);
				snprintf(buf, sizeof(buf), "%d mV  %s", b.millivolts, b.charging ? "Charging" : (b.usb ? "Full" : "On battery"));
				text(c, buf, 64, 104, Small, col::TextDim, textdatum_t::middle_center);
				return;
			}
			case Page::Reset:
				header(c, "Reset watch?");
				text(c, "Erases all settings", 64, 50, Small, col::TextDim, textdatum_t::middle_center);
				text(c, "and starts setup again.", 64, 62, Small, col::TextDim, textdatum_t::middle_center);
				c.fillSmoothRoundRect(24, 80, 80, 18, 9, col::Red);
				text(c, "Hold SELECT", 64, 89, Small, col::Bg, textdatum_t::middle_center);
				return;
			default:
				list.draw(c);
				return;
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		const bool click = e.action == os::KeyAction::Click;
		const bool press = e.action == os::KeyAction::Down || e.action == os::KeyAction::Repeat;
		auto& s = os::settings();
		bool cancelled = false;

		switch(page){
			case Page::Main:
				if(list.onKey(e)) return true;
				if(click && e.key == os::Key::Select) activateMain(list.selected);
				else return false;
				return true;

			case Page::Brightness:
				if(press && e.key == os::Key::Up && s.brightness < 100) s.brightness += 5;
				else if(press && e.key == os::Key::Down && s.brightness > 10) s.brightness -= 5;
				else if(click && (e.key == os::Key::Back || e.key == os::Key::Select)){
					os::saveSettings();
					open(Page::Main);
				}
				return true;

			case Page::Time:
				if(picker.onKey(e, cancelled)){
					auto t = os::localTime();
					t.hour = picker.value(0);
					t.minute = picker.value(1);
					t.second = 0;
					hal::setTime(os::makeTime(t));
					os::toast("Time set");
					open(Page::Main);
				}else if(cancelled) open(Page::Main);
				return true;

			case Page::Date:
				if(picker.onKey(e, cancelled)){
					auto t = os::localTime();
					t.day = picker.value(0);
					t.month = picker.value(1);
					t.year = picker.value(2);
					hal::setTime(os::makeTime(t));
					os::toast("Date set");
					open(Page::Main);
				}else if(cancelled) open(Page::Main);
				return true;

			case Page::Goal:
				if(picker.onKey(e, cancelled)){
					s.stepGoal = picker.value(0) * 1000;
					os::saveSettings();
					open(Page::Main);
				}else if(cancelled) open(Page::Main);
				return true;

			case Page::Timeout:
			case Page::Theme:
				if(list.onKey(e)){
					if(page == Page::Theme) s.accent = list.selected;
					return true;
				}
				if(click && e.key == os::Key::Select){
					if(page == Page::Timeout) s.screenTimeout = Timeouts[list.selected];
					os::saveSettings();
					open(Page::Main);
				}else if(click && e.key == os::Key::Back){
					os::saveSettings();
					open(Page::Main);
				}
				return true;

			case Page::About:
				if(list.onKey(e)) return true;
				if(click && e.key == os::Key::Select && list.selected == 4) buildNumberTap();
				else if(click && e.key == os::Key::Back) open(Page::Main);
				return true;

			case Page::Developer:
				if(list.onKey(e)) return true;
				if(click && e.key == os::Key::Select){
					if(list.selected == 0){
						s.showFps = !s.showFps;
					}else if(list.selected == 1){
						s.developer = false;
						os::toast("Dev mode off");
						os::saveSettings();
						open(Page::Main);
						return true;
					}else if(list.selected == 2){
						os::notify("settings", "Test notification", "Hello from developer options!");
					}
					os::saveSettings();
				}else if(click && e.key == os::Key::Back) open(Page::Main);
				return true;

			case Page::Battery:
				if(click && (e.key == os::Key::Back || e.key == os::Key::Select)) open(Page::Main);
				return true;

			case Page::Reset:
				if(e.action == os::KeyAction::LongPress && e.key == os::Key::Select){
					hal::prefClear();
					hal::restart();
				}else if(click && e.key == os::Key::Back) open(Page::Main);
				return true;
		}
		return false;
	}

private:
	Page page = Page::Main;
	ListView list;
	Picker picker;
	int itemIds[16];
	int itemCount = 0;
	int buildTaps = 0;
	char detail[16][16];

	void open(Page p){
		const Page from = page;
		page = p;
		list.rowH = 26;
		const auto& s = os::settings();
		const auto t = os::localTime();
		switch(p){
			case Page::Main:
				buildMain();
				if(from == Page::Main) list.selected = 0;
				break;
			case Page::Timeout:
				list.set(5, [](void*, int i){
					static char buf[5][12];
					snprintf(buf[i], sizeof(buf[i]), "%d seconds", Timeouts[i]);
					return ListView::Row{ buf[i], nullptr, 0, nullptr, (int8_t) (os::settings().screenTimeout == Timeouts[i]) };
				}, nullptr);
				list.selected = 2;
				for(int i = 0; i < 5; i++) if(Timeouts[i] == s.screenTimeout) list.selected = i;
				break;
			case Page::Theme:
				list.set(accentCount(), [](void*, int i){
					return ListView::Row{ accentAt(i).name, nullptr, accentAt(i).primary, icon::palette, -1 };
				}, nullptr);
				list.selected = s.accent;
				break;
			case Page::Time:
				picker.set({ { t.hour, 0, 23, ":" }, { t.minute, 0, 59, nullptr } });
				break;
			case Page::Date:
				picker.set({ { t.day, 1, 31, "/" }, { t.month, 1, 12, "/" }, { t.year, 2024, 2099, nullptr } });
				break;
			case Page::Goal:
				picker.set({ { s.stepGoal / 1000, 1, 30, nullptr } });
				break;
			case Page::About:
				buildTaps = 0;
				list.rowH = 32;
				list.set(6, [](void*, int i){
					static char buf[2][24];
					switch(i){
						case 0: return ListView::Row{ "Device", hal::deviceName(), col::AndroidGreen, icon::watch, -1 };
						case 1:
							snprintf(buf[0], sizeof(buf[0]), "%s %s", os::Name, os::Version);
							return ListView::Row{ "Software", buf[0], col::AndroidGreen, icon::android, -1 };
						case 2: return ListView::Row{ "Codename", os::Codename, col::Pink, icon::info, -1 };
						case 3:
							snprintf(buf[1], sizeof(buf[1]), "Revision %d", hal::revision());
							return ListView::Row{ "Hardware", buf[1], col::Blue, icon::gear, -1 };
						case 4: return ListView::Row{ "Build number", "SD1A.261018.001", col::Yellow, icon::code, -1 };
						default: return ListView::Row{ "Inspired by", "Android & Wear OS", col::Teal, icon::android, -1 };
					}
				}, nullptr);
				list.selected = 0;
				break;
			case Page::Developer:
				list.set(3, [](void*, int i){
					switch(i){
						case 0: return ListView::Row{ "Show FPS", nullptr, 0, nullptr, (int8_t) os::settings().showFps };
						case 1: return ListView::Row{ "Turn off dev mode", nullptr, 0, nullptr, -1 };
						default: return ListView::Row{ "Test notification", nullptr, 0, nullptr, -1 };
					}
				}, nullptr);
				list.selected = 0;
				break;
			default:
				break;
		}
	}

	void buildMain(){
		const auto& s = os::settings();
		itemCount = 0;
		for(int i = IBrightness; i <= IReset; i++){
			if(i == IDeveloper && !s.developer) continue;
			itemIds[itemCount++] = i;
		}
		list.rowH = 32;
		list.set(itemCount, [](void* ctx, int i){
			auto* self = (SettingsApp*) ctx;
			const auto& s = os::settings();
			char* d = self->detail[i];
			switch(self->itemIds[i]){
				case IBrightness:
					snprintf(d, 16, "%d%%", s.brightness);
					return ListView::Row{ "Brightness", d, col::Yellow, icon::sun, -1 };
				case ITimeout:
					snprintf(d, 16, "After %ds", s.screenTimeout);
					return ListView::Row{ "Screen off", d, col::Blue, icon::clock, -1 };
				case IFace: return ListView::Row{ "Watch face", apps::face(s.watchFace).name, col::AndroidGreen, icon::watch, -1 };
				case ITheme: return ListView::Row{ "Colour", accent().name, accent().primary, icon::palette, -1 };
				case ISound: return ListView::Row{ "Sound", nullptr, col::Orange, icon::sound, (int8_t) s.sound };
				case IDnd: return ListView::Row{ "DND", nullptr, col::Purple, icon::dnd, (int8_t) s.dnd };
				case I24h: return ListView::Row{ "24-hour", nullptr, col::Teal, icon::clock, (int8_t) s.use24h };
				case ITime: return ListView::Row{ "Set time", nullptr, col::Blue, icon::clock, -1 };
				case IDate: return ListView::Row{ "Set date", nullptr, col::Blue, icon::calendar, -1 };
				case IGoal:
					snprintf(d, 16, "%u", s.stepGoal);
					return ListView::Row{ "Step goal", d, col::AndroidGreen, icon::steps, -1 };
				case IBattery:
					snprintf(d, 16, "%d%%", hal::battery().percent);
					return ListView::Row{ "Battery", d, col::AndroidGreen, icon::battery, -1 };
				case IDiagnostics: return ListView::Row{ "Diagnostics", "Hardware test", col::Teal, icon::check, -1 };
				case IAbout: return ListView::Row{ "About watch", nullptr, col::TextDim, icon::info, -1 };
				case IDeveloper: return ListView::Row{ "Developer", nullptr, col::AndroidGreen, icon::code, -1 };
				default: return ListView::Row{ "Reset watch", nullptr, col::Red, icon::restart, -1 };
			}
		}, this);
		if(list.selected >= itemCount) list.selected = itemCount - 1;
	}

	void activateMain(int row){
		auto& s = os::settings();
		switch(itemIds[row]){
			case IBrightness: open(Page::Brightness); break;
			case ITimeout: open(Page::Timeout); break;
			case IFace: os::startActivity({ "facepicker" }); break;
			case ITheme: open(Page::Theme); break;
			case ISound: s.sound = !s.sound; os::saveSettings(); break;
			case IDnd: s.dnd = !s.dnd; os::saveSettings(); break;
			case I24h: s.use24h = !s.use24h; os::saveSettings(); break;
			case ITime: open(Page::Time); break;
			case IDate: open(Page::Date); break;
			case IGoal: open(Page::Goal); break;
			case IBattery: open(Page::Battery); break;
			case IDiagnostics: os::startActivity({ "hwtest" }); break;
			case IAbout: open(Page::About); break;
			case IDeveloper: open(Page::Developer); break;
			case IReset: open(Page::Reset); break;
		}
	}

	// Tap "Build number" seven times, just like on a phone.
	void buildNumberTap(){
		auto& s = os::settings();
		if(s.developer){
			os::toast("Already a developer");
			return;
		}
		buildTaps++;
		if(buildTaps >= 7){
			s.developer = true;
			os::saveSettings();
			os::toast("You're a developer!");
			os::play(os::Sound::Success);
		}else if(buildTaps >= 3){
			char buf[40];
			snprintf(buf, sizeof(buf), "%d taps to go", 7 - buildTaps);
			os::toast(buf);
		}
	}
};

}

const os::AppInfo apps::Settings = { "settings", "Settings", col::TextDim, icon::gear, os::make<SettingsApp>, true };
