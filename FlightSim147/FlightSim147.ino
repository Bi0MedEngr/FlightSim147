// FlightSim147 — procedural voxel flight sim for Waveshare 1.47" ST7789 (172x320)
//
// Library: "GFX Library for Arduino" by moononournation (Arduino_GFX), v1.4+
// Board:   ESP32 Arduino core 3.x
//
// BOOT button:
//   1 tap          → next aircraft (jet, prop, biplane, helicopter)
//   2 taps         → toggle chase (3rd person) / cockpit view
//   3 taps         → fly over a brand-new procedurally generated world
//   hold ~1 s      → toggle tilt-to-fly / autopilot (boards with an IMU, e.g. 1.47B);
//                    also re-centres "level" to however you're holding it
//   hold 4 s       → Wi-Fi updater: join Wi-Fi "FlightSim147", open http://192.168.4.1
//
// The ESP32-S3 build auto-detects the ESP32-S3-LCD-1.47B (QMI8658 IMU, backlight on GPIO46)
// versus the original ESP32-S3-LCD-1.47 (no IMU, backlight on GPIO48).

// ======== choose ONE ========
#define BOARD_WAVESHARE_ESP32S3_LCD_147   // Waveshare ESP32-S3-LCD-1.47 or 1.47B (auto-detected)
// #define BOARD_WAVESHARE_ESP32C6_LCD_147 // Waveshare ESP32-C6-LCD-1.47 dev board
// #define BOARD_CUSTOM_WIRING             // bare 1.47" module wired to any ESP32

// Tilt axis tweaks. If tilting feels swapped or backwards, change these.
#define TILT_SWAP_XY   0
#define TILT_INVERT_X  0
#define TILT_INVERT_Y  0

// #define USE_JOYSTICK
#define JOY_X_PIN 4
#define JOY_Y_PIN 5

#include <Arduino_GFX_Library.h>
#include "flightsim_engine.h"
#include "imu.h"
#include "ota.h"

#if defined(BOARD_WAVESHARE_ESP32S3_LCD_147)
  #define TFT_MOSI 45
  #define TFT_SCLK 40
  #define TFT_CS   42
  #define TFT_DC   41
  #define TFT_RST  39
  #define TFT_BL   48      // original board
  #define TFT_BL_B 46      // 1.47B
  #define BTN_PIN  0
#elif defined(BOARD_WAVESHARE_ESP32C6_LCD_147)
  #define TFT_MOSI 6
  #define TFT_SCLK 7
  #define TFT_CS   14
  #define TFT_DC   15
  #define TFT_RST  21
  #define TFT_BL   22
  #define BTN_PIN  9
#else  // BOARD_CUSTOM_WIRING — edit to match your wiring
  #define TFT_MOSI 11
  #define TFT_SCLK 12
  #define TFT_CS   10
  #define TFT_DC   9
  #define TFT_RST  8
  #define TFT_BL   7
  #define BTN_PIN  0
#endif

#if defined(BOARD_WAVESHARE_ESP32C6_LCD_147)
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
#else
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
#endif
#define SPI_HZ 80000000   // drop to 40000000 if you see glitches (esp. on jumper wires)

// rotation 1 = landscape (320x172). IPS = true. 172x320 panel, col offset 34.
Arduino_GFX *panel = new Arduino_ST7789(bus, TFT_RST, 1 /*rotation*/, true /*IPS*/,
                                        172, 320, 34, 0, 34, 0);
Arduino_Canvas *gfx = new Arduino_Canvas(fs::W, fs::H, panel);

fs::Plane plane;
uint32_t worldSeed;
uint32_t lastMs;
float fpsAvg = 0;

bool tiltMode = false;
bool chaseView = true;
int aircraftType = 1;
float tiltX0 = 0, tiltY0 = 0;      // neutral angles (rad)
float tiltX = 0, tiltY = 0;        // filtered stick values

enum Mode { FLY, UPDATER };
Mode mode = FLY;

