// Boot animation: bouncing dots that settle into the StarDroid wordmark,
// with the light sweep Android boot animations are known for.
#include "Apps.h"
#include <cmath>

using namespace ui;

namespace {

class BootAnimation : public os::Activity {
public:
	void onCreate(const os::Intent&) override { start = hal::millis(); }
	bool showStatusBar() const override { return false; }
	bool keepScreenOn() const override { return true; }

	void onUpdate(uint32_t) override {
		const uint32_t t = hal::millis() - start;
		if(t > 1300 && !chimed){
			chimed = true;
			os::play(os::Sound::Boot);
		}
		if(t > 3000 && !done){
			done = true;
			os::replaceActivity({ hal::prefGet("setup", 0) ? "launcher" : "setup" });
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		return true; // nothing interrupts the boot animation
	}

	void onDraw(os::Canvas& c) override {
		// E-paper can't animate: show the finished logo straight away.
		const uint32_t t = mono() ? 2900 : hal::millis() - start;
		const uint16_t colors[] = { col::Blue, col::Red, col::Yellow, col::AndroidGreen };
		const int cy = os::H / 2;

		if(t < 1300){
			// Four dots doing a wave
			for(int i = 0; i < 4; i++){
				const float phase = t / 180.0f - i * 0.7f;
				const float bounce = phase > 0 ? fabsf(sinf(phase)) * expf(-phase * 0.12f) : 0;
				const int x = 37 + i * 18;
				const int y = cy - (int) (bounce * 18);
				c.fillSmoothCircle(x, y, 5, colors[i]);
			}
			return;
		}

		// Dots slide together into the bugdroid
		const float m = fminf(1.0f, (t - 1300) / 350.0f);
		const float e = 1 - (1 - m) * (1 - m);
		if(m < 1){
			for(int i = 0; i < 4; i++){
				const int x0 = 37 + i * 18;
				const int x = (int) (x0 + (64 - x0) * e);
				c.fillSmoothCircle(x, cy - (int) (e * 16), (int) (5 + e * 3), colors[i]);
			}
			return;
		}

		const float appear = fminf(1.0f, (t - 1650) / 250.0f);
		const uint16_t green = blend(col::Bg, col::AndroidGreen, (uint8_t) (appear * 255));
		// Bugdroid head, scaled up
		c.fillArc(64, cy - 6, 0, 18, 180, 360, green);
		c.fillCircle(57, cy - 14, 2, col::Bg);
		c.fillCircle(71, cy - 14, 2, col::Bg);
		c.drawWideLine(53, cy - 21, 49, cy - 28, 1.2f, green);
		c.drawWideLine(75, cy - 21, 79, cy - 28, 1.2f, green);

		const uint16_t dim = blend(col::Bg, col::TextFaint, (uint8_t) (appear * 255));
		text(c, "stardroid", 64, cy + 14, &fonts::Roboto_Thin_24, dim, textdatum_t::middle_center);

		// Light sweep across the wordmark
		const float sweep = (t - 1650) / 900.0f;
		if(sweep > 0 && sweep < 1.4f){
			const int x = (int) (10 + sweep * 110);
			c.setClipRect(x - 12, cy, 24, 30);
			text(c, "stardroid", 64, cy + 14, &fonts::Roboto_Thin_24, col::Text, textdatum_t::middle_center);
			c.setClipRect(x - 5, cy, 10, 30);
			text(c, "stardroid", 64, cy + 14, &fonts::Roboto_Thin_24, col::AndroidGreen, textdatum_t::middle_center);
			c.clearClipRect();
		}else if(sweep >= 1.4f){
			text(c, "stardroid", 64, cy + 14, &fonts::Roboto_Thin_24, col::Text, textdatum_t::middle_center);
		}
		text(c, hal::deviceName(), 64, os::H - 6, Small, dim, textdatum_t::bottom_center);
	}

private:
	uint32_t start = 0;
	bool chimed = false;
	bool done = false;
};

}

const os::AppInfo apps::Boot = { "boot", "Boot", col::AndroidGreen, nullptr, os::make<BootAnimation>, false };
