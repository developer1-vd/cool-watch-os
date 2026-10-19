#pragma once
#include <cstdint>
#include "Hal.h"

// Hooks the desktop simulator uses to drive the OS without a window
// (scripted input, virtual clock, frame capture).
namespace sim {

void setHeadless(uint32_t startEpoch);
bool headless();
void advance(uint32_t ms);
void setButton(hal::Button b, bool pressed);
const uint16_t* lastFrame(); // 128x128 RGB565, byte-swapped like the panel
bool asleep();
void setStepCounter(int32_t steps); // -1 restores the simulated walk
void setBattery(const hal::Battery& b);
void setImuPresent(bool present);

}
