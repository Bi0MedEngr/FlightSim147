// FlightSim147 — procedural voxel flight sim for Waveshare 1.47" ST7789 (172x320)
//
// Library: "GFX Library for Arduino" by moononournation (Arduino_GFX), v1.4+
// Board:   ESP32 Arduino core 3.x
//
// Pick your hardware below. The Waveshare 1.47" panel is an ST7789 IPS LCD,
// 172x320, with a 34-pixel column offset (handled below).
//
// Controls: press BOOT to fly over a brand-new procedurally generated world.
// Optional: define USE_JOYSTICK to steer with an analog thumbstick.

// ======== choose ONE ========
#define BOARD_WAVESHARE_ESP32S3_LCD_147   // Waveshare ESP32-S3-LCD-1.47 dev board
// #define BOARD_WAVESHARE_ESP32C6_LCD_147 // Waveshare ESP32-C6-LCD-1.47 dev board
// #define BOARD_CUSTOM_WIRING             // bare 1.47" module wired to any ESP32

// #define USE_JOYSTICK
#define JOY_X_PIN 4
#define JOY_Y_PIN 5

#include <Arduino_GFX_Library.h>
#include "flightsim_engine.h"

#if defined(BOARD_WAVESHARE_ESP32S3_LCD_147)
  #define TFT_MOSI 45
  #define TFT_SCLK 40
  #define TFT_CS   42
  #define TFT_DC   41
  #define TFT_RST  39
  #define TFT_BL   48
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

void newWorld() {
  worldSeed = esp_random();
  gfx->fillScreen(0x0000);
  gfx->setTextColor(0xFFFF);
  gfx->setTextSize(2);
  gfx->setCursor(60, 70);
  gfx->print("Generating world");
  gfx->setCursor(110, 95);
  gfx->printf("#%lu", (unsigned long)(worldSeed % 100000));
  gfx->flush();

  fs::generateWorld(worldSeed);
  plane = fs::Plane();
  plane.rng = worldSeed | 1;
  plane.x = (worldSeed >> 8) & fs::MAP_MASK;
  plane.y = (worldSeed >> 16) & fs::MAP_MASK;
  plane.alt = fs::groundAt(plane.x, plane.y) + 60;
  lastMs = millis();
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  if (!gfx->begin(SPI_HZ)) {           // 80 MHz SPI
    Serial.println("Display init failed");
    while (true) delay(1000);
  }
  fs::buildPalettes();
  newWorld();
}

void loop() {
  static bool btnPrev = true;
  bool btn = digitalRead(BTN_PIN);
  if (btnPrev && !btn) newWorld();       // BOOT pressed → new world
  btnPrev = btn;

  uint32_t now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt > 0.1f) dt = 0.1f;

#ifdef USE_JOYSTICK
  float sx = (analogRead(JOY_X_PIN) - 2048) / 2048.0f;
  float sy = (analogRead(JOY_Y_PIN) - 2048) / 2048.0f;
  if (fabsf(sx) < 0.1f) sx = 0;
  if (fabsf(sy) < 0.1f) sy = 0;
  fs::updatePlane(plane, dt, true, sx, sy);
#else
  fs::updatePlane(plane, dt);
#endif

  uint16_t *fb = gfx->getFramebuffer();
  fs::render(plane, fb);
  fs::drawReticle(fb);

  // HUD
  if (dt > 0) fpsAvg += ((1.0f / dt) - fpsAvg) * 0.1f;
  gfx->setTextSize(1);
  gfx->setTextColor(0xFFFF);
  gfx->setCursor(4, 4);
  gfx->printf("ALT %3d", (int)plane.alt * 10);
  gfx->setCursor(4, 14);
  float hdg = fmodf(-plane.heading * 57.2958f + 3600.0f, 360.0f);
  gfx->printf("HDG %03d", (int)hdg);
  gfx->setCursor(fs::W - 46, 4);
  gfx->printf("%2d FPS", (int)(fpsAvg + 0.5f));

  gfx->flush();
}
