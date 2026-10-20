// SQFMI Watchy hardware layer (ESP32-PICO-D4, 200x200 e-paper).
// Pins and quirks follow the official Watchy library (sqfmi/Watchy).
#if defined(STARDROID_WATCHY)

#include "Hal.h"
#include "Mono.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <sys/time.h>
#include <GxEPD2_BW.h>
#include <SensorBMA423.hpp>

#ifndef STARDROID_WATCHY_REV
#define STARDROID_WATCHY_REV 20
#endif

namespace {

// ---- Pins --------------------------------------------------------------------------

constexpr int PinMenu = 26; // Select
constexpr int PinBack = 25;
constexpr int PinDown = 4;
#if STARDROID_WATCHY_REV >= 20
constexpr int PinUp = 35;
constexpr int PinBattery = 34;
#elif STARDROID_WATCHY_REV == 15
constexpr int PinUp = 32;
constexpr int PinBattery = 35;
#else
constexpr int PinUp = 32;
constexpr int PinBattery = 33;
#endif

constexpr int PinCs = 5, PinDc = 10, PinRst = 9, PinBusy = 19;
constexpr int PinVibration = 13;
constexpr int PinSda = 21, PinScl = 22;

// Order matches hal::Button
constexpr int ButtonPins[] = { PinUp, PinDown, PinMenu, PinBack };

constexpr uint8_t AddrDS3231 = 0x68;  // Watchy v1.0
constexpr uint8_t AddrPCF8563 = 0x51; // v1.5 and v2.0
constexpr uint8_t AddrBMA423 = 0x18;

// ---- State ---------------------------------------------------------------------------

GxEPD2_154_D67 epd(PinCs, PinDc, PinRst, PinBusy);
SensorBMA423 bma;
Preferences prefs;

uint8_t shown[mono::Bytes];
uint8_t next[mono::Bytes];
bool paper = true;
bool fullPending = true;
bool panelPowered = false;

enum class Rtc : uint8_t { None, DS3231, PCF8563 } rtc = Rtc::None;
bool rtcValid = false;
bool imuOk = false;

float battMv = 0;
uint32_t lastBattSample = 0;

// Presses that happen while the panel is busy refreshing are remembered and
// replayed afterwards, so quick taps are not lost.
volatile bool pressedWhileBusy[4] = {};
uint32_t latchUntil[4] = {};

void busyCallback(const void*){
	for(int i = 0; i < 4; i++){
		if(digitalRead(ButtonPins[i]) == HIGH) pressedWhileBusy[i] = true;
	}
	delay(1);
}

void applyLatches(){
	const uint32_t t = ::millis();
	for(int i = 0; i < 4; i++){
		if(pressedWhileBusy[i]){
			pressedWhileBusy[i] = false;
			latchUntil[i] = t + 60; // long enough to pass the OS debounce
		}
	}
}

// ---- I2C helpers -------------------------------------------------------------------

bool i2cPresent(uint8_t addr){
	Wire.beginTransmission(addr);
	return Wire.endTransmission() == 0;
}

bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len){
	Wire.beginTransmission(addr);
	Wire.write(reg);
	Wire.write(data, len);
	return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* data, size_t len){
	Wire.beginTransmission(addr);
	Wire.write(reg);
	if(Wire.endTransmission(false) != 0) return false;
	if(Wire.requestFrom(addr, (uint8_t) len) != len) return false;
	for(size_t i = 0; i < len; i++) data[i] = Wire.read();
	return true;
}

uint8_t bcd2dec(uint8_t v){ return (v >> 4) * 10 + (v & 0x0F); }
uint8_t dec2bcd(uint8_t v){ return ((v / 10) << 4) | (v % 10); }

// ---- RTC (DS3231 or PCF8563) -----------------------------------------------------------

void rtcInit(){
	if(i2cPresent(AddrDS3231)){
		rtc = Rtc::DS3231;
		const uint8_t control = 0x1C; // INTCN, alarms off, oscillator on
		i2cWrite(AddrDS3231, 0x0E, &control, 1);
	}else if(i2cPresent(AddrPCF8563)){
		rtc = Rtc::PCF8563;
		const uint8_t zero[2] = { 0, 0 }; // normal mode, no alarm/timer interrupts
		i2cWrite(AddrPCF8563, 0x00, zero, 2);
	}
}

