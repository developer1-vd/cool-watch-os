#include "Os.h"
#include "Ui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ui;

namespace os {

namespace {

// ---- Registry & back stack -----------------------------------------------------

constexpr int MaxApps = 24;
AppInfo apps[MaxApps];
int numApps = 0;

std::vector<std::unique_ptr<Activity>> stack;
std::vector<const char*> stackIds;

enum class Transition : uint8_t { None, Push, Pop, Fade };
Transition transition = Transition::None;
uint32_t transitionStart = 0;
constexpr uint32_t TransitionMs = 230;

// Activities may call startActivity/finish from inside callbacks, so stack
// changes are queued and applied between frames.
enum class PendingOp : uint8_t { None, Start, Replace, Finish, Home };
PendingOp pendingOp = PendingOp::None;
Intent pendingIntent{ nullptr };

Canvas frame;    // what is on screen
Canvas work;     // current activity is drawn here
Canvas snapshot; // previous frame, for transitions

// ---- Input -----------------------------------------------------------------------

constexpr uint32_t DebounceMs = 15;
constexpr uint32_t LongPressMs = 650;
constexpr uint32_t RepeatDelayMs = 450;
constexpr uint32_t RepeatMs = 90;

struct ButtonState {
	bool raw = false;
	bool down = false;
	uint32_t changed = 0;
	uint32_t pressedAt = 0;
	uint32_t lastRepeat = 0;
	bool longFired = false;
};
ButtonState buttons[4];
bool swallowUntilRelease = false;

// ---- Power -----------------------------------------------------------------------

bool lcdOn = true;
bool ambientMode = false;
int ambientMinute = -1;
int ambientRefreshes = 0;
uint32_t lastActivity = 0;
float shownBrightness = 0;
std::vector<Service*> services;

// ---- Sound -----------------------------------------------------------------------

struct Note {
	uint16_t freq, ms;
};
Note notes[16];
int noteHead = 0, noteCount = 0;
uint32_t noteStart = 0;
bool notePlaying = false;

// ---- Notifications & overlays ----------------------------------------------------

constexpr int MaxNotifs = 8;
Notification notifs[MaxNotifs];
int notifCount = 0;

char toastText[40] = {};
uint32_t toastUntil = 0;

int headsUp = -1;
uint32_t headsUpUntil = 0;
float headsUpY = -40;

bool shadeOpen = false;
float shadeY = -H;
int shadeFocus = 0;

bool powerMenu = false;
int powerFocus = 0;
float powerAnim = 0;

Settings cfg;

uint32_t lastFrame = 0;
float fps = 0;

// ---- Steps -----------------------------------------------------------------------

uint32_t stepBase = 0, stepCarry = 0, stepLastHw = 0;
int32_t stepDay = -1;
uint32_t stepHistory[7] = {}; // [0] unused, [1] = yesterday ...

int32_t dayKey(const DateTime& t){ return t.year * 1000 + t.month * 40 + t.day; }

void stepsPoll(){
	const uint32_t hw = hal::stepCounter();
	const DateTime t = localTime();
	const int32_t today = dayKey(t);
	if(hw < stepLastHw){
		// The 16-bit counter wrapped or the IMU was power cycled; either way
		// it restarted from zero, so bank what it had counted.
		stepCarry += stepLastHw - stepBase;
		stepBase = 0;
		hal::prefSet("stepCarry", stepCarry);
		hal::prefSet("stepBase", stepBase);
	}
	stepLastHw = hw;
	if(today != stepDay){
		if(stepDay >= 0){
			const uint32_t total = stepCarry + (hw - stepBase);
			for(int i = 6; i > 1; i--) stepHistory[i] = stepHistory[i - 1];
			stepHistory[1] = total;
			char key[4] = { 's', 't', '0', 0 };
			for(int i = 1; i < 7; i++){
				key[2] = '0' + i;
				hal::prefSet(key, stepHistory[i]);
			}
		}
		stepDay = today;
		stepBase = hw;
		stepCarry = 0;
		hal::prefSet("stepDay", stepDay);
		hal::prefSet("stepBase", stepBase);
		hal::prefSet("stepCarry", 0);
	}
}

void stepsLoad(){
	stepDay = hal::prefGet("stepDay", -1);
	stepBase = hal::prefGet("stepBase", 0);
	stepCarry = hal::prefGet("stepCarry", 0);
	stepLastHw = hal::stepCounter();
	if(stepLastHw < stepBase){
		stepBase = 0; // the IMU was power cycled and restarted from zero
	}
	char key[4] = { 's', 't', '0', 0 };
	for(int i = 1; i < 7; i++){
		key[2] = '0' + i;
		stepHistory[i] = hal::prefGet(key, 0);
	}
}

// ---- Helpers ---------------------------------------------------------------------

void applyBrightness(uint32_t dt){
	float target = lcdOn ? cfg.brightness : 0;
	if(lcdOn && !stack.empty() && stack.back()->brightnessOverride() >= 0) target = stack.back()->brightnessOverride();
	shownBrightness = approach(shownBrightness, target, dt, 18);
	static uint8_t last = 255;
	const uint8_t b = (uint8_t) lroundf(shownBrightness);
	if(b != last){
		hal::backlight(b);
		last = b;
	}
}

float easeOut(float t){ return 1 - (1 - t) * (1 - t) * (1 - t); }

void destroyTop(){
	if(stack.empty()) return;
	stack.back()->onPause();
	stack.back()->onDestroy();
	stack.pop_back();
	stackIds.pop_back();
}

void pushActivity(const Intent& intent){
	const AppInfo* info = findApp(intent.component);
	if(!info){
		toast("App not found");
		return;
	}
	if(!stack.empty()) stack.back()->onPause();
	stack.push_back(info->create());
	stackIds.push_back(info->id);
	stack.back()->onCreate(intent);
	stack.back()->onResume();
}

void beginTransition(Transition t){
	if(mono()) return; // e-paper can't animate, just show the new page
	memcpy(snapshot.getBuffer(), frame.getBuffer(), W * H * 2);
	transition = t;
	transitionStart = hal::millis();
}

void applyPending(){
	const PendingOp op = pendingOp;
	pendingOp = PendingOp::None;
	switch(op){
		case PendingOp::None:
			return;
		case PendingOp::Start:
			beginTransition(Transition::Push);
			pushActivity(pendingIntent);
			break;
		case PendingOp::Replace:
			beginTransition(Transition::Fade);
			destroyTop();
			pushActivity(pendingIntent);
			break;
		case PendingOp::Finish:
			if(stack.size() <= 1) return;
			beginTransition(Transition::Pop);
			destroyTop();
			stack.back()->onResume();
			break;
		case PendingOp::Home:
			if(stack.size() <= 1) return;
			beginTransition(Transition::Pop);
			while(stack.size() > 1) destroyTop();
			stack.back()->onResume();
			break;
	}
}

// ---- Status bar ------------------------------------------------------------------

void drawStatusBar(Canvas& c, uint16_t bg){
	c.fillRect(0, 0, W, StatusBarH, bg);
	const DateTime t = localTime();
	char buf[8];
	int hour = t.hour;
	if(!cfg.use24h){
		hour %= 12;
		if(hour == 0) hour = 12;
	}
	snprintf(buf, sizeof(buf), "%d:%02d", hour, t.minute);
	text(c, buf, W / 2, 2, Small, col::Text, textdatum_t::top_center);

	const auto b = hal::battery();
	batteryGlyph(c, W - 20, 3, b, col::TextDim);

	int x = 6;
	if(unreadCount() > 0){
		c.fillCircle(x + 2, 6, 2, accent().primary);
		x += 8;
	}
	if(cfg.dnd){
		c.fillCircle(x + 3, 6, 3, col::TextDim);
		c.drawFastHLine(x + 1, 6, 5, bg);
		x += 9;
	}
	if(!cfg.sound){
		c.fillRect(x, 4, 2, 4, col::TextDim);
		c.fillTriangle(x + 1, 6, x + 5, 2, x + 5, 10, col::TextDim);
		c.drawLine(x + 6, 3, x + 9, 9, col::Red);
	}
}

// ---- Quick settings shade ----------------------------------------------------------

struct Tile {
	const char* label;
	IconFn icon;
};
constexpr int TileCount = 6;

// Tiles follow the hardware: e-paper watches get Refresh and Paper mode
// instead of Flashlight and Brightness.
Tile tile(int i){
	const auto& f = hal::features();
	switch(i){
		case 0: return f.backlight ? Tile{ "Flashlight", icon::flashlight } : Tile{ "Refresh screen", icon::restart };
		case 1: return f.backlight ? Tile{ "Brightness", icon::sun } : Tile{ "Paper mode", icon::sun };
		case 2: return f.buzzer ? Tile{ "Sound", icon::sound } : Tile{ "Vibration", icon::sound };
		case 3: return { "Do not disturb", icon::dnd };
		case 4: return f.color ? Tile{ "Theme", icon::palette } : Tile{ "Watch face", icon::watch };
		default: return { "Settings", icon::gear };
	}
}

bool tileActive(int i){
	switch(i){
		case 1: return hal::features().backlight ? cfg.brightness >= 75 : cfg.paper;
		case 2: return cfg.sound;
		case 3: return cfg.dnd;
		default: return false;
	}
}

void tileTap(int i){
	switch(i){
		case 0:
			if(!hal::features().backlight){
				hal::refreshFull();
				break;
			}
			shadeOpen = false;
			startActivity({ "flashlight" });
			break;
		case 1: {
			if(!hal::features().backlight){
				cfg.paper = !cfg.paper;
				hal::setPaperMode(cfg.paper);
				saveSettings();
				break;
			}
			const uint8_t steps[] = { 25, 50, 75, 100 };
			uint8_t next = steps[0];
			for(auto s : steps){
				if(s > cfg.brightness){
					next = s;
					break;
				}
			}
			cfg.brightness = next;
			saveSettings();
			char buf[24];
			snprintf(buf, sizeof(buf), "Brightness %d%%", next);
			toast(buf);
			break;
		}
		case 2:
			cfg.sound = !cfg.sound;
			saveSettings();
			toast(cfg.sound ? (hal::features().buzzer ? "Sound on" : "Vibration on") : "Silent");
			if(cfg.sound) play(Sound::Click);
			break;
		case 3:
			cfg.dnd = !cfg.dnd;
			saveSettings();
			toast(cfg.dnd ? "Do not disturb on" : "Do not disturb off");
			break;
		case 4:
			if(!hal::features().color){
				shadeOpen = false;
				startActivity({ "facepicker" });
				break;
			}
			cfg.accent = (cfg.accent + 1) % accentCount();
			saveSettings();
			toast(accent().name);
			break;
		case 5:
			shadeOpen = false;
			startActivity({ "settings" });
			break;
	}
}

void drawShade(Canvas& c){
	const int y0 = (int) shadeY;
	if(y0 <= -H) return;
	c.fillRect(0, y0, W, H, col::Surface);
	c.fillSmoothRoundRect(0, y0 + H - 12, W, 12, 6, col::Surface);

	const DateTime t = localTime();
	char buf[24];
	int hour = t.hour;
	if(!cfg.use24h){
		hour %= 12;
		if(hour == 0) hour = 12;
	}
	snprintf(buf, sizeof(buf), "%d:%02d", hour, t.minute);
	text(c, buf, 8, y0 + 5, Title, col::Text);
	snprintf(buf, sizeof(buf), "%s, %s %d", weekdayName(t.weekday), monthName(t.month), t.day);
	text(c, buf, 8, y0 + 25, Small, col::TextDim);
	const auto b = hal::battery();
	snprintf(buf, sizeof(buf), "%d%%", b.percent);
	text(c, buf, W - 8, y0 + 7, Small, col::TextDim, textdatum_t::top_right);
	batteryGlyph(c, W - 22, y0 + 18, b, col::TextDim);

	const auto& a = accent();
	for(int i = 0; i < TileCount; i++){
		const int col_ = i % 3, row = i / 3;
		const int x = 7 + col_ * 39, y = y0 + 38 + row * 36;
		const bool on = tileActive(i);
		const bool focus = i == shadeFocus;
		card(c, x, y, 36, 32, on ? a.primary : col::Surface2, 12);
		if(focus){
			c.drawRoundRect(x - 1, y - 1, 38, 34, 13, col::Text);
			if(mono()) c.drawRoundRect(x - 2, y - 2, 40, 36, 14, col::Text);
		}
		tile(i).icon(c, x + 18, y + 16, on ? a.container : col::Text);
	}
	// Android shows tile labels; on 128 px we show the focused one.
	char label[32];
	const Tile focused = tile(shadeFocus);
	if(shadeFocus == 1 && hal::features().backlight) snprintf(label, sizeof(label), "Brightness %d%%", cfg.brightness);
	else if(shadeFocus >= 1 && shadeFocus <= 3) snprintf(label, sizeof(label), "%s %s", focused.label, tileActive(shadeFocus) ? "on" : "off");
	else if(shadeFocus == 4 && hal::features().color) snprintf(label, sizeof(label), "Theme: %s", accent().name);
	else snprintf(label, sizeof(label), "%s", focused.label);
	text(c, label, W / 2, y0 + 113, Small, col::Text, textdatum_t::middle_center);
	c.fillRoundRect(W / 2 - 12, y0 + H - 6, 24, 3, 1, faint());
}

// ---- Heads-up notification & toast -------------------------------------------------

void drawHeadsUp(Canvas& c){
	if(headsUp < 0 && headsUpY <= -40) return;
	const int y = (int) headsUpY;
	const Notification& n = notifs[headsUp < 0 ? 0 : headsUp];
	const AppInfo* info = findApp(n.appId);
	card(c, 4, y, W - 8, 36, col::Surface2, 12);
	const uint16_t color = info ? info->color : accent().primary;
	c.fillSmoothCircle(19, y + 18, 10, color);
	if(info && info->icon) info->icon(c, 19, y + 18, col::Bg);
	c.setClipRect(32, y, W - 40, 36);
	text(c, n.title, 33, y + 6, Body, col::Text);
	text(c, n.text, 33, y + 21, Small, col::TextDim);
	c.clearClipRect();
}

void drawToast(Canvas& c){
	if(hal::millis() > toastUntil || !toastText[0]) return;
	int w = textWidth(c, toastText, Small) + 16;
	if(w > W - 4) w = W - 4;
	const int x = (W - w) / 2, y = H - 24;
	c.fillSmoothRoundRect(x, y, w, 16, 8, col::Surface2);
	text(c, toastText, W / 2, y + 8, Small, col::Text, textdatum_t::middle_center);
}

// ---- Power menu ----------------------------------------------------------------------

void drawPowerMenu(Canvas& c){
	if(powerAnim <= 0.01f) return;
	// Dim the content behind
	const uint8_t dim = (uint8_t) (powerAnim * 200);
	auto* px = (uint16_t*) c.getBuffer();
	for(int i = 0; i < W * H; i++){
		const uint16_t v = (px[i] >> 8) | (px[i] << 8);
		const uint16_t d = blend(v, 0, dim);
		px[i] = (d >> 8) | (d << 8);
	}
	if(!powerMenu) return;
	const auto& a = accent();
	const char* labels[] = { "Power off", "Restart" };
	IconFn icons[] = { icon::power, icon::restart };
	for(int i = 0; i < 2; i++){
		const int cx = 36 + i * 56, cy = 58;
		const bool focus = i == powerFocus;
		c.fillSmoothCircle(cx, cy, 20, focus ? a.primary : col::Surface2);
		icons[i](c, cx, cy, focus ? a.container : col::Text);
	}
	text(c, labels[powerFocus], W / 2, 92, Body, col::Text, textdatum_t::middle_center);
	hint(c, "BACK to cancel");
}

// ---- Input dispatch ------------------------------------------------------------------

void dispatch(const KeyEvent& e){
	userActivity();

	if(powerMenu){
		if(e.action == KeyAction::Down && (e.key == Key::Up || e.key == Key::Down)) powerFocus ^= 1;
		if(e.action == KeyAction::Click && e.key == Key::Back) powerMenu = false;
		if(e.action == KeyAction::Click && e.key == Key::Select){
			frame.fillScreen(col::Bg);
			text(frame, powerFocus == 0 ? "Shutting down..." : "Restarting...", W / 2, H / 2, Small, col::TextDim, textdatum_t::middle_center);
			hal::present(frame);
			saveSettings();
			hal::idle(600);
			if(powerFocus == 0) hal::powerOff();
			else hal::restart();
		}
		return;
	}

	if(e.key == Key::Back && e.action == KeyAction::LongPress){
		shadeOpen = false;
		powerMenu = true;
		powerFocus = 0;
		play(Sound::Click);
		return;
	}

	if(headsUp >= 0 && e.action == KeyAction::Click){
		if(e.key == Key::Select){
			headsUp = -1;
			startActivity({ "notifications" });
			return;
		}
		if(e.key == Key::Back){
			headsUp = -1;
			return;
		}
	}

	if(shadeOpen){
		const bool press = e.action == KeyAction::Down || e.action == KeyAction::Repeat;
		if(press && e.key == Key::Down) shadeFocus = (shadeFocus + 1) % TileCount;
		if(press && e.key == Key::Up){
			if(shadeFocus == 0) shadeOpen = false;
			else shadeFocus--;
		}
		if(e.action == KeyAction::Click && e.key == Key::Select) tileTap(shadeFocus);
		if(e.action == KeyAction::Click && e.key == Key::Back) shadeOpen = false;
		return;
	}

	// Keys keep working during transitions: the new activity is already on
	// top, and dropping them made a quick double-Back lose its second press.
	if(stack.empty()) return;

	if(stack.back()->onKey(e)) return;

	if(e.key == Key::Back && e.action == KeyAction::Click) finish();
}

void pollInput(){
	const uint32_t now = hal::millis();
	bool anyDown = false;
	for(int i = 0; i < 4; i++){
		auto& b = buttons[i];
		const bool raw = hal::button((hal::Button) i);
		if(raw != b.raw){
			b.raw = raw;
			b.changed = now;
		}
		if(raw) anyDown = true;
		if(b.raw != b.down && now - b.changed >= DebounceMs){
			b.down = b.raw;
			if(swallowUntilRelease) continue;
			const Key key = (Key) i;
			if(b.down){
				b.pressedAt = b.lastRepeat = now;
				b.longFired = false;
				dispatch({ key, KeyAction::Down });
			}else{
				dispatch({ key, KeyAction::Up });
				if(!b.longFired) dispatch({ key, KeyAction::Click });
			}
		}
		if(b.down && !swallowUntilRelease){
			if(!b.longFired && now - b.pressedAt >= LongPressMs){
				b.longFired = true;
				dispatch({ (Key) i, KeyAction::LongPress });
			}
			if((i == 0 || i == 1) && now - b.pressedAt >= RepeatDelayMs && now - b.lastRepeat >= RepeatMs){
				b.lastRepeat = now;
				dispatch({ (Key) i, KeyAction::Repeat });
			}
		}
	}
	if(!anyDown && swallowUntilRelease){
		bool settled = true;
		for(auto& b : buttons) settled &= !b.down;
		if(settled) swallowUntilRelease = false;
	}
}

// ---- Sound ---------------------------------------------------------------------------

void soundUpdate(){
	if(noteCount == 0){
		if(notePlaying){
			hal::tone(0);
			notePlaying = false;
		}
		return;
	}
	const uint32_t now = hal::millis();
	const Note& n = notes[noteHead];
	if(!notePlaying){
		hal::tone(n.freq);
		noteStart = now;
		notePlaying = true;
	}else if(now - noteStart >= n.ms){
		noteHead = (noteHead + 1) % 16;
		noteCount--;
		notePlaying = false;
		if(noteCount == 0) hal::tone(0);
		else soundUpdate();
	}
}

// ---- Sleep -----------------------------------------------------------------------------

void ledIndicator(bool on){
	if(!on){
		hal::rgb(false, false, false);
		return;
	}
	const auto b = hal::battery();
	if(b.usb){
		hal::rgb(b.charging, !b.charging, false); // red charging, green full
	}else{
		hal::rgb(false, false, false);
	}
}

void render(uint32_t dt);

void renderAmbient(){
	ambientMinute = localTime().minute;
	// Partial refreshes slowly leave ghosts; clean up now and then.
	if(++ambientRefreshes % 30 == 0) hal::refreshFull();
	render(0);
}

void sleepStep(){
	uint32_t wait = 60000;
	for(auto* s : services){
		const uint32_t ms = s->msUntilNext();
		if(ms < wait) wait = ms;
	}

	const bool persistent = hal::features().persistentDisplay;
	if(persistent){
		// Wake at the next minute to update the ambient watch face.
		const uint32_t toMinute = (60 - localTime().second) * 1000u;
		if(toMinute < wait) wait = toMinute;
	}

	// Classic Android notification light: blink while there is something unread.
	const bool blink = hal::features().rgbLed && unreadCount() > 0 && !cfg.dnd && !hal::battery().usb;
	if(blink){
		if(wait > 4000) wait = 4000;
		hal::rgb(false, true, false);
		hal::sleep(40);
		hal::rgb(false, false, false);
	}else{
		ledIndicator(true);
	}
	if(wait < 5) wait = 5;

	if(hal::sleep(wait) == hal::Wake::Button){
		swallowUntilRelease = true;
		for(int i = 0; i < 4; i++){
			buttons[i].raw = buttons[i].down = hal::button((hal::Button) i);
			buttons[i].changed = hal::millis();
		}
		wakeScreen();
	}else if(persistent && !lcdOn && localTime().minute != ambientMinute){
		renderAmbient();
	}
}

void render(uint32_t dt){
	Activity* top = stack.empty() ? nullptr : stack.back().get();
	if(top){
		const uint16_t bg = top->background();
		work.fillScreen(bg);
		top->onDraw(work);
		if(top->showStatusBar()) drawStatusBar(work, bg);
	}

	if(transition != Transition::None){
		float p = (hal::millis() - transitionStart) / (float) TransitionMs;
		if(p >= 1){
			p = 1;
			transition = Transition::None;
		}
		const float e = easeOut(p);
		frame.fillScreen(col::Bg);
		switch(transition == Transition::None ? Transition::Fade : transition){
			case Transition::Push:
				snapshot.pushSprite(&frame, -(int) (e * 40), 0);
				work.pushSprite(&frame, (int) ((1 - e) * W), 0);
				break;
			case Transition::Pop:
				work.pushSprite(&frame, -(int) ((1 - e) * 40), 0);
				snapshot.pushSprite(&frame, (int) (e * W), 0);
				break;
			default: {
				auto* dst = (uint16_t*) frame.getBuffer();
				auto* a = (const uint16_t*) snapshot.getBuffer();
				auto* b = (const uint16_t*) work.getBuffer();
				const uint8_t t = (uint8_t) (e * 255);
				for(int i = 0; i < W * H; i++){
					const uint16_t va = (a[i] >> 8) | (a[i] << 8);
					const uint16_t vb = (b[i] >> 8) | (b[i] << 8);
					const uint16_t v = blend(va, vb, t);
					dst[i] = (v >> 8) | (v << 8);
				}
				break;
			}
		}
	}else{
		memcpy(frame.getBuffer(), work.getBuffer(), W * H * 2);
	}

	drawShade(frame);
	drawHeadsUp(frame);
	drawPowerMenu(frame);
	drawToast(frame);

	if(cfg.showFps){
		char buf[12];
		snprintf(buf, sizeof(buf), "%.0f fps", fps);
		frame.fillRect(0, H - 10, 34, 10, col::Bg);
		text(frame, buf, 1, H - 1, Small, col::AndroidGreen, textdatum_t::bottom_left);
	}

	hal::present(frame);
}

}

// ---- Public API ----------------------------------------------------------------------

uint16_t Activity::background() const { return col::Bg; }

void registerApp(const AppInfo& app){
	if(numApps < MaxApps) apps[numApps++] = app;
}

int appCount(){ return numApps; }

const AppInfo& app(int i){ return apps[i]; }

bool appAvailable(const AppInfo& a){
	const auto& f = hal::features();
	if((a.needs & NeedsBacklight) && !f.backlight) return false;
	if((a.needs & NeedsFastDisplay) && f.persistentDisplay) return false;
	return true;
}

const AppInfo* findApp(const char* id){
	if(!id) return nullptr;
	for(int i = 0; i < numApps; i++){
		if(strcmp(apps[i].id, id) == 0) return &apps[i];
	}
	return nullptr;
}

void startActivity(const Intent& intent){
	pendingOp = PendingOp::Start;
	pendingIntent = intent;
}

void replaceActivity(const Intent& intent){
	pendingOp = PendingOp::Replace;
	pendingIntent = intent;
}

void finish(){
	pendingOp = PendingOp::Finish;
}

void goHome(){
	shadeOpen = false;
	pendingOp = PendingOp::Home;
}

Activity* current(){
	return stack.empty() ? nullptr : stack.back().get();
}

const char* currentId(){
	return stackIds.empty() ? nullptr : stackIds.back();
}

int stackDepth(){
	return (int) stack.size();
}

void notify(const char* appId, const char* title, const char* text_, bool showHeadsUp){
	for(int i = MaxNotifs - 1; i > 0; i--) notifs[i] = notifs[i - 1];
	Notification& n = notifs[0];
	n.appId = appId;
	snprintf(n.title, sizeof(n.title), "%s", title);
	snprintf(n.text, sizeof(n.text), "%s", text_);
	n.when = hal::now();
	n.unread = true;
	if(notifCount < MaxNotifs) notifCount++;
	if(headsUp >= 0) headsUp = 0;

	if(cfg.dnd) return;
	if(showHeadsUp){
		headsUp = 0;
		headsUpUntil = hal::millis() + 3500;
	}
	play(Sound::Notify);
	wakeScreen();
}

int notificationCount(){ return notifCount; }

const Notification& notification(int i){ return notifs[i]; }

void dismissNotification(int i){
	if(i < 0 || i >= notifCount) return;
	for(int j = i; j < notifCount - 1; j++) notifs[j] = notifs[j + 1];
	notifCount--;
	headsUp = -1;
}

void clearNotifications(){
	notifCount = 0;
	headsUp = -1;
}

int unreadCount(){
	int n = 0;
	for(int i = 0; i < notifCount; i++) n += notifs[i].unread;
	return n;
}

void markAllRead(){
	for(int i = 0; i < notifCount; i++) notifs[i].unread = false;
}

void toast(const char* s){
	snprintf(toastText, sizeof(toastText), "%s", s);
	toastUntil = hal::millis() + 1800;
}

void beep(uint16_t freq, uint16_t ms){
	if(!cfg.sound || noteCount >= 16) return;
	notes[(noteHead + noteCount) % 16] = { freq, ms };
	noteCount++;
}

void play(Sound s){
	switch(s){
		case Sound::Click:
			beep(2200, 12);
			break;
		case Sound::Notify:
			beep(1319, 70);
			beep(0, 40);
			beep(1760, 110);
			break;
		case Sound::Alarm:
			for(int i = 0; i < 3; i++){
				beep(2093, 90);
				beep(0, 60);
			}
			beep(0, 300);
			break;
		case Sound::Success:
			beep(1047, 70);
			beep(1319, 70);
			beep(1568, 120);
			break;
		case Sound::Error:
			beep(330, 150);
			beep(262, 220);
			break;
		case Sound::Boot:
			beep(784, 90);
			beep(1047, 90);
			beep(1319, 90);
			beep(1568, 200);
			break;
	}
}

void wakeScreen(){
	userActivity();
	if(lcdOn) return;
	lcdOn = true;
	ambientMode = false;
	hal::rgb(false, false, false);
	hal::displayPower(true);
	if(!stack.empty()) stack.back()->onResume();
}

void sleepScreen(){
	if(!lcdOn) return;
	lcdOn = false;
	shadeOpen = false;
	shadeY = -H;
	powerMenu = false;
	headsUp = -1;
	headsUpY = -40;
	shownBrightness = 0;
	hal::backlight(0);
	hal::displayPower(false);
	noteCount = 0;
	hal::tone(0);
	if(!stack.empty()) stack.back()->onPause();
	// Wear OS returns to the watch face after the screen turns off.
	if(stack.size() > 1 && !stack.back()->keepScreenOn()){
		while(stack.size() > 1) destroyTop();
		transition = Transition::None;
	}
	if(hal::features().persistentDisplay){
		ambientMode = true;
		renderAmbient();
	}
}

bool ambient(){ return ambientMode; }

void userActivity(){
	lastActivity = hal::millis();
}

bool screenOn(){ return lcdOn; }

void addService(Service* s){ services.push_back(s); }

Settings& settings(){ return cfg; }

void saveSettings(){
	hal::prefSet("bright", cfg.brightness);
	hal::prefSet("timeout", cfg.screenTimeout);
	hal::prefSet("sound", cfg.sound);
	hal::prefSet("dnd", cfg.dnd);
	hal::prefSet("face", cfg.watchFace);
	hal::prefSet("accent", cfg.accent);
	hal::prefSet("goal", cfg.stepGoal);
	hal::prefSet("24h", cfg.use24h);
	hal::prefSet("dev", cfg.developer);
	hal::prefSet("fps", cfg.showFps);
	hal::prefSet("paper", cfg.paper);
}

DateTime localTime(){
	const time_t t = hal::now();
	tm p;
	gmtime_r(&t, &p);
	return { p.tm_year + 1900, p.tm_mon + 1, p.tm_mday, p.tm_hour, p.tm_min, p.tm_sec, p.tm_wday };
}

time_t makeTime(const DateTime& dt){
	// days_from_civil (Howard Hinnant), independent of the C library's TZ handling
	int y = dt.year - (dt.month <= 2);
	const int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned) (y - era * 400);
	const unsigned doy = (153 * (dt.month + (dt.month > 2 ? -3 : 9)) + 2) / 5 + dt.day - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const long days = era * 146097L + (long) doe - 719468L;
	return (time_t) days * 86400 + dt.hour * 3600 + dt.minute * 60 + dt.second;
}

