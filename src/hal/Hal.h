#pragma once
#include <cstdint>
#include <ctime>

#include <LovyanGFX.hpp>

// Hardware abstraction layer. One implementation drives the real Clockstar
// (HalClockstar.cpp), the other runs the OS in an SDL window (HalSim.cpp).
namespace hal {

enum class Button : uint8_t { Up, Down, Select, Alt, Count };

struct Battery {
	uint8_t percent;
	uint16_t millivolts;
	bool usb;
	bool charging;
};

struct Accel {
	float x, y, z; // in g
};

enum class Wake : uint8_t { Button, Timer };

void begin();

// Pushes a finished 128x128 frame to the screen.
void present(LGFX_Sprite& frame);
void displayPower(bool on);

bool button(Button b);
uint32_t millis();
void idle(uint32_t ms); // yield between frames

// Wall-clock time. The RTC keeps local time, so treat this as "local epoch"
// and break it down with gmtime_r().
time_t now();
void setTime(time_t t);
bool timeValid(); // false if the RTC lost power

Battery battery();

void backlight(uint8_t percent); // 0 turns it off
void tone(uint16_t freq);        // 0 silences the buzzer
void rgb(bool r, bool g, bool b); // notification LED
int ledCount();                  // small single-colour LEDs
void led(int i, bool on);

bool imuPresent();
Accel accel();
uint32_t stepCounter(); // hardware pedometer, free running

// Sleeps until a button is pressed or `ms` have passed. Screen must be off.
Wake sleep(uint32_t ms);
void powerOff();
void restart();

uint8_t revision();
const char* deviceName();

int32_t prefGet(const char* key, int32_t def);
void prefSet(const char* key, int32_t value);
void prefClear();

}
