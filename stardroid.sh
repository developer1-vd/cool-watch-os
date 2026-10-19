#!/usr/bin/env bash
# StarDroid helper: one script for the whole build / test / flash loop.
#
#   ./stardroid.sh            install what's missing, run tests, build firmware
#   ./stardroid.sh help       list every command
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SIM_BUILD="$ROOT/build/sim"
FW_BUILD="$ROOT/.pio/build/clockstar"
DIST="$ROOT/dist"
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

# ---- Output helpers -------------------------------------------------------------

if [[ -t 1 ]]; then
	BOLD=$'\e[1m' GREEN=$'\e[32m' RED=$'\e[31m' YELLOW=$'\e[33m' RESET=$'\e[0m'
else
	BOLD="" GREEN="" RED="" YELLOW="" RESET=""
fi

step(){ printf '\n%s==> %s%s\n' "$BOLD" "$*" "$RESET"; }
ok(){ printf '%s✔ %s%s\n' "$GREEN" "$*" "$RESET"; }
warn(){ printf '%s! %s%s\n' "$YELLOW" "$*" "$RESET"; }
die(){ printf '%s✘ %s%s\n' "$RED" "$*" "$RESET" >&2; exit 1; }

have(){ command -v "$1" >/dev/null 2>&1; }

# ---- Tools ---------------------------------------------------------------------------

# PlatformIO may be on PATH, in ~/.platformio/penv, or only importable as a module.
pio(){
	if [[ -n "$(type -P pio)" ]]; then
		command pio "$@"
	elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
		"$HOME/.platformio/penv/bin/pio" "$@"
	elif python3 -c "import platformio" >/dev/null 2>&1; then
		python3 -m platformio "$@"
	else
		return 127
	fi
}

has_pio(){ pio --version >/dev/null 2>&1; }

has_sdl(){
	pkg-config --exists sdl2 2>/dev/null || have sdl2-config
}

install_pio(){
	step "Installing PlatformIO"
	have python3 || die "Python 3 is required. Install it from https://www.python.org/downloads/"
	if have pipx; then
		pipx install platformio
	else
		python3 -m pip install --user --upgrade platformio ||
			python3 -m pip install --user --upgrade --break-system-packages platformio
	fi
	has_pio || die "PlatformIO installed, but 'pio' isn't on your PATH. Add ~/.local/bin to PATH and re-run."
	ok "PlatformIO $(pio --version | awk '{print $NF}')"
}

install_sim_deps(){
	step "Installing simulator dependencies (CMake, SDL2)"
	if have apt-get; then
		sudo apt-get update -q && sudo apt-get install -y -q cmake g++ libsdl2-dev pkg-config
	elif have brew; then
		brew install cmake sdl2 pkg-config
	elif have dnf; then
		sudo dnf install -y cmake gcc-c++ SDL2-devel pkgconf
	elif have pacman; then
		sudo pacman -S --needed --noconfirm cmake gcc sdl2 pkgconf
	else
		die "Couldn't find a package manager. Install CMake, a C++ compiler and SDL2 yourself."
	fi
}

need_pio(){ has_pio || install_pio; }

need_sim(){
	if ! have cmake || ! has_sdl || ! { have c++ || have g++ || have clang++; }; then
		install_sim_deps
	fi
}

# ---- Commands ----------------------------------------------------------------------

cmd_setup(){
	need_pio
	need_sim
	cmd_doctor
}

cmd_doctor(){
	step "Checking your setup"
	local missing=0
	if has_pio; then ok "PlatformIO $(pio --version | awk '{print $NF}')"; else warn "PlatformIO missing (./stardroid.sh setup)"; missing=1; fi
	if have cmake; then ok "$(cmake --version | head -1)"; else warn "CMake missing"; missing=1; fi
	if has_sdl; then ok "SDL2 found"; else warn "SDL2 missing"; missing=1; fi
	if have git; then ok "$(git --version)"; else warn "git missing (CMake fetches LovyanGFX with it)"; missing=1; fi
	local ports
	ports=$(list_ports)
	if [[ -n "$ports" ]]; then ok "Serial ports: $(echo "$ports" | tr '\n' ' ')"; else warn "No watch detected over USB"; fi
	return $missing
}

cmd_build(){
	need_pio
	step "Building firmware"
	(cd "$ROOT" && pio run)
	ok "Firmware: $FW_BUILD/firmware.bin"
}

