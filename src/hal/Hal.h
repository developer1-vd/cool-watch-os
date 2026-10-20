#pragma once
#include <cstdint>
#include <ctime>

#include <LovyanGFX.hpp>

// Hardware abstraction layer. HalClockstar.cpp and HalWatchy.cpp drive real
// watches, HalSim.cpp runs the OS in an SDL window (as either watch).
namespace hal {

// What the watch can do. The OS and apps adapt to these.
struct Features {
	const char* screen;     // shown in About
	bool color;             // false: black & white e-paper
	bool persistentDisplay; // keeps its image while the CPU sleeps
	bool backlight;
	bool buzzer;            // false: tone() drives a vibration motor
	bool rgbLed;
	bool usbDetect;         // can tell when USB power is connected
};

const Features& features();

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
void setPaperMode(bool paper); // e-paper: white background, black ink
void refreshFull();            // e-paper: full refresh next frame (clears ghosting)

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
