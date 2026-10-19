// Desktop simulator hardware layer (LovyanGFX SDL backend).
#if defined(LGFX_SDL)

#include "Hal.h"
#include "SimControl.h"
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace {

// Panel_sdl's default key mapping drives these emulated GPIOs low.
constexpr uint8_t KeyGpio[] = { 36, 38, 37, 39 }; // Up, Down, Select, Alt

LGFX* lcd = nullptr;
bool isHeadless = false;
bool buttons[4] = {};
uint32_t virtualMs = 0;
uint32_t startEpoch = 0;
long timeOffset = 0;
bool sleeping = false;
uint8_t brightness = 0;
uint16_t frameCopy[128 * 128];
std::map<std::string, int32_t> prefs;

}

namespace sim {

void setHeadless(uint32_t epoch){
	isHeadless = true;
	startEpoch = epoch;
}

bool headless(){ return isHeadless; }

void advance(uint32_t ms){ virtualMs += ms; }

void setButton(hal::Button b, bool pressed){ buttons[(int) b] = pressed; }

const uint16_t* lastFrame(){ return frameCopy; }

bool asleep(){ return sleeping; }

}

namespace hal {

void begin(){
	if(isHeadless) return;
	lcd = new LGFX(128, 128, 4);
	lcd->init();
	lgfx::Panel_sdl::addKeyCodeMapping(SDLK_RETURN, KeyGpio[2]);
	lgfx::Panel_sdl::addKeyCodeMapping(SDLK_SPACE, KeyGpio[2]);
	lgfx::Panel_sdl::addKeyCodeMapping(SDLK_ESCAPE, KeyGpio[3]);
	lgfx::Panel_sdl::addKeyCodeMapping(SDLK_BACKSPACE, KeyGpio[3]);
	((lgfx::Panel_sdl*) lcd->panel())->setWindowTitle("StarDroid - Clockstar simulator");
}

void present(LGFX_Sprite& frame){
	memcpy(frameCopy, frame.getBuffer(), sizeof(frameCopy));
	if(!lcd) return;
	if(brightness == 0){
		lcd->fillScreen(TFT_BLACK);
		return;
	}
	frame.pushSprite(lcd, 0, 0);
}

void displayPower(bool on){
	if(!on && lcd) lcd->fillScreen(TFT_BLACK);
}

bool button(Button b){
	if(isHeadless) return buttons[(int) b];
	return !lgfx::gpio_in(KeyGpio[(int) b]);
}

uint32_t millis(){
	return isHeadless ? virtualMs : lgfx::millis();
}

void idle(uint32_t ms){
	if(!isHeadless) lgfx::delay(ms);
}

time_t now(){
	if(isHeadless) return startEpoch + virtualMs / 1000 + timeOffset;
	time_t t = time(nullptr);
	tm local;
	localtime_r(&t, &local);
	return t + local.tm_gmtoff + timeOffset;
}

void setTime(time_t t){
	timeOffset += (long) (t - now());
}

bool timeValid(){ return true; }

Battery battery(){
	return { 78, 3990, false, false };
}

void backlight(uint8_t percent){ brightness = percent; }

void tone(uint16_t){}

void rgb(bool, bool, bool){}

bool imuPresent(){ return true; }

Accel accel(){
	const float t = millis() / 1000.0f;
	return { 0.25f * sinf(t * 0.7f), 0.2f * cosf(t * 0.5f), 0.95f };
}

uint32_t stepCounter(){
	return 4321 + millis() / 600;
}

Wake sleep(uint32_t ms){
	sleeping = true;
	const uint32_t start = millis();
	while(millis() - start < ms){
		for(int i = 0; i < 4; i++){
			if(button((Button) i)){
				sleeping = false;
				return Wake::Button;
			}
		}
		if(isHeadless) return Wake::Timer; // the script advances time for us
		lgfx::delay(10);
	}
	sleeping = false;
	return Wake::Timer;
}

void powerOff(){
	exit(0);
}

void restart(){
	exit(0);
}

uint8_t revision(){ return 3; }

const char* deviceName(){ return "Clockstar 2.0 (sim)"; }

int32_t prefGet(const char* key, int32_t def){
	auto it = prefs.find(key);
	return it == prefs.end() ? def : it->second;
}

void prefSet(const char* key, int32_t value){ prefs[key] = value; }

void prefClear(){ prefs.clear(); }

}

#endif