cmd_image(){
	cmd_build
	step "Merging a single flashable image"
	local esptool="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
	local bootapp="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
	[[ -f "$esptool" && -f "$bootapp" ]] || die "PlatformIO packages not found under ~/.platformio"
	mkdir -p "$DIST"
	python3 "$esptool" --chip esp32s3 merge_bin -o "$DIST/stardroid-clockstar.bin" \
		--flash_mode dio --flash_size 4MB \
		0x0 "$FW_BUILD/bootloader.bin" 0x8000 "$FW_BUILD/partitions.bin" \
		0xe000 "$bootapp" 0x10000 "$FW_BUILD/firmware.bin" >/dev/null
	ok "Image: $DIST/stardroid-clockstar.bin (flash at 0x0)"
}

list_ports(){
	ls /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* 2>/dev/null || true
}

pick_port(){
	if [[ -n "${1:-}" ]]; then
		echo "$1"
		return
	fi
	local ports
	ports=$(list_ports)
	[[ -n "$ports" ]] || die "No watch found. Plug it in with a data USB-C cable and turn it on."
	if [[ $(echo "$ports" | wc -l) -gt 1 ]]; then
		warn "Several serial ports found, using the first. Pass one explicitly: ./stardroid.sh flash /dev/ttyACM1" >&2
	fi
	echo "$ports" | head -1
}

cmd_flash(){
	need_pio
	local port
	port=$(pick_port "${1:-}")
	step "Building and flashing to $port"
	(cd "$ROOT" && pio run -t upload --upload-port "$port")
	ok "Flashed. The watch restarts into StarDroid."
}

cmd_monitor(){
	need_pio
	local port
	port=$(pick_port "${1:-}")
	step "Serial monitor on $port (Ctrl+C to quit)"
	(cd "$ROOT" && pio device monitor --port "$port")
}

cmd_deploy(){
	cmd_test
	cmd_flash "${1:-}"
	cmd_monitor "${1:-}"
}

build_sim(){
	need_sim
	step "Building simulator and tests"
	cmake -S "$ROOT/sim" -B "$SIM_BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
	cmake --build "$SIM_BUILD" -j "$JOBS"
}

cmd_test(){
	build_sim
	step "Running the test program"
	"$SIM_BUILD/stardroid-tests" "$@"
}

cmd_sim(){
	build_sim
	step "Starting the simulator (arrows = Up/Down, Enter = Select, Esc = Back)"
	"$SIM_BUILD/stardroid-sim"
}

cmd_screenshots(){
	build_sim
	python3 -c "import PIL" >/dev/null 2>&1 || python3 -m pip install --user pillow
	step "Rendering docs/screenshots"
	"$ROOT/sim/screenshots.sh" "$SIM_BUILD/stardroid-sim"
}

cmd_clean(){
	step "Removing build output"
	rm -rf "$ROOT/.pio" "$ROOT/build" "$DIST"
	ok "Clean"
}

cmd_all(){
	cmd_test
	cmd_image
	step "All done"
	ok "Tests passed, and the firmware image is in dist/"
	if [[ -n "$(list_ports)" ]]; then
		echo "  Watch detected. Flash it with: ./stardroid.sh flash"
	else
		echo "  Plug in the watch, then run: ./stardroid.sh flash"
	fi
}

cmd_help(){
	cat <<EOF
${BOLD}StarDroid helper${RESET}

Usage: ./stardroid.sh [command] [args]

  ${BOLD}(no command)${RESET}     Install what's missing, run tests, build a flashable image
  setup            Install PlatformIO, CMake and SDL2, then check everything
  doctor           Check tools and look for a connected watch
  test [filter]    Build and run the test program (filter: part of a test name)
  build            Build the firmware
  image            Build and merge dist/stardroid-clockstar.bin (flash at 0x0)
  flash [port]     Build and flash the watch over USB
  monitor [port]   Open the serial monitor
  deploy [port]    Test, flash, then open the monitor
  sim              Run the desktop simulator
  screenshots      Regenerate docs/screenshots
  clean            Delete build output
  help             Show this help
EOF
}

# ---- Dispatch ------------------------------------------------------------------------

main(){
	local cmd="${1:-all}"
	[[ $# -gt 0 ]] && shift
	case "$cmd" in
		all) cmd_all ;;
		setup) cmd_setup ;;
		doctor) cmd_doctor ;;
		test|tests) cmd_test "$@" ;;
		build) cmd_build ;;
		image) cmd_image ;;
		flash|upload) cmd_flash "$@" ;;
		monitor) cmd_monitor "$@" ;;
		deploy) cmd_deploy "$@" ;;
		sim|simulator) cmd_sim ;;
		screenshots) cmd_screenshots ;;
		clean) cmd_clean ;;
		help|-h|--help) cmd_help ;;
		*) cmd_help; die "Unknown command: $cmd" ;;
	esac
}

main "$@"
