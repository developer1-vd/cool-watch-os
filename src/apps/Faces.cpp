// Watch faces
#include "Apps.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ui;

namespace {

int displayHour(int h){
	if(os::settings().use24h) return h;
	h %= 12;
	return h == 0 ? 12 : h;
}

float goalFrac(){
	const auto& s = os::settings();
	return s.stepGoal ? (float) os::stepsToday() / s.stepGoal : 0;
}

void unreadDot(os::Canvas& c, int y){
	if(os::unreadCount() > 0) c.fillSmoothCircle(64, y, 2, col::Text);
}

// Pixel: big stacked digits with a complication column.
void drawPixel(os::Canvas& c, const os::DateTime& t, float){
	const auto& a = accent();
	char buf[8];
	snprintf(buf, sizeof(buf), "%02d", displayHour(t.hour));
	text(c, buf, 8, 6, &fonts::DejaVu56, a.primary);
	snprintf(buf, sizeof(buf), "%02d", t.minute);
	text(c, buf, 8, 62, &fonts::DejaVu56, col::Text);

	// Complications
	const int x = 104;
	text(c, os::weekdayName(t.weekday), x, 12, Small, col::TextDim, textdatum_t::top_center);
	snprintf(buf, sizeof(buf), "%d", t.day);
	text(c, buf, x, 23, Body, col::Text, textdatum_t::top_center);

	ring(c, x, 62, 12, 3, goalFrac(), a.primary, col::Surface2);
	icon::steps(c, x, 62, col::TextDim);

	const auto b = hal::battery();
	ring(c, x, 98, 12, 3, b.percent / 100.0f, b.percent <= 15 ? col::Red : col::Text, col::Surface2);
	snprintf(buf, sizeof(buf), "%d", b.percent);
	text(c, buf, x, 98, Small, col::TextDim, textdatum_t::middle_center);
	unreadDot(c, 124);
}

// Analog: a classic dial with a smooth sweeping second hand.
void hand(os::Canvas& c, float angle, float len, float tail, float w0, float w1, uint16_t color){
	const float a = (angle - 90) * (float) M_PI / 180.0f;
	const float cs = cosf(a), sn = sinf(a);
	c.drawWedgeLine(64 - (int) (cs * tail), 64 - (int) (sn * tail), 64 + (int) lroundf(cs * len), 64 + (int) lroundf(sn * len), w0, w1, color);
}

void drawAnalog(os::Canvas& c, const os::DateTime& t, float secFrac){
	const auto& a = accent();
	c.fillSmoothCircle(64, 64, 62, col::Surface);
	for(int i = 0; i < 60; i++){
		const float ang = i * 6 * (float) M_PI / 180.0f;
		const bool major = i % 5 == 0;
		const float r0 = major ? 52 : 56, r1 = 59;
		c.drawWideLine(64 + (int) lroundf(sinf(ang) * r0), 64 - (int) lroundf(cosf(ang) * r0),
					   64 + (int) lroundf(sinf(ang) * r1), 64 - (int) lroundf(cosf(ang) * r1),
					   major ? 1.2f : 0.5f, major ? col::Text : faint());
	}
	text(c, "12", 64, 14, Body, col::Text, textdatum_t::top_center);
	text(c, "6", 64, 114, Body, col::Text, textdatum_t::bottom_center);
	text(c, "9", 14, 64, Body, col::Text, textdatum_t::middle_left);

	// Date window at 3 o'clock
	char buf[4];
	snprintf(buf, sizeof(buf), "%d", t.day);
	c.fillSmoothRoundRect(92, 57, 22, 14, 4, a.container);
	text(c, buf, 103, 64, Small, a.primary, textdatum_t::middle_center);

	text(c, os::Name, 64, 36, Small, col::TextFaint, textdatum_t::middle_center);

	// Step progress sub-dial
	ring(c, 64, 88, 10, 2, goalFrac(), a.primary, col::Surface2);
	icon::steps(c, 64, 88, col::TextDim);

	const float sec = t.second + secFrac;
	const float min = t.minute + sec / 60.0f;
	const float hr = (t.hour % 12) + min / 60.0f;
	hand(c, hr * 30, 32, 6, 3.0f, 2.0f, col::Text);
	hand(c, min * 6, 48, 6, 2.4f, 1.4f, col::Text);
	if(!os::ambient()) hand(c, sec * 6, 52, 12, 0.8f, 0.6f, a.primary);
	c.fillSmoothCircle(64, 64, 4, a.primary);
	c.fillSmoothCircle(64, 64, 1, col::Bg);
	unreadDot(c, 124);
}

// Neon: the seconds trace the edge of the square screen.
void drawNeon(os::Canvas& c, const os::DateTime& t, float secFrac){
	const auto& a = accent();
	const float p = os::ambient() ? 0 : (t.second + secFrac) / 60.0f;
	// Perimeter path starting at top centre, clockwise
	const int inset = 3;
	const int side = os::W - inset * 2;
	const float total = side * 4.0f;
	float remaining = p * total;
	const uint16_t dim = blend(col::Bg, a.primary, 50);
	c.drawRoundRect(inset, inset, side, side, 6, dim);
	struct Seg { int x0, y0, dx, dy; float len; } segs[] = {
		{ 64, inset, 1, 0, side / 2.0f },
		{ os::W - inset - 1, inset, 0, 1, (float) side },
		{ os::W - inset - 1, os::H - inset - 1, -1, 0, (float) side },
		{ inset, os::H - inset - 1, 0, -1, (float) side },
		{ inset, inset, 1, 0, side / 2.0f },
	};
	for(auto& s : segs){
		if(remaining <= 0) break;
		const float l = fminf(remaining, s.len);
		const int x1 = s.x0 + (int) (s.dx * l), y1 = s.y0 + (int) (s.dy * l);
		c.drawWideLine(s.x0, s.y0, x1, y1, 1.5f, a.primary);
		remaining -= l;
	}

	char buf[8];
	snprintf(buf, sizeof(buf), "%02d:%02d", displayHour(t.hour), t.minute);
	// Faux glow: draw offset copies in a dim tone underneath.
	for(int dx = -1; dx <= 1; dx += 2){
		text(c, buf, 64 + dx, 58, &fonts::Orbitron_Light_32, a.container, textdatum_t::middle_center);
		text(c, buf, 64, 58 + dx, &fonts::Orbitron_Light_32, a.container, textdatum_t::middle_center);
	}
	text(c, buf, 64, 58, &fonts::Orbitron_Light_32, a.primary, textdatum_t::middle_center);

	if(!os::ambient()){
		snprintf(buf, sizeof(buf), "%02d", t.second);
		text(c, buf, 64, 82, Body, col::TextDim, textdatum_t::middle_center);
	}
	char date[24];
	snprintf(date, sizeof(date), "%s %s %d", os::weekdayName(t.weekday), os::monthName(t.month), t.day);
	text(c, date, 64, 26, Small, col::TextDim, textdatum_t::middle_center);
	snprintf(date, sizeof(date), "%lu steps", (unsigned long) os::stepsToday());
	text(c, date, 64, 102, Small, col::TextFaint, textdatum_t::middle_center);
	unreadDot(c, 116);
}

// Rings: Apple-style activity rings for steps, battery and the minute.
void drawRings(os::Canvas& c, const os::DateTime& t, float secFrac){
	const auto& a = accent();
	const auto b = hal::battery();
	ring(c, 64, 58, 50, 8, goalFrac(), a.primary, blend(col::Bg, a.primary, 50));
	ring(c, 64, 58, 40, 8, b.percent / 100.0f, col::Teal, blend(col::Bg, col::Teal, 50));
	ring(c, 64, 58, 30, 8, os::ambient() ? 0 : (t.second + secFrac) / 60.0f, col::Orange, blend(col::Bg, col::Orange, 50));
	char buf[12];
	snprintf(buf, sizeof(buf), "%d:%02d", displayHour(t.hour), t.minute);
	text(c, buf, 64, 58, Body, col::Text, textdatum_t::middle_center);
	snprintf(buf, sizeof(buf), "%lu", (unsigned long) os::stepsToday());
	text(c, buf, 4, 125, Small, a.primary, textdatum_t::bottom_left);
	snprintf(buf, sizeof(buf), "%d%%", b.percent);
	text(c, buf, 124, 125, Small, col::Teal, textdatum_t::bottom_right);
	unreadDot(c, 122);
}

// Terminal: for the developers.
void drawTerminal(os::Canvas& c, const os::DateTime& t, float secFrac){
	const uint16_t green = mono() ? col::Text : col::AndroidGreen; // full contrast on e-paper
	const uint16_t dim = mono() ? green : blend(col::Bg, green, 120);
	// The 1 px bitmap font breaks up when scaled for e-paper; DejaVu doesn't.
	const lgfx::IFont* small = mono() ? (const lgfx::IFont*) &fonts::DejaVu9 : &fonts::Font0;
	c.setFont(small);
	c.setTextDatum(textdatum_t::top_left);
	char buf[32];
	int y = 4;
	auto line = [&](const char* s, uint16_t color){
		c.setTextColor(color);
		c.drawString(s, 4, y);
		y += 10;
	};
	line("watch:/ $ date", dim);
	snprintf(buf, sizeof(buf), "%s %s %02d %d", os::weekdayName(t.weekday), os::monthName(t.month), t.day, t.year);
	line(buf, green);

	c.setFont(&fonts::Font0); // chunky 3x pixels look right on both screens
	c.setTextSize(3);
	snprintf(buf, sizeof(buf), "%02d:%02d", displayHour(t.hour), t.minute);
	c.setTextColor(green);
	c.drawString(buf, 4, y + 2);
	c.setTextSize(1);
	c.setFont(small);
	if(!os::ambient()){
		snprintf(buf, sizeof(buf), ":%02d", t.second);
		c.drawString(buf, 98, y + 16);
	}
	y += 30;

	const auto b = hal::battery();
	line("$ dumpsys battery", dim);
	snprintf(buf, sizeof(buf), "level: %d%%%s", b.percent, b.charging ? " (AC)" : "");
	line(buf, green);
	line("$ pedometer", dim);
	snprintf(buf, sizeof(buf), "%lu/%u steps", (unsigned long) os::stepsToday(), os::settings().stepGoal);
	line(buf, green);
	snprintf(buf, sizeof(buf), "$ %s", os::unreadCount() ? "notif --unread" : "");
	line(buf, dim);
	if(secFrac < 0.5f && !os::ambient()) c.fillRect(4 + c.textWidth(buf) + 1, y - 10, 5, 8, green);
}

const apps::FaceInfo Faces[] = {
	{ "Pixel", drawPixel },
	{ "Analog", drawAnalog },
	{ "Neon", drawNeon },
	{ "Rings", drawRings },
	{ "Terminal", drawTerminal },
};

}

int apps::faceCount(){ return sizeof(Faces) / sizeof(Faces[0]); }

const apps::FaceInfo& apps::face(int i){ return Faces[i % faceCount()]; }
