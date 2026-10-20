#include "Ui.h"
#include "hal/Mono.h"
#include <cmath>
#include <cstdio>

namespace ui {

const lgfx::IFont* Small = &fonts::DejaVu9;
const lgfx::IFont* Body = &fonts::Font2;
const lgfx::IFont* Title = &fonts::DejaVu18;
const lgfx::IFont* Huge = &fonts::DejaVu40;

namespace {

const Accent Accents[] = {
	{ "Mint", col::Mint, rgb(10, 70, 40) },
	{ "Ocean", col::Blue, rgb(22, 48, 92) },
	{ "Lavender", col::Purple, rgb(62, 44, 110) },
	{ "Coral", col::Red, rgb(100, 30, 26) },
	{ "Sunny", col::Yellow, rgb(90, 70, 0) },
	{ "Bubblegum", col::Pink, rgb(100, 28, 58) },
};

}

int accentCount(){ return sizeof(Accents) / sizeof(Accents[0]); }

const Accent& accentAt(int i){ return Accents[i % accentCount()]; }

const Accent& accent(){ return accentAt(os::settings().accent); }

uint16_t blend(uint16_t a, uint16_t b, uint8_t t){
	const int r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
	const int g = (((a >> 5) & 0x3F) * (255 - t) + ((b >> 5) & 0x3F) * t) / 255;
	const int bl = ((a & 0x1F) * (255 - t) + (b & 0x1F) * t) / 255;
	return (r << 11) | (g << 5) | bl;
}

bool mono(){
	return !hal::features().color;
}

bool dark(uint16_t color){
	// Surfaces and accent containers sit below 70; dim text is about 100.
	const int r = (color >> 11) * 255 / 31, g = ((color >> 5) & 0x3F) * 255 / 63, b = (color & 0x1F) * 255 / 31;
	return (r * 77 + g * 150 + b * 29) >> 8 < 80;
}

uint16_t faint(){
	return mono() ? col::Text : col::TextFaint;
}

void text(Canvas& c, const char* s, int x, int y, const lgfx::IFont* font, uint16_t color, textdatum_t datum){
	// On e-paper, grey text loses its thin strokes to the black/white
	// threshold, so draw all text at full contrast.
	if(mono()) color = dark(color) ? col::Bg : col::Text;
	c.setFont(font);
	c.setTextColor(color);
	c.setTextDatum(datum);
	c.drawString(s, x, y);
}

int textWidth(Canvas& c, const char* s, const lgfx::IFont* font){
	c.setFont(font);
	return c.textWidth(s);
}

void ring(Canvas& c, int cx, int cy, int r, int thick, float frac, uint16_t fg, uint16_t bg){
	if(frac < 0) frac = 0;
	c.fillArc(cx, cy, r - thick, r, 0, 360, bg);
	if(frac <= 0) return;
	if(frac >= 1){
		c.fillArc(cx, cy, r - thick, r, 0, 360, fg);
		return;
	}
	const float end = -90 + 360 * frac;
	c.fillArc(cx, cy, r - thick, r, -90, end, fg);
	// Rounded caps
	const float mid = r - thick / 2.0f;
	const float rad = thick / 2.0f;
	c.fillSmoothCircle(cx, cy - (int) mid, (int) rad, fg);
	const float a = end * (float) M_PI / 180.0f;
	c.fillSmoothCircle(cx + (int) lroundf(cosf(a) * mid), cy + (int) lroundf(sinf(a) * mid), (int) rad, fg);
}

void toggle(Canvas& c, int x, int y, bool on, bool inverted){
	if(mono()){
		// Outline pill; filled when on. "inverted" is for focused (white) rows.
		const uint16_t ink = inverted ? col::Bg : col::Text, paper = inverted ? col::Text : col::Bg;
		c.fillSmoothRoundRect(x, y, 22, 12, 6, on ? ink : paper);
		c.drawRoundRect(x, y, 22, 12, 6, ink);
		c.fillSmoothCircle(on ? x + 16 : x + 6, y + 6, 3, on ? paper : ink);
		return;
	}
	const auto& a = accent();
	c.fillSmoothRoundRect(x, y, 22, 12, 6, on ? a.primary : col::Surface2);
	c.fillSmoothCircle(on ? x + 16 : x + 6, y + 6, on ? 4 : 3, on ? a.container : col::TextDim);
}

void header(Canvas& c, const char* title, int y){
	text(c, title, os::W / 2, y, Body, accent().primary, textdatum_t::top_center);
}

void hint(Canvas& c, const char* s){
	text(c, s, os::W / 2, os::H - 2, Small, col::TextFaint, textdatum_t::bottom_center);
}

void card(Canvas& c, int x, int y, int w, int h, uint16_t color, int r){
	c.fillSmoothRoundRect(x, y, w, h, r, color);
	if(mono() && dark(color)) c.drawRoundRect(x, y, w, h, r, col::Text);
}

float approach(float cur, float target, uint32_t dtMs, float speed){
	if(mono()) return target; // e-paper can't show in-between frames
	const float k = 1.0f - expf(-speed * dtMs / 1000.0f);
	cur += (target - cur) * k;
	if(fabsf(target - cur) < 0.01f) cur = target;
	return cur;
}

// ---- ListView ----------------------------------------------------------------

void ListView::update(uint32_t dt){
	scroll = approach(scroll, (float) selected, dt, 16);
}

void ListView::draw(Canvas& c, int top, int bottom){
	if(!fn) return;
	const int center = (top + bottom) / 2;
	const auto& a = accent();
	c.setClipRect(0, top, os::W, bottom - top);
	for(int i = 0; i < count; i++){
		const float off = (i - scroll) * rowH;
		const int y = center + (int) lroundf(off) - rowH / 2;
		if(y + rowH < top || y > bottom) continue;

		// Rows shrink towards the edges, like a curved Wear OS list.
		const float dist = fabsf(off) / (os::H / 2.0f);
		const int inset = 4 + (int) (dist * dist * 14);
		const Row row = fn(ctx, i);
		const bool focus = i == selected;
		// On e-paper the focused row is drawn inverted (white, black text).
		const bool inverted = focus && mono();

		if(inverted) c.fillSmoothRoundRect(inset, y + 1, os::W - inset * 2, rowH - 2, (rowH - 2) / 2, col::Text);
		else card(c, inset, y + 1, os::W - inset * 2, rowH - 2, focus ? a.container : col::Surface, (rowH - 2) / 2);

		int tx = inset + 9;
		if(row.icon){
			const int ix = inset + 13, iy = y + rowH / 2;
			c.fillSmoothCircle(ix, iy, 9, inverted ? col::Bg : row.color);
			row.icon(c, ix, iy, inverted ? col::Text : col::Bg);
			tx = inset + 26;
		}
		const uint16_t fg = inverted ? col::Bg : (focus ? col::Text : col::TextDim);
		const uint16_t detailColor = inverted ? col::Bg : (focus ? a.primary : col::TextFaint);
		int right = os::W - inset - 6;
		if(row.toggle >= 0){
			toggle(c, right - 22, y + rowH / 2 - 6, row.toggle, inverted);
			right -= 26;
		}
		if(row.detail && row.toggle < 0 && rowH < 30){
			text(c, row.detail, right, y + rowH / 2, Small, detailColor, textdatum_t::middle_right);
			right -= textWidth(c, row.detail, Small) + 4;
		}
		if(row.detail && rowH >= 30){
			text(c, row.label, tx, y + rowH / 2 - 1, Body, fg, textdatum_t::bottom_left);
			text(c, row.detail, tx, y + rowH / 2 + 1, Small, detailColor, textdatum_t::top_left);
		}else{
			c.setClipRect(tx, top, right - tx, bottom - top);
			text(c, row.label, tx, y + rowH / 2, Body, fg, textdatum_t::middle_left);
			c.setClipRect(0, top, os::W, bottom - top);
		}
	}
	c.clearClipRect();

	// Scroll indicator
	if(count > 1){
		const int trackTop = top + 8, trackH = bottom - top - 16;
		const int thumb = trackH / count < 6 ? 6 : trackH / count;
		const int ty = trackTop + (int) ((trackH - thumb) * scroll / (count - 1));
		c.fillRoundRect(os::W - 3, ty, 2, thumb, 1, faint());
	}
}

bool ListView::onKey(const os::KeyEvent& e){
	if(e.action != os::KeyAction::Down && e.action != os::KeyAction::Repeat) return false;
	if(e.key == os::Key::Up){
		selected = selected > 0 ? selected - 1 : count - 1;
		return true;
	}
	if(e.key == os::Key::Down){
		selected = selected < count - 1 ? selected + 1 : 0;
		return true;
	}
	return false;
}

// ---- Picker --------------------------------------------------------------------

void Picker::set(std::initializer_list<Field> list){
	count = 0;
	for(auto& f : list){
		if(count < 4) fields[count++] = f;
	}
	field = 0;
}

bool Picker::onKey(const os::KeyEvent& e, bool& cancelled){
	cancelled = false;
	auto& f = fields[field];
	const bool press = e.action == os::KeyAction::Down || e.action == os::KeyAction::Repeat;
	if(press && e.key == os::Key::Up){
		f.value = f.value >= f.max ? f.min : f.value + 1;
	}else if(press && e.key == os::Key::Down){
		f.value = f.value <= f.min ? f.max : f.value - 1;
	}else if(e.action == os::KeyAction::Click && e.key == os::Key::Select){
		if(field == count - 1) return true;
		field++;
	}else if(e.action == os::KeyAction::Click && e.key == os::Key::Back){
		if(field == 0) cancelled = true;
		else field--;
	}
	return false;
}

void Picker::draw(Canvas& c, int cy, const lgfx::IFont* font){
	if(!font) font = Title;
	char buf[8];
	int total = 0;
	int widths[4], sepW[4];
	for(int i = 0; i < count; i++){
		snprintf(buf, sizeof(buf), fields[i].max >= 1000 ? "%04d" : "%02d", fields[i].value);
		widths[i] = textWidth(c, buf, font) + 8;
		sepW[i] = fields[i].suffix ? textWidth(c, fields[i].suffix, font) + 2 : 0;
		total += widths[i] + sepW[i];
	}
	int x = (os::W - total) / 2;
	const auto& a = accent();
	for(int i = 0; i < count; i++){
		snprintf(buf, sizeof(buf), fields[i].max >= 1000 ? "%04d" : "%02d", fields[i].value);
		const bool active = i == field;
		c.setFont(font);
		const int fh = c.fontHeight();
		if(active){
			c.fillSmoothRoundRect(x, cy - fh / 2 - 4, widths[i], fh + 8, 6, mono() ? col::Text : a.container);
			c.fillTriangle(x + widths[i] / 2 - 4, cy - fh / 2 - 7, x + widths[i] / 2 + 4, cy - fh / 2 - 7, x + widths[i] / 2, cy - fh / 2 - 11, a.primary);
			c.fillTriangle(x + widths[i] / 2 - 4, cy + fh / 2 + 7, x + widths[i] / 2 + 4, cy + fh / 2 + 7, x + widths[i] / 2, cy + fh / 2 + 11, a.primary);
		}
		text(c, buf, x + widths[i] / 2, cy, font, active ? (mono() ? col::Bg : col::Text) : col::TextDim, textdatum_t::middle_center);
		x += widths[i];
		if(fields[i].suffix){
			text(c, fields[i].suffix, x + sepW[i] / 2, cy, font, col::TextFaint, textdatum_t::middle_center);
			x += sepW[i];
		}
	}
}

// ---- Icons ---------------------------------------------------------------------

namespace icon {

void clock(Canvas& c, int x, int y, uint16_t fg){
	c.drawCircle(x, y, 6, fg);
	c.drawCircle(x, y, 7, fg);
	c.drawLine(x, y, x, y - 4, fg);
	c.drawLine(x, y, x + 3, y + 1, fg);
}

void stopwatch(Canvas& c, int x, int y, uint16_t fg){
	c.drawCircle(x, y + 1, 6, fg);
	c.drawCircle(x, y + 1, 5, fg);
	c.fillRect(x - 2, y - 8, 5, 2, fg);
	c.drawLine(x, y + 1, x + 2, y - 2, fg);
	c.drawLine(x + 5, y - 5, x + 6, y - 6, fg);
}

void timer(Canvas& c, int x, int y, uint16_t fg){
	c.fillRect(x - 5, y - 7, 11, 2, fg);
	c.fillRect(x - 5, y + 6, 11, 2, fg);
	c.fillTriangle(x - 4, y - 5, x + 4, y - 5, x, y, fg);
	c.drawTriangle(x - 4, y + 5, x + 4, y + 5, x, y, fg);
	c.fillTriangle(x - 2, y + 5, x + 2, y + 5, x, y + 3, fg);
}

void bell(Canvas& c, int x, int y, uint16_t fg){
	c.fillCircle(x, y - 1, 5, fg);
	c.fillRect(x - 5, y - 1, 11, 5, fg);
	c.fillRect(x - 7, y + 4, 15, 2, fg);
	c.fillCircle(x, y + 7, 2, fg);
	c.fillRect(x - 1, y - 8, 3, 2, fg);
}

void alarm(Canvas& c, int x, int y, uint16_t fg){
	c.drawCircle(x, y + 1, 6, fg);
	c.drawCircle(x, y + 1, 5, fg);
	c.drawLine(x, y + 1, x, y - 2, fg);
	c.drawLine(x, y + 1, x + 2, y + 2, fg);
	c.fillTriangle(x - 8, y - 4, x - 4, y - 8, x - 8, y - 8, fg);
	c.fillTriangle(x + 8, y - 4, x + 4, y - 8, x + 8, y - 8, fg);
}

void steps(Canvas& c, int x, int y, uint16_t fg){
	c.fillEllipse(x - 3, y - 2, 3, 5, fg);
	c.fillEllipse(x - 3, y + 6, 2, 2, fg);
	c.fillEllipse(x + 4, y - 5, 3, 4, fg);
	c.fillEllipse(x + 4, y + 2, 2, 2, fg);
}

void heart(Canvas& c, int x, int y, uint16_t fg){
	c.fillCircle(x - 3, y - 2, 4, fg);
	c.fillCircle(x + 3, y - 2, 4, fg);
	c.fillTriangle(x - 7, y, x + 7, y, x, y + 7, fg);
}

void gear(Canvas& c, int x, int y, uint16_t fg){
	for(int i = 0; i < 8; i++){
		const float a = i * (float) M_PI / 4;
		c.drawWideLine(x, y, x + (int) lroundf(cosf(a) * 7), y + (int) lroundf(sinf(a) * 7), 1.5f, fg);
	}
	c.fillCircle(x, y, 5, fg);
	c.fillCircle(x, y, 2, col::Bg == fg ? col::Text : col::Bg);
}

void flashlight(Canvas& c, int x, int y, uint16_t fg){
	c.fillTriangle(x - 6, y - 7, x + 6, y - 7, x, y - 1, fg);
	c.fillRect(x - 6, y - 8, 13, 3, fg);
	c.fillRect(x - 2, y - 2, 5, 10, fg);
}

void level(Canvas& c, int x, int y, uint16_t fg){
	c.drawRoundRect(x - 8, y - 4, 17, 9, 4, fg);
	c.fillCircle(x + 1, y, 2, fg);
	c.drawFastVLine(x - 3, y - 4, 9, fg);
	c.drawFastVLine(x + 5, y - 4, 9, fg);
}

void snake(Canvas& c, int x, int y, uint16_t fg){
	c.fillRect(x - 7, y + 3, 9, 3, fg);
	c.fillRect(x - 1, y - 3, 3, 9, fg);
	c.fillRect(x - 1, y - 3, 8, 3, fg);
	c.fillRect(x + 4, y - 7, 3, 4, fg);
	c.fillCircle(x - 5, y - 5, 1, fg);
}

void runner(Canvas& c, int x, int y, uint16_t fg){
	// The sparkle mascot mid-stride
	sparkle(c, x, y - 2, 6, 6, fg);
	c.drawWideLine(x - 1, y + 2, x - 4, y + 7, 1, fg);
	c.drawWideLine(x + 1, y + 2, x + 4, y + 6, 1, fg);
}

void calendar(Canvas& c, int x, int y, uint16_t fg){
	c.drawRoundRect(x - 7, y - 5, 15, 13, 2, fg);
	c.fillRect(x - 7, y - 5, 15, 4, fg);
	c.fillRect(x - 4, y - 8, 2, 4, fg);
	c.fillRect(x + 3, y - 8, 2, 4, fg);
	c.fillRect(x - 4, y + 1, 3, 3, fg);
}

void info(Canvas& c, int x, int y, uint16_t fg){
	c.drawCircle(x, y, 7, fg);
	c.fillRect(x - 1, y - 1, 2, 5, fg);
	c.fillRect(x - 1, y - 4, 2, 2, fg);
}

void battery(Canvas& c, int x, int y, uint16_t fg){
	c.drawRoundRect(x - 4, y - 6, 9, 14, 2, fg);
	c.fillRect(x - 1, y - 8, 3, 2, fg);
	c.fillRect(x - 2, y - 1, 5, 7, fg);
}

void sound(Canvas& c, int x, int y, uint16_t fg){
	c.fillRect(x - 7, y - 2, 4, 5, fg);
	c.fillTriangle(x - 4, y, x + 1, y - 6, x + 1, y + 6, fg);
	c.drawArc(x + 1, y, 4, 4, -50, 50, fg);
	c.drawArc(x + 1, y, 7, 7, -50, 50, fg);
}

void dnd(Canvas& c, int x, int y, uint16_t fg){
	c.fillCircle(x, y, 7, fg);
	c.fillRect(x - 4, y - 1, 9, 3, fg == col::Bg ? col::Text : col::Bg);
}

void sun(Canvas& c, int x, int y, uint16_t fg){
	c.fillCircle(x, y, 3, fg);
	for(int i = 0; i < 8; i++){
		const float a = i * (float) M_PI / 4;
		c.drawLine(x + (int) lroundf(cosf(a) * 5), y + (int) lroundf(sinf(a) * 5),
				   x + (int) lroundf(cosf(a) * 7), y + (int) lroundf(sinf(a) * 7), fg);
	}
}

void power(Canvas& c, int x, int y, uint16_t fg){
	c.drawArc(x, y + 1, 6, 5, -60, 240, fg);
	c.fillRect(x - 1, y - 7, 2, 7, fg);
}

void restart(Canvas& c, int x, int y, uint16_t fg){
	c.drawArc(x, y, 6, 5, -30, 270, fg);
	c.fillTriangle(x + 2, y - 9, x + 2, y - 2, x + 7, y - 5, fg);
}

void palette(Canvas& c, int x, int y, uint16_t fg){
	c.fillCircle(x, y, 7, fg);
	const uint16_t bg = fg == col::Bg ? col::Text : col::Bg;
	c.fillCircle(x - 3, y - 3, 1, bg);
	c.fillCircle(x + 2, y - 4, 1, bg);
	c.fillCircle(x + 4, y, 1, bg);
	c.fillCircle(x - 1, y + 4, 2, bg);
}

void logo(Canvas& c, int x, int y, uint16_t fg){
	c.fillRect(x - 3, y - 8, 7, 3, fg);
	c.fillRect(x - 3, y + 6, 7, 3, fg);
	c.drawRoundRect(x - 6, y - 6, 13, 13, 4, fg);
	c.drawRoundRect(x - 5, y - 5, 11, 11, 3, fg);
	sparkle(c, x, y, 3, 3, fg);
}

void code(Canvas& c, int x, int y, uint16_t fg){
	c.drawWideLine(x - 3, y - 5, x - 7, y, 1, fg);
	c.drawWideLine(x - 7, y, x - 3, y + 5, 1, fg);
	c.drawWideLine(x + 3, y - 5, x + 7, y, 1, fg);
	c.drawWideLine(x + 7, y, x + 3, y + 5, 1, fg);
	c.drawLine(x + 1, y - 6, x - 1, y + 6, fg);
}

void watch(Canvas& c, int x, int y, uint16_t fg){
	c.fillRect(x - 3, y - 9, 7, 18, fg);
	c.fillSmoothRoundRect(x - 6, y - 6, 13, 13, 3, fg);
	const uint16_t bg = fg == col::Bg ? col::Text : col::Bg;
	c.fillRoundRect(x - 4, y - 4, 9, 9, 2, bg);
	c.drawLine(x, y, x, y - 3, fg);
	c.drawLine(x, y, x + 2, y, fg);
}

void check(Canvas& c, int x, int y, uint16_t fg){
	c.drawWideLine(x - 6, y, x - 2, y + 4, 1.5f, fg);
	c.drawWideLine(x - 2, y + 4, x + 6, y - 5, 1.5f, fg);
}

void apps(Canvas& c, int x, int y, uint16_t fg){
	for(int i = -1; i <= 1; i++){
		for(int j = -1; j <= 1; j++) c.fillCircle(x + i * 5, y + j * 5, 1, fg);
	}
}

}

void sparkle(Canvas& c, int cx, int cy, int rx, int ry, uint16_t color){
	// Four slim points meeting in the middle
	const int kx = rx * 3 / 10 + 1, ky = ry * 3 / 10 + 1;
	c.fillTriangle(cx, cy - ry, cx - kx, cy, cx + kx, cy, color);
	c.fillTriangle(cx, cy + ry, cx - kx, cy, cx + kx, cy, color);
	c.fillTriangle(cx - rx, cy, cx, cy - ky, cx, cy + ky, color);
	c.fillTriangle(cx + rx, cy, cx, cy - ky, cx, cy + ky, color);
}

void drawLogo(Canvas& c, int cx, int cy, int s, uint16_t fg, uint16_t bg){
	const int strapW = s * 11 / 10, strapH = s / 2 + 1;
	c.fillSmoothRoundRect(cx - strapW / 2, cy - s - strapH + 2, strapW, strapH, 2, fg);
	c.fillSmoothRoundRect(cx - strapW / 2, cy + s - 2, strapW, strapH, 2, fg);
	const int r = s * 9 / 20, t = s / 6 > 2 ? s / 6 : 2;
	c.fillSmoothRoundRect(cx - s, cy - s, s * 2, s * 2, r, fg);
	c.fillSmoothRoundRect(cx - s + t, cy - s + t, (s - t) * 2, (s - t) * 2, r - t > 1 ? r - t : 1, bg);
	sparkle(c, cx, cy, s * 6 / 10, s * 6 / 10, fg);
}

void batteryGlyph(Canvas& c, int x, int y, const hal::Battery& b, uint16_t fg){
	c.drawRoundRect(x, y, 12, 7, 1, fg);
	c.fillRect(x + 12, y + 2, 2, 3, fg);
	const int w = (b.percent * 8 + 50) / 100;
	uint16_t fill = b.percent <= 15 ? col::Red : fg;
	if(b.charging) fill = col::Mint;
	c.fillRect(x + 2, y + 2, w, 3, fill);
}

}
