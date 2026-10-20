// Cool Watch OS simulator entry point.
//
//   coolwatch-sim                      interactive SDL window
//   coolwatch-sim --script "..." --out DIR
//                                      headless, scripted, writes PPM frames
//   --watchy                           run as a Watchy (200x200 e-paper)
//
// Script tokens (space separated):
//   u d s b    click Up / Down / Select / Back
//   U D S B    long-press
//   wN         wait N ms
//   p:NAME     save the current frame as DIR/NAME.ppm
//   reset      wipe preferences (first boot)
#include <LovyanGFX.hpp>
#include "hal/SimControl.h"
#include "hal/Mono.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

void setup();
void loop();

namespace {

constexpr uint32_t FrameMs = 25;

void run(uint32_t ms){
	for(uint32_t t = 0; t < ms; t += FrameMs){
		sim::advance(FrameMs);
		loop();
	}
}

void savePpm(const std::string& path){
	FILE* f = fopen(path.c_str(), "wb");
	if(!f){
		perror(path.c_str());
		return;
	}
	if(sim::target() == sim::Target::Watchy){
		fprintf(f, "P6\n%d %d\n255\n", mono::Size, mono::Size);
		const uint8_t* bits = sim::lastMono();
		for(int y = 0; y < mono::Size; y++){
			for(int x = 0; x < mono::Size; x++){
				const bool white = mono::pixel(bits, x, y);
				const uint8_t rgb[3] = { (uint8_t) (white ? 222 : 34), (uint8_t) (white ? 222 : 34), (uint8_t) (white ? 212 : 38) };
				fwrite(rgb, 1, 3, f);
			}
		}
		fclose(f);
		return;
	}
	fprintf(f, "P6\n128 128\n255\n");
	const uint16_t* px = sim::lastFrame();
	for(int i = 0; i < 128 * 128; i++){
		const uint16_t v = (px[i] >> 8) | (px[i] << 8);
		const uint8_t rgb[3] = {
			(uint8_t) (((v >> 11) & 0x1F) * 255 / 31),
			(uint8_t) (((v >> 5) & 0x3F) * 255 / 63),
			(uint8_t) ((v & 0x1F) * 255 / 31),
		};
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

hal::Button buttonFor(char c){
	switch(tolower(c)){
		case 'u': return hal::Button::Up;
		case 'd': return hal::Button::Down;
		case 's': return hal::Button::Select;
		default: return hal::Button::Alt;
	}
}

int headless(const std::string& script, const std::string& out){
	// 2026-10-18 10:09:30, the classic watch-ad time.
	sim::setHeadless(1792318170);
	setup();
	std::istringstream in(script);
	std::string tok;
	while(in >> tok){
		if(tok == "reset"){
			hal::prefClear();
		}else if(tok[0] == 'w'){
			run(atoi(tok.c_str() + 1));
		}else if(tok.rfind("p:", 0) == 0){
			savePpm(out + "/" + tok.substr(2) + ".ppm");
		}else if(tok.size() == 1 && strchr("udsbUDSB", tok[0])){
			const auto b = buttonFor(tok[0]);
			const bool longPress = isupper(tok[0]);
			sim::setButton(b, true);
			run(longPress ? 800 : 75);
			sim::setButton(b, false);
			run(150);
		}else{
			fprintf(stderr, "unknown script token: %s\n", tok.c_str());
			return 1;
		}
	}
	return 0;
}

int interactive(bool* running){
	setup();
	while(*running) loop();
	return 0;
}

}

int main(int argc, char** argv){
	std::string script, out = ".";
	for(int i = 1; i < argc; i++){
		if(!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
		else if(!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
		else if(!strcmp(argv[i], "--watchy")) sim::setTarget(sim::Target::Watchy);
	}
	if(!script.empty()) return headless(script, out);

	printf("Cool Watch OS simulator\n  Up/Down arrows: Up/Down\n  Enter/Space/Right: Select\n  Esc/Backspace/Left: Back\n");
	return lgfx::Panel_sdl::main(interactive);
}