bool rtcRead(tm& t){
	uint8_t d[7];
	t = {};
	if(rtc == Rtc::DS3231){
		uint8_t status = 0;
		if(!i2cRead(AddrDS3231, 0x00, d, 7) || !i2cRead(AddrDS3231, 0x0F, &status, 1)) return false;
		rtcValid = !(status & 0x80); // OSF: oscillator stopped
		t.tm_sec = bcd2dec(d[0] & 0x7F);
		t.tm_min = bcd2dec(d[1] & 0x7F);
		t.tm_hour = bcd2dec(d[2] & 0x3F);
		t.tm_wday = (d[3] & 0x07) - 1;
		t.tm_mday = bcd2dec(d[4] & 0x3F);
		t.tm_mon = bcd2dec(d[5] & 0x1F) - 1;
		t.tm_year = bcd2dec(d[6]) + 100;
		return true;
	}
	if(rtc == Rtc::PCF8563){
		if(!i2cRead(AddrPCF8563, 0x02, d, 7)) return false;
		rtcValid = !(d[0] & 0x80); // VL: voltage low, time unreliable
		t.tm_sec = bcd2dec(d[0] & 0x7F);
		t.tm_min = bcd2dec(d[1] & 0x7F);
		t.tm_hour = bcd2dec(d[2] & 0x3F);
		t.tm_mday = bcd2dec(d[3] & 0x3F);
		t.tm_wday = d[4] & 0x07;
		t.tm_mon = bcd2dec(d[5] & 0x1F) - 1;
		t.tm_year = bcd2dec(d[6]) + 100;
		return true;
	}
	return false;
}

void rtcWrite(const tm& t){
	if(rtc == Rtc::DS3231){
		const uint8_t d[7] = {
			dec2bcd(t.tm_sec), dec2bcd(t.tm_min), dec2bcd(t.tm_hour), (uint8_t) (t.tm_wday + 1),
			dec2bcd(t.tm_mday), dec2bcd(t.tm_mon + 1), dec2bcd(t.tm_year % 100),
		};
		i2cWrite(AddrDS3231, 0x00, d, 7);
		uint8_t status = 0;
		if(i2cRead(AddrDS3231, 0x0F, &status, 1)){
			status &= ~0x80;
			i2cWrite(AddrDS3231, 0x0F, &status, 1);
		}
	}else if(rtc == Rtc::PCF8563){
		const uint8_t d[7] = {
			dec2bcd(t.tm_sec), dec2bcd(t.tm_min), dec2bcd(t.tm_hour), dec2bcd(t.tm_mday),
			(uint8_t) t.tm_wday, dec2bcd(t.tm_mon + 1), dec2bcd(t.tm_year % 100),
		};
		i2cWrite(AddrPCF8563, 0x02, d, 7);
	}else{
		return;
	}
	rtcValid = true;
}

// ---- Battery ---------------------------------------------------------------------------

float sampleBattery(){
	uint32_t total = 0;
	for(int i = 0; i < 8; i++) total += analogReadMilliVolts(PinBattery);
	return total / 8.0f * 2.0f; // 1:2 divider
}

}

