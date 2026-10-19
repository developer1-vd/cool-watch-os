# StarDroid: an Android-style OS for the CircuitMess Clockstar 2.0

StarDroid is custom firmware for the [CircuitMess Clockstar 2.0](https://circuitmess.com/)
smartwatch (ESP32-S3, 128×128 colour screen, 4 buttons). The Clockstar is a
microcontroller, so real Android can't run on it. StarDroid copies Android and
Wear OS where it can: apps are **Activities** on a **back stack**, started with
**Intents**, and the system UI has a status bar, a quick-settings shade,
heads-up notifications, toasts and a power menu.

<p>
<img src="docs/screenshots/boot.png" width="160">
<img src="docs/screenshots/face-pixel.png" width="160">
<img src="docs/screenshots/face-analog.png" width="160">
<img src="docs/screenshots/face-neon.png" width="160">
<img src="docs/screenshots/quick-settings.png" width="160">
<img src="docs/screenshots/drawer.png" width="160">
<img src="docs/screenshots/fit.png" width="160">
<img src="docs/screenshots/heart-rate.png" width="160">
<img src="docs/screenshots/stopwatch.png" width="160">
<img src="docs/screenshots/dino.png" width="160">
</p>

## Features

- **Boot animation** with bouncing dots and a light sweep across the wordmark.
- **First-boot setup wizard**: welcome, time, date, 12/24 h, accent colour,
  watch face, daily step goal, and a tour of the buttons.
- **5 watch faces**: Pixel, Analog (smooth sweeping second hand), Neon
  (seconds trace the screen edge), Rings, and Terminal (`adb shell` style).
  Hold SELECT on the face to change it.
- **Material You-style accent colours**: Android green, Ocean, Lavender,
  Coral, Sunny, Bubblegum.
- **System UI**: status bar, quick-settings tiles (flashlight, brightness,
  sound, Do Not Disturb, theme, settings), notification list, heads-up
  notifications, toasts, power menu (hold BACK), screen timeout, and a
  notification LED that blinks while something is unread (red while charging,
  green when full).
- **Fit**: daily steps from the IMU's hardware pedometer (it keeps counting
  while the watch sleeps), distance, calories, goal ring, and a 7-day chart.
- **Heart rate**: see the note below.
- **Clock**: stopwatch with laps, countdown timer, and a daily alarm with
  snooze. Timers and alarms wake the watch from sleep.
- **Tools**: calendar, bubble level, and a flashlight with white, red and warm
  modes.
- **Games**: Snake and Dino run.
- **Settings**: brightness, screen timeout, watch face, colour, sound, DND,
  24-hour time, time and date, step goal, battery, About watch, and reset.
  Tap *Build number* 7 times to unlock developer options.

### About heart rate

The Clockstar 2.0 has **no heart-rate sensor** (no optical PPG). Instead of
showing made-up numbers, the Heart rate app measures your pulse the manual
way: feel your pulse on your wrist or neck and tap SELECT with each beat. It
works out your BPM, shows a zone (low, normal resting, elevated) and keeps
your last 5 readings. It is not a medical device.

## Buttons

| Button | On the watch face | In apps |
| --- | --- | --- |
| UP | Quick settings | Move up / previous |
| DOWN | Notifications | Move down / next |
| SELECT | App drawer (hold: change face) | Select / confirm |
| BACK | Turn the screen off | Back (hold: power menu) |

## Building and flashing

You need [PlatformIO](https://platformio.org/install/cli).

```sh
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial log
```

The ESP32-S3 flashes over its built-in USB, so no driver or button dance
should be needed. Every CI run also attaches a
merged `stardroid-clockstar.bin` that you can flash at offset `0x0` with
esptool or the [ESP web flasher](https://espressif.github.io/esptool-js/).

**To go back to the stock firmware**, flash the image from the
[CircuitMess Artemis firmware releases](https://github.com/CircuitMess/GC_Artemis-Firmware/releases).

## Desktop simulator

The same OS and app code runs on your computer on top of LovyanGFX's SDL2
backend, so you can work on the UI without flashing.

```sh
sudo apt install libsdl2-dev        # or: brew install sdl2
cmake -S sim -B build/sim && cmake --build build/sim -j
./build/sim/stardroid-sim
```

Keys: arrows for Up and Down, Enter or Right for Select, Esc or Left for Back.

The simulator also has a headless, scripted mode. It is used to render
`docs/screenshots` (`sim/screenshots.sh build/sim/stardroid-sim`) and as a
smoke test in CI:

```sh
./build/sim/stardroid-sim --out shots --script "w3300 s s p:setup"
```

## How it works

```
src/
  hal/       Hardware layer: Clockstar 2.0 (pins, display, RTC, IMU, battery,
             backlight, buzzer, light sleep) and the SDL simulator
  os/        The framework: activity manager and back stack, input
             (click / long-press / repeat), power management, notifications,
             system UI, settings, widgets and icons
  apps/      Watch faces and apps
sim/         Desktop simulator
```

- **Activities**: each app subclasses `os::Activity` (`onCreate`, `onResume`,
  `onPause`, `onDestroy`, `onUpdate`, `onDraw`, `onKey`) and is registered with
  an `os::AppInfo`. `os::startActivity({"timer"})` pushes it with a Material
  slide transition, and BACK pops it.
- **Services**: `os::Service` covers background work such as timers and
  alarms. While the screen is off the ESP32-S3 sits in light sleep and wakes
  on any button, or when the next service deadline comes up.
- **Rendering**: everything draws into a 128×128 RGB565 sprite, which is
  pushed to the ST7735S over 40 MHz SPI at up to 40 fps.

### Hardware notes

Pin maps, display offsets, the BM8563 RTC, the LSM6DS3TR-C IMU and the
battery-sense calibration come from CircuitMess's open-source
[Artemis firmware](https://github.com/CircuitMess/GC_Artemis-Firmware). The
board revision is read from eFuse, so revisions 1 to 3 all get the right pins
and display offsets.

## License

GPL-3.0, see [LICENSE](LICENSE).