const char* weekdayName(int wd, bool shortName){
	static const char* s[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char* l[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
	return (shortName ? s : l)[wd % 7];
}

const char* monthName(int m, bool shortName){
	static const char* s[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const char* l[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
	return (shortName ? s : l)[(m + 11) % 12];
}

uint32_t stepsToday(){
	return stepCarry + (stepLastHw - stepBase);
}

uint32_t stepsOnDay(int daysAgo){
	if(daysAgo <= 0) return stepsToday();
	if(daysAgo > 6) return 0;
	return stepHistory[daysAgo];
}

void begin(){
	hal::begin();

	cfg.brightness = hal::prefGet("bright", 70);
	cfg.screenTimeout = hal::prefGet("timeout", 15);
	cfg.sound = hal::prefGet("sound", 1);
	cfg.dnd = hal::prefGet("dnd", 0);
	cfg.watchFace = hal::prefGet("face", 0);
	cfg.accent = hal::prefGet("accent", 0);
	cfg.stepGoal = hal::prefGet("goal", 8000);
	cfg.use24h = hal::prefGet("24h", 1);
	cfg.developer = hal::prefGet("dev", 0);
	cfg.showFps = hal::prefGet("fps", 0);
	cfg.paper = hal::prefGet("paper", 1);
	hal::setPaperMode(cfg.paper);

	for(Canvas* c : { &frame, &work, &snapshot }){
		c->setColorDepth(16);
		c->createSprite(W, H);
		c->fillScreen(col::Bg);
	}

	stepsLoad();

	registerApps();

	pushActivity({ "boot" });
	lastFrame = lastActivity = hal::millis();
}

void tick(){
	const uint32_t now = hal::millis();
	uint32_t dt = now - lastFrame;
	if(dt > 100) dt = 100;
	lastFrame = now;
	if(dt > 0) fps = fps * 0.9f + (1000.0f / dt) * 0.1f;

	pollInput();
	soundUpdate();
	stepsPoll();
	for(auto* s : services) s->poll();
	applyPending();

	if(!lcdOn){
		sleepStep();
		lastFrame = hal::millis();
		return;
	}

	Activity* top = current();
	const bool hold = (top && top->keepScreenOn()) || powerMenu;
	if(!hold && hal::millis() - lastActivity > cfg.screenTimeout * 1000u){
		sleepScreen();
		return;
	}

	// Overlay animations
	shadeY = approach(shadeY, shadeOpen ? 0 : -H, dt, 16);
	if(headsUp >= 0 && now > headsUpUntil) headsUp = -1;
	headsUpY = approach(headsUpY, headsUp >= 0 ? 4 : -40, dt, 14);
	powerAnim = approach(powerAnim, powerMenu ? 1 : 0, dt, 16);

	if(top) top->onUpdate(dt);
	applyPending();
	render(dt);
	applyBrightness(dt);

	// ~40 fps cap leaves time for the CPU to idle.
	const uint32_t spent = hal::millis() - now;
	if(spent < 25) hal::idle(25 - spent);
}

}

// Lets the launcher open the quick settings shade.
namespace os::systemui {
void openShade(){
	shadeOpen = true;
	shadeFocus = 0;
}
}
