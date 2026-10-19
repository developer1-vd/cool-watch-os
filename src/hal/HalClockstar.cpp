// Clockstar 2.0 hardware layer (ESP32-S3, Arduino core).
#if defined(ARDUINO) && !defined(LGFX_SDL)

#include "Hal.h"
#include "Pins.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_efuse.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <sys/time.h>
#include <LovyanGFX.hpp>

namespace {

constexpr esp_efuse_desc_t RevField = { EFUSE_BLK3, 32, 8 };
const esp_efuse_desc_t* RevBlob[] = { &RevField, nullptr };

constexpr uint8_t BuzzChannel = 0;
constexpr uint8_t BacklightChannel = 2; // separate LEDC timer from the buzzer
constexpr uint8_t BacklightBits = 10;
constexpr uint32_t BacklightMax = (1 << BacklightBits) - 1;
constexpr uint8_t BacklightMinDuty = 10;

constexpr uint8_t RtcAddr = 0x51; // BM8563
constexpr uint8_t ImuAddr = 0x6A; // LSM6DS3TR-C

namespace imureg {
	constexpr uint8_t WhoAmI = 0x0F;
	constexpr uint8_t Ctrl1Xl = 0x10;
	constexpr uint8_t Ctrl3C = 0x12;
	constexpr uint8_t Ctrl10C = 0x19;
	constexpr uint8_t OutXlXl = 0x28;
	constexpr uint8_t StepCounterL = 0x4B;
}

uint8_t rev = 3;
const hal::PinMap* pins = &hal::PinsRev2;
Preferences prefs;
bool rtcValid = false;
bool imuOk = false;
bool lcdOn = true;

float battMv = 0;
float battCalOffset = 0;
uint32_t lastBattSample = 0;

class ClockstarLCD : public lgfx::LGFX_Device {
public:
	void setup(const hal::PinMap& p, uint8_t revision){
		auto b = bus.config();
		b.spi_host = SPI2_HOST;
		b.spi_mode = 0;
		b.freq_write = 40000000;
		b.freq_read = 16000000;
		b.spi_3wire = false;
		b.use_lock = true;
		b.dma_channel = SPI_DMA_CH_AUTO;
		b.pin_sclk = p.tftSck;
		b.pin_mosi = p.tftMosi;
		b.pin_miso = -1;
		b.pin_dc = p.tftDc;
		bus.config(b);
		panel.setBus(&bus);

		auto c = panel.config();
		c.pin_cs = -1;
		c.pin_rst = p.tftRst;
		c.pin_busy = -1;
		c.memory_width = revision == 3 ? 128 : 132;
		c.memory_height = revision == 3 ? 160 : 132;
		c.panel_width = 128;
		c.panel_height = 128;
		c.offset_x = revision == 3 ? 0 : 2;
		c.offset_y = revision == 3 ? 0 : 1;
		c.offset_rotation = revision >= 2 ? 3 : 1;
		c.readable = false;
		c.invert = false;
		c.rgb_order = false;
		c.dlen_16bit = false;
		c.bus_shared = false;
		panel.config(c);
		setPanel(&panel);
	}

private:
	lgfx::Bus_SPI bus;
	lgfx::Panel_ST7735S panel;
};

ClockstarLCD lcd;

bool i2cWrite(uint8_t addr, uint8_t reg, const uint8_t* data, size_t len){
	Wire.beginTransmission(addr);
	Wire.write(reg);
	Wire.write(data, len);
	return Wire.endTransmission() == 0;
}

bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t value){
	return i2cWrite(addr, reg, &value, 1);
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

bool rtcRead(tm& t){
	uint8_t d[7];
	if(!i2cRead(RtcAddr, 0x02, d, 7)) return false;
	rtcValid = !(d[0] & 0x80); // VL bit: clock integrity lost
	t = {};
	t.tm_sec = bcd2dec(d[0] & 0x7F);
	t.tm_min = bcd2dec(d[1] & 0x7F);
	t.tm_hour = bcd2dec(d[2] & 0x3F);
	t.tm_mday = bcd2dec(d[3] & 0x3F);
	t.tm_wday = bcd2dec(d[4] & 0x07);
	t.tm_mon = bcd2dec(d[5] & 0x1F) - 1;
	t.tm_year = bcd2dec(d[6]) + ((d[5] & 0x80) ? 100 : 0);
	return true;
}

void rtcWrite(const tm& t){
	uint8_t d[7];
	d[0] = dec2bcd(t.tm_sec) & 0x7F; // also clears VL
	d[1] = dec2bcd(t.tm_min);
	d[2] = dec2bcd(t.tm_hour);
	d[3] = dec2bcd(t.tm_mday);
	d[4] = dec2bcd(t.tm_wday);
	d[5] = dec2bcd(t.tm_mon + 1) | (t.tm_year >= 100 ? 0x80 : 0);
	d[6] = dec2bcd(t.tm_year % 100);
	i2cWrite(RtcAddr, 0x02, d, 7);
	rtcValid = true;
}

void imuInit(){
	uint8_t id = 0;
	imuOk = i2cRead(ImuAddr, imureg::WhoAmI, &id, 1) && id == 0x6A;
	if(!imuOk) return;
	i2cWrite(ImuAddr, imureg::Ctrl3C, 0x44);  // BDU | IF_INC
	i2cWrite(ImuAddr, imureg::Ctrl1Xl, 0x40); // 104 Hz, +-2 g
	i2cWrite(ImuAddr, imureg::Ctrl10C, 0x14); // FUNC_EN | PEDO_EN
}

float readBattRaw(){
	uint32_t total = 0;
	for(int i = 0; i < 8; i++) total += analogReadMilliVolts(pins->battRead);
	return total / 8.0f;
}

void calibrateBattery(){
	if(pins->battVref < 0) return;
	// The reference switch puts a known 2.5 V (through the same divider)
	// onto the battery sense line, which lets us cancel ADC offset error.
	digitalWrite(pins->battVref, HIGH);
	delay(100);
	float ref = readBattRaw() * 4.0f;
	digitalWrite(pins->battVref, LOW);
	delay(100);
	battCalOffset = 2500.0f - ref;
}

float sampleBattery(){
	if(rev >= 2) return readBattRaw() * 4.0f + 75.0f + battCalOffset;
	return readBattRaw() * 2.0f;
}

void backlightOff(){
	ledcDetachPin(pins->backlight);
	pinMode(pins->backlight, OUTPUT);
	digitalWrite(pins->backlight, HIGH);
}

const int8_t* buttonPins(){
	static int8_t list[4];
	list[0] = pins->btnUp;
	list[1] = pins->btnDown;
	list[2] = pins->btnSelect;
	list[3] = pins->btnAlt;
	return list;
}

}

