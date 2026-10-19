#pragma once
#include <cstdint>

// Pin maps for the CircuitMess Clockstar 2.0 (ESP32-S3), taken from the
// official CircuitMess Artemis firmware. The hardware revision is burned into
// eFuse BLK3 (bits 32..39) at the factory.
namespace hal {

struct PinMap {
	int8_t btnUp, btnDown, btnSelect, btnAlt;
	int8_t backlight; // active low
	int8_t buzzer;
	int8_t battRead, battVref; // battVref < 0 when the board has no reference switch
	int8_t usb;        // high when USB is plugged in
	int8_t chrgIn;     // low while charging
	int8_t sda, scl;
	int8_t tftSck, tftMosi, tftDc, tftRst;
	int8_t rgbR, rgbG, rgbB; // active low
	int8_t pwdn;       // drive low to cut power
	int8_t imuInt1, imuInt2;
};

// Revisions 0 and 1
constexpr PinMap PinsRev1 = {
	9, 10, 8, 21,
	37,
	11,
	6, -1,
	47,
	1,
	35, 36,
	41, 40, 39, 38,
	33, 34, 48,
	42,
	4, 5,
};

// Revisions 2 and 3
constexpr PinMap PinsRev2 = {
	1, 2, 3, 21,
	33,
	47,
	5, 4,
	42,
	7,
	40, 41,
	36, 35, 48, 34,
	14, 12, 13,
	37,
	38, 39,
};

// Small single-colour LEDs, identical on all revisions (active high).
constexpr int8_t SingleLeds[] = { 17, 18, 43, 44, 45, 46 };

inline const PinMap& pinsFor(uint8_t revision){
	return revision <= 1 ? PinsRev1 : PinsRev2;
}

}
