# FlightSim147

A procedurally generated voxel-terrain flight sim (Comanche-style) for the **Waveshare 1.47" ST7789 LCD** (172×320) on ESP32-S3 / ESP32-C6.

![preview](preview.png)

- Each boot, or each press of **BOOT**, generates a new 256×256 wrapping island world with lakes, beaches, forest, rock and snow.
- An autopilot banks through turns and follows the terrain. It also has distance fog, a tilted horizon, and an ALT/HDG/FPS HUD.
- **Four aircraft** (jet, prop plane, biplane, helicopter), each with its own speed, handling and cruise height.
- **Chase view** (third person) or **cockpit view**.
- **Tilt-to-fly** on the ESP32-S3-LCD-1.47B. Its QMI8658 IMU is auto-detected.
- **Wi-Fi updater** for flashing without a cable, e.g. from an iPhone.
- Optional analog-joystick steering (`#define USE_JOYSTICK`).

## BOOT button
| Action | Result |
|---|---|
| 1 tap | Next aircraft: jet → prop plane → biplane → helicopter |
| 2 taps | Toggle chase (3rd-person) / cockpit view |
| 3 taps | New procedurally generated world |
| Hold ~1 s | Toggle tilt-to-fly / autopilot (1.47B only). Also sets "level" to how you're holding it |
| Hold 4 s | Wi-Fi updater mode |

## Wi-Fi update (works from iPhone)
1. Download `flightsim-esp32s3-ota.bin` (or `-esp32c6-ota.bin`) from the [latest release](../../releases/tag/latest) and save it to Files.
2. Hold **BOOT** for 4 s. The screen shows the updater info.
3. Join the Wi-Fi network **FlightSim147**. Choose "Use without internet" if asked.
4. Open **http://192.168.4.1** in Safari, pick the `-ota.bin` file, and tap Upload. The board reboots into the new firmware.

The first install still needs USB, because firmware without the updater can't receive updates over Wi-Fi.

## Play the browser demo
**https://bi0medengr.github.io/FlightSim147/demo.html** runs the same engine, ported to JavaScript. Steer by dragging, with the arrow keys, or by tilting your phone.

It also has a **Dogfight** mode (browser only for now): waves of bandits, machine guns with tracers, lock-on homing missiles, explosions, a radar and health. Keys: Space = guns, M = missile, F = start or stop the dogfight.

## Flash from your browser (no IDE)
**https://bi0medengr.github.io/FlightSim147/** works in Chrome on Android (v148+) or desktop Chrome/Edge. Plug in the board and tap Install.

The prebuilt merged images are also on the [latest release](../../releases/tag/latest). Flash them at offset `0x0`.

## Build it yourself
1. Install the **esp32 by Espressif** core **3.3.x** (4.0.0 currently breaks the GFX library) and the **GFX Library for Arduino** (moononournation).
2. Open `FlightSim147/FlightSim147.ino` and choose your board `#define` at the top.
3. ESP32-S3: Tools → USB CDC On Boot → Enabled. Then upload.

## Supported hardware
| Define | Board |
|---|---|
| `BOARD_WAVESHARE_ESP32S3_LCD_147` | Waveshare ESP32-S3-LCD-1.47 **and** 1.47B (auto-detected) |
| `BOARD_WAVESHARE_ESP32C6_LCD_147` | Waveshare ESP32-C6-LCD-1.47 |
| `BOARD_CUSTOM_WIRING` | Bare 1.47" module on any ESP32 (edit pins) |

If tilt control feels swapped or backwards, change `TILT_SWAP_XY` / `TILT_INVERT_X` / `TILT_INVERT_Y` at the top of the sketch.

## Tuning (`flightsim_engine.h`)
- `DIST`: view distance. Lower it for more FPS.
- `HSCALE`: vertical exaggeration.
- `WATER`: sea level.
- `Plane::speed`: cruise speed.

The engine is plain C++ with no Arduino dependencies, so you can compile and test it on a PC.
