// imu.h — minimal QMI8658 accelerometer driver with pin auto-detect
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace imu {

static const uint8_t ADDRS[2] = {0x6B, 0x6A};
static uint8_t addr = 0;
static int sdaPin = -1, sclPin = -1;
static bool present = false;

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static bool writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg); Wire.write(v);
  return Wire.endTransmission() == 0;
}

// Try a list of SDA/SCL pairs; returns true if a QMI8658 (WHO_AM_I = 0x05) answers.
static bool detect() {
#if CONFIG_IDF_TARGET_ESP32S3
  // Display uses 39-42,45; backlight 46/48; SD 14-18,21; USB 19/20; BOOT 0; LED 38.
  static const int8_t pairs[][2] = {
    {47, 48}, {48, 47}, {11, 10}, {10, 11}, {1, 2}, {2, 1}, {3, 4}, {4, 3},
    {5, 6}, {6, 5}, {7, 8}, {8, 7}, {12, 13}, {13, 12}, {9, 10}, {10, 9},
  };
#else
  static const int8_t pairs[][2] = {{0, 1}};  // no IMU on the C6 board
  return false;
#endif
  for (auto &p : pairs) {
    if (!Wire.begin(p[0], p[1], 400000)) continue;
    for (uint8_t a : ADDRS) {
      addr = a;
      uint8_t who = 0;
      if (readRegs(0x00, &who, 1) && who == 0x05) {
        sdaPin = p[0]; sclPin = p[1];
        // CTRL1: address auto-increment. CTRL2: accel ±4 g, ~235 Hz. CTRL7: accel on.
        writeReg(0x02, 0x40);
        writeReg(0x03, 0x15);
        writeReg(0x08, 0x01);
        delay(20);
        present = true;
        return true;
      }
    }
    Wire.end();
  }
  return false;
}

// Acceleration in g (raw chip axes)
static bool readAccel(float &ax, float &ay, float &az) {
  if (!present) return false;
  uint8_t b[6];
  if (!readRegs(0x35, b, 6)) return false;
  const float k = 1.0f / 8192.0f;   // ±4 g
  ax = (int16_t)(b[0] | (b[1] << 8)) * k;
  ay = (int16_t)(b[2] | (b[3] << 8)) * k;
  az = (int16_t)(b[4] | (b[5] << 8)) * k;
  return true;
}

} // namespace imu