namespace hal {

void begin(){
	uint8_t r = 0;
	if(esp_efuse_read_field_blob(RevBlob, &r, 8) == ESP_OK && r <= 3) rev = r;
	pins = &pinsFor(rev);

	Serial.begin(115200);

	for(auto pin : SingleLeds){
		pinMode(pin, OUTPUT);
		digitalWrite(pin, LOW);
	}
	for(auto pin : { pins->rgbR, pins->rgbG, pins->rgbB }){
		pinMode(pin, OUTPUT);
		digitalWrite(pin, HIGH);
	}

	auto btns = buttonPins();
	for(int i = 0; i < 4; i++) pinMode(btns[i], INPUT_PULLDOWN);

	pinMode(pins->usb, INPUT);
	pinMode(pins->chrgIn, INPUT);
	if(pins->battVref >= 0){
		pinMode(pins->battVref, OUTPUT);
		digitalWrite(pins->battVref, LOW);
	}
	analogSetPinAttenuation(pins->battRead, rev >= 2 ? ADC_2_5db : ADC_11db);

	backlightOff();
	ledcSetup(BacklightChannel, 5000, BacklightBits);
	ledcSetup(BuzzChannel, 2000, 10);
	ledcAttachPin(pins->buzzer, BuzzChannel);
	ledcWrite(BuzzChannel, 0);

	lcd.setup(*pins, rev);
	lcd.init();
	lcd.fillScreen(TFT_BLACK);

	Wire.begin(pins->sda, pins->scl, 400000);
	i2cWrite(RtcAddr, 0x00, (const uint8_t[]){ 0, 0 }, 2);
	tm t;
	if(rtcRead(t)){
		timeval tv = { mktime(&t), 0 };
		settimeofday(&tv, nullptr);
	}
	imuInit();

	calibrateBattery();
	battMv = sampleBattery();
	lastBattSample = ::millis();

	prefs.begin("stardroid");
}

void present(LGFX_Sprite& frame){
	if(!lcdOn) return;
	frame.pushSprite(&lcd, 0, 0);
}

void displayPower(bool on){
	if(on == lcdOn) return;
	lcdOn = on;
	if(on) lcd.wakeup();
	else lcd.sleep();
}

bool button(Button b){
	return digitalRead(buttonPins()[(int) b]) == HIGH;
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
	if(t - lastBattSample > 2000){
		lastBattSample = t;
		battMv = battMv * 0.9f + sampleBattery() * 0.1f;
	}
	const float full = rev >= 2 ? 4150.0f : 4200.0f;
	const float empty = 3600.0f;
	float pct = (battMv - empty) / (full - empty) * 100.0f;
	if(pct < 0) pct = 0;
	if(pct > 100) pct = 100;

	Battery b;
	b.millivolts = (uint16_t) battMv;
	b.percent = (uint8_t) pct;
	b.usb = digitalRead(pins->usb) == HIGH;
	b.charging = b.usb && digitalRead(pins->chrgIn) == LOW;
	return b;
}

void backlight(uint8_t percent){
	if(percent == 0){
		backlightOff();
		return;
	}
	if(percent > 100) percent = 100;
	// Perceptual curve, same shape as the stock firmware.
	float f = percent / 100.0f;
	f = BacklightMinDuty + f * f * (100 - BacklightMinDuty);
	const uint32_t duty = (uint32_t) (f / 100.0f * BacklightMax);
	ledcAttachPin(pins->backlight, BacklightChannel);
	ledcWrite(BacklightChannel, BacklightMax - duty); // active low
}

void tone(uint16_t freq){
	if(freq == 0) ledcWrite(BuzzChannel, 0);
	else ledcWriteTone(BuzzChannel, freq);
}

void rgb(bool r, bool g, bool b){
	digitalWrite(pins->rgbR, r ? LOW : HIGH);
	digitalWrite(pins->rgbG, g ? LOW : HIGH);
	digitalWrite(pins->rgbB, b ? LOW : HIGH);
}

bool imuPresent(){
	return imuOk;
}

Accel accel(){
	uint8_t d[6];
	if(!imuOk || !i2cRead(ImuAddr, imureg::OutXlXl, d, 6)) return { 0, 0, 1 };
	auto axis = [&](int i){ return (int16_t) (d[i] | (d[i + 1] << 8)) * 0.000061f; };
	// Raw sensor axes. If the level app tilts the wrong way on your unit,
	// swap or negate these.
	return { axis(0), axis(2), axis(4) };
}

uint32_t stepCounter(){
	uint8_t d[2];
	if(!imuOk || !i2cRead(ImuAddr, imureg::StepCounterL, d, 2)) return 0;
	return d[0] | (d[1] << 8);
}

Wake sleep(uint32_t ms){
	tone(0);
	backlightOff();
	Serial.flush();

	// Keep the panel lines and backlight in a defined state while asleep.
	gpio_sleep_set_pull_mode((gpio_num_t) pins->tftDc, GPIO_PULLUP_ONLY);
	gpio_sleep_set_pull_mode((gpio_num_t) pins->tftRst, GPIO_PULLUP_ONLY);
	gpio_sleep_set_pull_mode((gpio_num_t) pins->tftSck, GPIO_PULLDOWN_ONLY);
	gpio_sleep_set_pull_mode((gpio_num_t) pins->tftMosi, GPIO_PULLUP_ONLY);
	gpio_sleep_set_pull_mode((gpio_num_t) pins->backlight, GPIO_PULLUP_ONLY);
	for(auto pin : { pins->tftDc, pins->tftRst, pins->tftSck, pins->tftMosi, pins->backlight }){
		gpio_sleep_sel_en((gpio_num_t) pin);
	}

	auto btns = buttonPins();
	for(int i = 0; i < 4; i++) gpio_wakeup_enable((gpio_num_t) btns[i], GPIO_INTR_HIGH_LEVEL);
	esp_sleep_enable_gpio_wakeup();
	esp_sleep_enable_timer_wakeup((uint64_t) ms * 1000ULL);

	esp_light_sleep_start();

	const auto cause = esp_sleep_get_wakeup_cause();
	for(int i = 0; i < 4; i++) gpio_wakeup_disable((gpio_num_t) btns[i]);
	esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

	return cause == ESP_SLEEP_WAKEUP_GPIO ? Wake::Button : Wake::Timer;
}

void powerOff(){
	tone(0);
	backlightOff();
	rgb(false, false, false);
	lcd.sleep();
	digitalWrite(pins->pwdn, LOW);
	pinMode(pins->pwdn, OUTPUT);
	delay(500);
	// Still running, so USB is powering us. Deep sleep until Select is pressed.
	esp_sleep_enable_ext1_wakeup(1ULL << pins->btnSelect, ESP_EXT1_WAKEUP_ANY_HIGH);
	esp_deep_sleep_start();
}

void restart(){
	ESP.restart();
}

uint8_t revision(){
	return rev;
}

const char* deviceName(){
	return "Clockstar 2.0";
}

int32_t prefGet(const char* key, int32_t def){
	return prefs.getInt(key, def);
}

void prefSet(const char* key, int32_t value){
	if(prefs.getInt(key, value + 1) == value) return; // spare the flash
	prefs.putInt(key, value);
}

void prefClear(){
	prefs.clear();
}

}

#endif
