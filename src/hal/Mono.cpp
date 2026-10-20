#include "Mono.h"
#include <cstring>

namespace mono {

namespace {

constexpr int Src = 128;

uint8_t luma(uint16_t swapped){
	const uint16_t v = (swapped >> 8) | (swapped << 8);
	const uint32_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
	// Rec. 601 weights on 0..255 channels
	return (uint8_t) ((r * 255 / 31 * 77 + g * 255 / 63 * 150 + b * 255 / 31 * 29) >> 8);
}

}

void convert(const uint16_t* src, uint8_t* dst, bool paper){
	static uint8_t lum[Src * Src];
	for(int i = 0; i < Src * Src; i++) lum[i] = luma(src[i]);

	memset(dst, 0, Bytes);
	// Fixed point (16.16) source coordinates for each output pixel centre.
	constexpr int32_t Step = (Src << 16) / Size;
	for(int y = 0; y < Size; y++){
		int32_t sy = y * Step + Step / 2 - (1 << 15);
		if(sy < 0) sy = 0;
		int y0 = sy >> 16;
		if(y0 > Src - 2) y0 = Src - 2;
		const uint32_t fy = (sy >> 8) & 0xFF;
		const uint8_t* row0 = lum + y0 * Src;
		const uint8_t* row1 = row0 + Src;
		uint8_t* out = dst + y * (Size / 8);

		for(int x = 0; x < Size; x++){
			int32_t sx = x * Step + Step / 2 - (1 << 15);
			if(sx < 0) sx = 0;
			int x0 = sx >> 16;
			if(x0 > Src - 2) x0 = Src - 2;
			const uint32_t fx = (sx >> 8) & 0xFF;

			const uint32_t top = row0[x0] * (256 - fx) + row0[x0 + 1] * fx;
			const uint32_t bottom = row1[x0] * (256 - fx) + row1[x0 + 1] * fx;
			const uint32_t v = (top * (256 - fy) + bottom * fy) >> 16;

			const bool ink = v >= Threshold;
			if(ink != paper) out[x >> 3] |= 0x80 >> (x & 7);
		}
	}
}

}
