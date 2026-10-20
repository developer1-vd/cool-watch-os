#pragma once
#include <cstdint>

// Turns the OS's 128x128 colour frame into a 200x200 black-and-white bitmap
// for e-paper. Luminance is upscaled bilinearly and then thresholded, which
// keeps edges and anti-aliased text smooth instead of blocky.
namespace mono {

constexpr int Size = 200;
constexpr int Bytes = Size * Size / 8;

// Below this luminance a pixel is background. The midpoint keeps strokes the
// same width whether they are light-on-dark or dark-on-light; the UI draws
// in full-contrast colours on e-paper so nothing important sits near it.
constexpr uint8_t Threshold = 128;

// src: 128x128 RGB565, byte-swapped as LovyanGFX sprites store it.
// dst: 1 bit per pixel, MSB first, rows of 25 bytes; 1 = white.
// paper: white background with black ink instead of the dark theme.
void convert(const uint16_t* src, uint8_t* dst, bool paper);

inline bool pixel(const uint8_t* bitmap, int x, int y){
	return bitmap[(y * Size + x) >> 3] & (0x80 >> (x & 7));
}

}
