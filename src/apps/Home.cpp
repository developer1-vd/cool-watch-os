// Launcher (watch face), face picker, app drawer and notification list.
#include "Apps.h"
#include <cmath>
#include <cstdio>

using namespace ui;

namespace {

// Gives faces a smooth sub-second fraction, since the RTC only ticks in seconds.
// E-paper gets whole seconds: one refresh per second is all it can do.
float secondFraction(const os::DateTime& t){
	if(mono()) return 0;
	static int lastSec = -1;
	static uint32_t secStart = 0;
	if(t.second != lastSec){
		lastSec = t.second;
		secStart = hal::millis();
	}
	return fminf(0.999f, (hal::millis() - secStart) / 1000.0f);
}

class Launcher : public os::Activity {
public:
	bool showStatusBar() const override { return false; }

	void onDraw(os::Canvas& c) override {
		const auto t = os::localTime();
		apps::face(os::settings().watchFace).draw(c, t, secondFraction(t));
	}

	bool onKey(const os::KeyEvent& e) override {
		if(e.action == os::KeyAction::LongPress && e.key == os::Key::Select){
			os::play(os::Sound::Click);
			os::startActivity({ "facepicker" });
			return true;
		}
		if(e.action != os::KeyAction::Click) return e.key == os::Key::Back;
		switch(e.key){
			case os::Key::Select: os::startActivity({ "drawer" }); break;
			case os::Key::Up: os::systemui::openShade(); break;
			case os::Key::Down: os::startActivity({ "notifications" }); break;
			case os::Key::Back: os::sleepScreen(); break;
		}
		return true;
	}
};

class FacePicker : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		sel = os::settings().watchFace;
		pos = sel;
	}
	bool showStatusBar() const override { return false; }

	void onUpdate(uint32_t dt) override { pos = approach(pos, (float) sel, dt, 18); }

	bool onKey(const os::KeyEvent& e) override {
		const int n = apps::faceCount();
		if(e.action == os::KeyAction::Down && e.key == os::Key::Up) sel = (sel + n - 1) % n;
		else if(e.action == os::KeyAction::Down && e.key == os::Key::Down) sel = (sel + 1) % n;
		else if(e.action == os::KeyAction::Click && e.key == os::Key::Select){
			os::settings().watchFace = sel;
			os::saveSettings();
			os::play(os::Sound::Success);
			os::finish();
		}else return false;
		return true;
	}

	void onDraw(os::Canvas& c) override {
		const auto t = os::localTime();
		const float f = secondFraction(t);
		apps::face(sel).draw(c, t, f);

		// Frame it like a Wear OS "edit face" carousel
		const auto& a = accent();
		c.drawRoundRect(0, 0, 128, 128, 10, a.primary);
		c.drawRoundRect(1, 1, 126, 126, 9, a.primary);
		c.fillSmoothRoundRect(24, 108, 80, 16, 8, col::Surface2);
		char buf[24];
		snprintf(buf, sizeof(buf), "%s  %d/%d", apps::face(sel).name, sel + 1, apps::faceCount());
		text(c, buf, 64, 116, Small, col::Text, textdatum_t::middle_center);
		c.fillTriangle(58, 6, 70, 6, 64, 1, a.primary);
	}

private:
	int sel = 0;
	float pos = 0;
};

class Drawer : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		for(int i = 0; i < os::appCount(); i++){
			if(os::app(i).inDrawer && os::appAvailable(os::app(i))) ids[count++] = i;
		}
		list.rowH = 28;
		list.set(count, [](void* ctx, int i){
			auto* self = (Drawer*) ctx;
			const auto& app = os::app(self->ids[i]);
			return ListView::Row{ app.label, nullptr, app.color, app.icon, -1 };
		}, this);
	}

	void onUpdate(uint32_t dt) override { list.update(dt); }
	void onDraw(os::Canvas& c) override { list.draw(c); }

	bool onKey(const os::KeyEvent& e) override {
		if(list.onKey(e)) return true;
		if(e.action == os::KeyAction::Click && e.key == os::Key::Select){
			os::startActivity({ os::app(ids[list.selected]).id });
			return true;
		}
		return false;
	}

private:
	int ids[24];
	int count = 0;
	ListView list;
};

class Notifications : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		list.rowH = 34;
		refresh();
	}

	void onResume() override { refresh(); }
	void onPause() override { os::markAllRead(); }

	void refresh(){
		list.set(os::notificationCount() + (os::notificationCount() ? 1 : 0), [](void*, int i){
			if(i == os::notificationCount()) return ListView::Row{ "Clear all", nullptr, col::Surface2, icon::check, -1 };
			const auto& n = os::notification(i);
			const auto* app = os::findApp(n.appId);
			return ListView::Row{ n.title, n.text, app ? app->color : col::TextDim, app ? app->icon : icon::bell, -1 };
		}, nullptr);
		if(list.selected >= os::notificationCount() + 1) list.selected = 0;
	}

	void onUpdate(uint32_t dt) override { list.update(dt); }

	void onDraw(os::Canvas& c) override {
		if(os::notificationCount() == 0){
			c.fillSmoothCircle(64, 54, 18, col::Surface2);
			icon::bell(c, 64, 54, col::TextDim);
			text(c, "No notifications", 64, 86, Body, col::TextDim, textdatum_t::middle_center);
			return;
		}
		list.draw(c);
	}

	bool onKey(const os::KeyEvent& e) override {
		if(list.onKey(e)) return true;
		if(e.action == os::KeyAction::Click && e.key == os::Key::Select && os::notificationCount()){
			if(list.selected == os::notificationCount()){
				os::clearNotifications();
				os::toast("Cleared");
				os::finish();
			}else{
				const auto* app = os::findApp(os::notification(list.selected).appId);
				os::dismissNotification(list.selected);
				if(app && app->inDrawer) os::startActivity({ app->id });
				refresh();
			}
			return true;
		}
		return false;
	}

private:
	ListView list;
};

}

const os::AppInfo apps::Launcher = { "launcher", "Watch face", col::Mint, icon::watch, os::make<::Launcher>, false };
const os::AppInfo apps::FacePicker = { "facepicker", "Watch faces", col::Mint, icon::watch, os::make<::FacePicker>, false };
const os::AppInfo apps::Drawer = { "drawer", "Apps", col::Mint, icon::apps, os::make<::Drawer>, false };
const os::AppInfo apps::Notifications = { "notifications", "Notifications", col::Blue, icon::bell, os::make<::Notifications>, false };