// ---------------------------------------------------------------------------
static void message(const char *l1, const char *l2 = nullptr, const char *l3 = nullptr) {
  gfx->fillScreen(0x0000);
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(2);
  int y = l3 ? 45 : 60;
  for (const char *l : {l1, l2, l3}) {
    if (!l) continue;
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(l, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((fs::W - w) / 2, y);
    gfx->print(l);
    y += 28;
  }
  gfx->flush();
}

static bool tiltAngles(float &rx, float &ry) {
  float ax, ay, az;
  if (!imu::readAccel(ax, ay, az)) return false;
#if TILT_SWAP_XY
  float t = ax; ax = ay; ay = t;
#endif
  rx = atan2f(ay, az);                          // roll around the long axis
  ry = atan2f(-ax, sqrtf(ay * ay + az * az));   // pitch
#if TILT_INVERT_X
  rx = -rx;
#endif
#if TILT_INVERT_Y
  ry = -ry;
#endif
  return true;
}

static void recentreTilt() {
  float sx = 0, sy = 0; int n = 0;
  for (int i = 0; i < 10; i++) {
    float rx, ry;
    if (tiltAngles(rx, ry)) { sx += rx; sy += ry; n++; }
    delay(5);
  }
  if (n) { tiltX0 = sx / n; tiltY0 = sy / n; }
  tiltX = tiltY = 0;
}

static void newWorld() {
  worldSeed = esp_random();
  char buf[24];
  snprintf(buf, sizeof buf, "World #%lu", (unsigned long)(worldSeed % 100000));
  message("Generating", buf);
  fs::generateWorld(worldSeed);
  plane = fs::Plane();
  plane.type = aircraftType;
  plane.rng = worldSeed | 1;
  plane.x = (worldSeed >> 8) & fs::MAP_MASK;
  plane.y = (worldSeed >> 16) & fs::MAP_MASK;
  plane.alt = fs::groundAt(plane.x, plane.y) + 60;
  lastMs = millis();
}

static void startUpdater() {
  mode = UPDATER;
  message("Wi-Fi updater", "Join: FlightSim147", "Open 192.168.4.1");
  ota::begin();
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  int blPin = TFT_BL;
#if defined(BOARD_WAVESHARE_ESP32S3_LCD_147)
  if (imu::detect()) {
    blPin = TFT_BL_B;
    Serial.printf("QMI8658 found: SDA=%d SCL=%d addr=0x%02X -> 1.47B board\n",
                  imu::sdaPin, imu::sclPin, imu::addr);
  } else {
    Serial.println("No IMU found -> original 1.47 board");
  }
#endif
  pinMode(blPin, OUTPUT);
  digitalWrite(blPin, HIGH);

  if (!gfx->begin(SPI_HZ)) {
    Serial.println("Display init failed");
    while (true) delay(1000);
  }
  fs::buildPalettes();
  if (imu::present) {
    char buf[24];
    snprintf(buf, sizeof buf, "SDA %d  SCL %d", imu::sdaPin, imu::sclPin);
    message("Tilt sensor found", buf, "Hold BOOT 1s: tilt");
    delay(1500);
  }
  newWorld();
}

void loop() {
  // ---- button handling ----
  static bool btnPrev = true;
  static uint32_t pressMs = 0, lastTapMs = 0;
  static int taps = 0;
  bool btn = digitalRead(BTN_PIN);           // LOW = pressed
  uint32_t now = millis();
  if (btnPrev && !btn) pressMs = now;
  if (!btn && mode == FLY && now - pressMs > 4000) startUpdater();
  if (!btnPrev && btn && mode == FLY) {      // released
    uint32_t held = now - pressMs;
    if (held < 600) { taps++; lastTapMs = now; }
    else if (held < 4000 && imu::present) {
      taps = 0;
      tiltMode = !tiltMode;
      if (tiltMode) recentreTilt();
    }
  }
  btnPrev = btn;
  if (taps && btn && now - lastTapMs > 400) {   // tap sequence finished
    if (taps == 1) {
      aircraftType = (aircraftType + 1) % fs::NUM_AIRCRAFT;
      plane.type = aircraftType;
    } else if (taps == 2) {
      chaseView = !chaseView;
    } else {
      newWorld();
    }
    taps = 0;
  }

  if (mode == UPDATER) {
    ota::loop();
    static uint32_t lastDraw = 0;
    size_t p = ota::progress();
    if (p && now - lastDraw > 250) {
      lastDraw = now;
      char buf[24];
      snprintf(buf, sizeof buf, "%u KB received", (unsigned)(p / 1024));
      message("Updating...", buf, "Don't unplug");
    }
    return;
  }

  // ---- flight ----
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt > 0.1f) dt = 0.1f;

  if (tiltMode) {
    float rx, ry;
    if (tiltAngles(rx, ry)) {
      float sx = (rx - tiltX0) / 0.5f, sy = (ry - tiltY0) / 0.5f;   // ±~30° = full stick
      sx = sx > 1 ? 1 : (sx < -1 ? -1 : sx);
      sy = sy > 1 ? 1 : (sy < -1 ? -1 : sy);
      tiltX += (sx - tiltX) * 0.3f;
      tiltY += (sy - tiltY) * 0.3f;
    }
    fs::updatePlane(plane, dt, true, tiltX, tiltY);
  } else {
#ifdef USE_JOYSTICK
    float sx = (analogRead(JOY_X_PIN) - 2048) / 2048.0f;
    float sy = (analogRead(JOY_Y_PIN) - 2048) / 2048.0f;
    if (fabsf(sx) < 0.1f) sx = 0;
    if (fabsf(sy) < 0.1f) sy = 0;
    fs::updatePlane(plane, dt, true, sx, sy);
#else
    fs::updatePlane(plane, dt);
#endif
  }

  uint16_t *fb = gfx->getFramebuffer();
  fs::drawFrame(plane, chaseView, fb);

  // HUD
  if (dt > 0) fpsAvg += ((1.0f / dt) - fpsAvg) * 0.1f;
  gfx->setTextSize(1);
  gfx->setTextColor(0xFFFF);
  gfx->setCursor(4, 4);
  gfx->printf("ALT %4d", (int)plane.alt * 10);
  gfx->setCursor(4, 14);
  float hdg = fmodf(-plane.heading * 57.2958f + 3600.0f, 360.0f);
  gfx->printf("HDG %03d", (int)hdg);
  gfx->setCursor(fs::W - 46, 4);
  gfx->printf("%2d FPS", (int)(fpsAvg + 0.5f));
  gfx->setCursor(fs::W - 46, 14);
  gfx->print(tiltMode ? "TILT" : "AUTO");
  gfx->setCursor(4, fs::H - 11);
  gfx->print(fs::AIRCRAFT[aircraftType].name);

  gfx->flush();
}
