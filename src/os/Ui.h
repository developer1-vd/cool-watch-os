#pragma once
#include "Os.h"

// Material-ish widgets and vector icons shared by system UI and apps.
namespace ui {

using os::Canvas;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b){
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

namespace col {
	constexpr uint16_t Bg = rgb(0, 0, 0);
	constexpr uint16_t Surface = rgb(30, 32, 36);
	constexpr uint16_t Surface2 = rgb(50, 53, 59);
	constexpr uint16_t Text = rgb(240, 242, 245);
	constexpr uint16_t TextDim = rgb(160, 166, 175);
	constexpr uint16_t TextFaint = rgb(95, 100, 108);
	constexpr uint16_t Mint = rgb(61, 220, 132);
	constexpr uint16_t Red = rgb(242, 107, 99);
	constexpr uint16_t Orange = rgb(255, 167, 38);
	constexpr uint16_t Yellow = rgb(255, 213, 79);
	constexpr uint16_t Blue = rgb(138, 180, 248);
	constexpr uint16_t Purple = rgb(208, 188, 255);
	constexpr uint16_t Pink = rgb(244, 143, 177);
	constexpr uint16_t Teal = rgb(77, 208, 225);
}

struct Accent {
	const char* name;
	uint16_t primary;   // bright tone for highlights
	uint16_t container; // dark tone for filled surfaces
};

int accentCount();
const Accent& accentAt(int i);
const Accent& accent(); // current

uint16_t blend(uint16_t a, uint16_t b, uint8_t t); // t: 0 = a, 255 = b

// True on black & white e-paper. Widgets swap dark fills for outlines and
// show focus by inverting, and animations jump straight to their end.
bool mono();
bool dark(uint16_t color); // a background-like colour: vanishes on e-paper
uint16_t faint();          // dim decoration colour that survives e-paper

// Common fonts
extern const lgfx::IFont* Small;  // ~9 px, UI captions
extern const lgfx::IFont* Body;   // ~12 px
extern const lgfx::IFont* Title;  // ~18 px
extern const lgfx::IFont* Huge;   // ~40 px digits

void text(Canvas& c, const char* s, int x, int y, const lgfx::IFont* font, uint16_t color,
		  textdatum_t datum = textdatum_t::top_left);
int textWidth(Canvas& c, const char* s, const lgfx::IFont* font);

void ring(Canvas& c, int cx, int cy, int r, int thick, float frac, uint16_t fg, uint16_t bg);
void toggle(Canvas& c, int x, int y, bool on, bool inverted = false);
void header(Canvas& c, const char* title, int y = os::StatusBarH + 1);
void hint(Canvas& c, const char* text); // bottom button hint
void card(Canvas& c, int x, int y, int w, int h, uint16_t color, int r = 8);

// Smoothly animates a value towards a target (call once per frame).
float approach(float cur, float target, uint32_t dtMs, float speed = 14.0f);

// Wear OS style vertical list: the focused row is centered and highlighted.
class ListView {
public:
	struct Row {
		const char* label;
		const char* detail; // optional second line or value
		uint16_t color;      // icon bubble colour
		os::IconFn icon;     // optional
		int8_t toggle;       // -1 none, 0 off, 1 on
	};
	using RowFn = Row (*)(void* ctx, int i);

	void set(int count, RowFn fn, void* ctx){ this->count = count; this->fn = fn; this->ctx = ctx; }
	void update(uint32_t dt);
	void draw(Canvas& c, int top = os::StatusBarH, int bottom = os::H);
	bool onKey(const os::KeyEvent& e); // handles Up/Down
	int selected = 0;
	int rowH = 26;

private:
	int count = 0;
	RowFn fn = nullptr;
	void* ctx = nullptr;
	float scroll = 0;
};

// Multi-field numeric picker (times, dates, durations).
class Picker {
public:
	struct Field {
		int value, min, max;
		const char* suffix; // separator drawn after the field
	};
	void set(std::initializer_list<Field> fields);
	// Returns true when the user confirmed the last field.
	bool onKey(const os::KeyEvent& e, bool& cancelled);
	void draw(Canvas& c, int cy, const lgfx::IFont* font = nullptr);
	int value(int i) const { return fields[i].value; }
	void setValue(int i, int v){ fields[i].value = v; }
	int field = 0;

private:
	Field fields[4];
	int count = 0;
};

// ---- Icons (centered at cx, cy, about 16 px) ----
namespace icon {
	void clock(Canvas& c, int cx, int cy, uint16_t fg);
	void stopwatch(Canvas& c, int cx, int cy, uint16_t fg);
	void timer(Canvas& c, int cx, int cy, uint16_t fg);
	void alarm(Canvas& c, int cx, int cy, uint16_t fg);
	void steps(Canvas& c, int cx, int cy, uint16_t fg);
	void heart(Canvas& c, int cx, int cy, uint16_t fg);
	void gear(Canvas& c, int cx, int cy, uint16_t fg);
	void flashlight(Canvas& c, int cx, int cy, uint16_t fg);
	void level(Canvas& c, int cx, int cy, uint16_t fg);
	void snake(Canvas& c, int cx, int cy, uint16_t fg);
	void runner(Canvas& c, int cx, int cy, uint16_t fg);
	void calendar(Canvas& c, int cx, int cy, uint16_t fg);
	void info(Canvas& c, int cx, int cy, uint16_t fg);
	void battery(Canvas& c, int cx, int cy, uint16_t fg);
	void sound(Canvas& c, int cx, int cy, uint16_t fg);
	void dnd(Canvas& c, int cx, int cy, uint16_t fg);
	void sun(Canvas& c, int cx, int cy, uint16_t fg);
	void power(Canvas& c, int cx, int cy, uint16_t fg);
	void restart(Canvas& c, int cx, int cy, uint16_t fg);
	void palette(Canvas& c, int cx, int cy, uint16_t fg);
	void logo(Canvas& c, int cx, int cy, uint16_t fg);
	void bell(Canvas& c, int cx, int cy, uint16_t fg);
	void code(Canvas& c, int cx, int cy, uint16_t fg);
	void watch(Canvas& c, int cx, int cy, uint16_t fg);
	void check(Canvas& c, int cx, int cy, uint16_t fg);
	void apps(Canvas& c, int cx, int cy, uint16_t fg);
}

// The Cool Watch OS logo: a watch outline with a four-point sparkle. s is half
// the width of the watch body; bg fills the face.
void drawLogo(Canvas& c, int cx, int cy, int s, uint16_t fg, uint16_t bg);
void sparkle(Canvas& c, int cx, int cy, int rx, int ry, uint16_t color);

// Small battery glyph for status bars (w = 14)
void batteryGlyph(Canvas& c, int x, int y, const hal::Battery& b, uint16_t fg);

}