namespace hal {

const Features& features(){
	static const Features f = { "1.54\" e-paper", false, true, false, false, false, false };
	return f;
}

void begin(){
	Serial.begin(115200);

	for(int pin : ButtonPins) pinMode(pin, INPUT);
	pinMode(PinVibration, OUTPUT);
	digitalWrite(PinVibration, LOW);

	epd.init(0, true, 2, true); // Watchy's reset timing
	epd.setBusyCallback(busyCallback);

	Wire.begin(PinSda, PinScl);
	rtcInit();
	tm t;
	if(rtcRead(t)){
		timeval tv = { mktime(&t), 0 };
		settimeofday(&tv, nullptr);
	}

	imuOk = bma.init(Wire, PinSda, PinScl, AddrBMA423);
	if(imuOk){
		bma.configAccelerometer(SensorBMA423::RANGE_2G, SensorBMA423::ODR_100HZ);
		bma.enableAccelerometer();
		bma.enablePedometer();
	}

	battMv = sampleBattery();
	lastBattSample = ::millis();

	prefs.begin("stardroid");
}

void present(LGFX_Sprite& frame){
	mono::convert((const uint16_t*) frame.getBuffer(), next, paper);
	if(!fullPending && memcmp(next, shown, mono::Bytes) == 0) return;
	memcpy(shown, next, mono::Bytes);
	panelPowered = true;
	if(fullPending){
		epd.writeImageForFullRefresh(shown, 0, 0, mono::Size, mono::Size);
		epd.refresh(false);
		epd.writeImageAgain(shown, 0, 0, mono::Size, mono::Size);
		fullPending = false;
	}else{
		epd.drawImage(shown, 0, 0, mono::Size, mono::Size);
	}
	applyLatches();
}

void displayPower(bool on){
	if(!on && panelPowered){
		epd.powerOff();
		panelPowered = false;
	}
}

void setPaperMode(bool p){
	if(p != paper) fullPending = true;
	paper = p;
}

void refreshFull(){
	fullPending = true;
}

bool button(Button b){
	const int i = (int) b;
	return digitalRead(ButtonPins[i]) == HIGH || ::millis() < latchUntil[i];
}

uint32_t millis(){
	return ::millis();
}

void idle(uint32_t ms){
	delay(ms);
}

time_t now(){
	return time(nullptr);
}

void setTime(time_t t){
	timeval tv = { t, 0 };
	settimeofday(&tv, nullptr);
	tm parts;
	gmtime_r(&t, &parts);
	rtcWrite(parts);
}

bool timeValid(){
	return rtcValid;
}

Battery battery(){
	const uint32_t t = ::millis();
	if(t - lastBattSample > 5000){
		lastBattSample = t;
		battMv = battMv * 0.8f + sampleBattery() * 0.2f;
	}
	float pct = (battMv - 3600.0f) / (4200.0f - 3600.0f) * 100.0f;
	if(pct < 0) pct = 0;
	if(pct > 100) pct = 100;
	// v1/v2 boards can't sense USB; above 4.25 V the charger must be connected.
	const bool usb = battMv > 4250;
	return { (uint8_t) pct, (uint16_t) battMv, usb, false };
}

void backlight(uint8_t){}

void tone(uint16_t freq){
	digitalWrite(PinVibration, freq ? HIGH : LOW); // the vibration motor
}

void rgb(bool, bool, bool){}

int ledCount(){ return 0; }

void led(int, bool){}

bool imuPresent(){
	return imuOk;
}

Accel accel(){
	int16_t x, y, z;
	if(!imuOk || !bma.getAccelerometer(x, y, z)) return { 0, 0, 1 };
	// 12-bit samples at +-2 g: 1024 counts per g
	return { x / 1024.0f, y / 1024.0f, z / 1024.0f };
}

uint32_t stepCounter(){
	return imuOk ? bma.getPedometerCounter() : 0;
}

Wake sleep(uint32_t ms){
	tone(0);
	displayPower(false);
	Serial.flush();

	for(int pin : ButtonPins) gpio_wakeup_enable((gpio_num_t) pin, GPIO_INTR_HIGH_LEVEL);
	esp_sleep_enable_gpio_wakeup();
	esp_sleep_enable_timer_wakeup((uint64_t) ms * 1000ULL);

	esp_light_sleep_start();

	const auto cause = esp_sleep_get_wakeup_cause();
	for(int pin : ButtonPins) gpio_wakeup_disable((gpio_num_t) pin);
	esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

	return cause == ESP_SLEEP_WAKEUP_GPIO ? Wake::Button : Wake::Timer;
}

void powerOff(){
	// Watchy has no power switch: show the last frame and deep sleep until
	// any button is pressed, which boots the watch again.
	tone(0);
	epd.hibernate();
	uint64_t mask = 0;
	for(int pin : ButtonPins) mask |= 1ULL << pin;
	esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);
	esp_deep_sleep_start();
}

void restart(){
	ESP.restart();
}

uint8_t revision(){
	return STARDROID_WATCHY_REV;
}

const char* deviceName(){
#if STARDROID_WATCHY_REV >= 20
	return "Watchy v2.0";
#elif STARDROID_WATCHY_REV == 15
	return "Watchy v1.5";
#else
	return "Watchy v1.0";
#endif
}

int32_t prefGet(const char* key, int32_t def){
	return prefs.getInt(key, def);
}

void prefSet(const char* key, int32_t value){
	if(prefs.getInt(key, value + 1) == value) return;
	prefs.putInt(key, value);
}

void prefClear(){
	prefs.clear();
}

}

#endif
