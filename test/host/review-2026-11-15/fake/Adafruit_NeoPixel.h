// Fake strip with the REAL Adafruit_NeoPixel 1.15.5 brightness math (setPixelColor :3447, setBrightness :3663).
#pragma once
#include "Arduino.h"
#define NEO_GRB 0x52
#define NEO_GRBW 0xD2
#define NEO_KHZ800 0
struct Adafruit_NeoPixel {
  uint8_t pixels[7 * 3] = {0}; uint8_t brightness = 0; uint16_t numLEDs = 7;
  uint8_t shown[7 * 3] = {0}; long showCount = 0;
  void updateType(int) {} void updateLength(int) {} void setPin(int) {} void begin() {}
  void setPixelColor(uint16_t n, uint32_t c) {
    if (n < numLEDs) { uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
      if (brightness) { r = (r * brightness) >> 8; g = (g * brightness) >> 8; b = (b * brightness) >> 8; }
      pixels[n*3] = r; pixels[n*3+1] = g; pixels[n*3+2] = b; } }
  void setBrightness(uint8_t b) {
    uint8_t newBrightness = b + 1;
    if (newBrightness != brightness) {
      uint8_t c, *ptr = pixels, oldBrightness = brightness - 1; uint16_t scale;
      if (oldBrightness == 0) scale = 0; else if (b == 255) scale = 65535 / oldBrightness;
      else scale = (((uint16_t)newBrightness << 8) - 1) / oldBrightness;
      for (uint16_t i = 0; i < sizeof(pixels); i++) { c = *ptr; *ptr++ = (c * scale) >> 8; }
      brightness = newBrightness; } }
  void show() { memcpy(shown, pixels, sizeof(pixels)); showCount++; }
  uint8_t getBrightness() const { return brightness - 1; }
  uint8_t* getPixels() { return pixels; }
  uint16_t numPixels() const { return numLEDs; }
};
