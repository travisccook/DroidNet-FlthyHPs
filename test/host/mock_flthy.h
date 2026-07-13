// Minimal host mock of the Flthy board API surface that ContractFlthy.h uses, to
// TYPE-CHECK the firmware layer on the host (NOT a behavioral sim). Signatures
// mirror src/main.cpp / Adafruit_NeoPixel as verified 2026-07-12:
//   Adafruit_NeoPixel::{setPixelColor(uint16_t,uint32_t),show(),setBrightness(uint8_t)}
//   neoStrips[HPCOUNT] (main.cpp:667); LED_command[HPCOUNT].LEDFunction (main.cpp:608)
//   enableTwitchLED/enableTwitchHP/startEnableTwitchLED/startEnableTwitchHP/offcoloroverride
//     (315/335/574/575/619)
//   ledOFF(byte) (1205), varResets(byte) (1460); millis/random/map/Serial (Arduino core)
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t byte;
typedef bool    boolean;

#define HPCOUNT 3
#define NEO_JEWEL_LEDS 7

// ---- Arduino core stubs ----
static unsigned long _mock_millis = 0;
inline unsigned long millis() { return _mock_millis; }
inline long random(long a, long b) { (void)b; return a; }   // deterministic for type-check
inline long random(long b) { (void)b; return 0; }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
#define F(str) (str)                                   // flash-string macro no-op on host

// Records what the fork writes (Serial.last) so a host guard can assert on the ack line.
struct _MockSerial {
  char last[128] = {0};
  void clear() { last[0] = 0; }
  void print(const char* s) {
    size_t n = strlen(last);
    snprintf(last + n, sizeof(last) - n, "%s", s);
  }
  void print(int v) {
    size_t n = strlen(last);
    snprintf(last + n, sizeof(last) - n, "%d", v);
  }
  void println(const char* s) { print(s); print("\r\n"); }
  void println(int v)         { print(v); print("\r\n"); }
} Serial;

// ---- Adafruit_NeoPixel surface (only what the fork calls) ----
// Records the staged pixels and the last LATCHED frame (show()) so a host guard can
// assert on what the jewel would actually display, not just on internal fork state.
struct Adafruit_NeoPixel {
  uint32_t staged[NEO_JEWEL_LEDS] = {0};     // written by setPixelColor()
  uint32_t shown[NEO_JEWEL_LEDS]  = {0};     // snapshot at the last show()
  int      showCount = 0;
  void setPixelColor(uint16_t i, uint32_t c) { if (i < NEO_JEWEL_LEDS) staged[i] = c; }
  void show() { memcpy(shown, staged, sizeof(staged)); showCount++; }
  void setBrightness(uint8_t) {}
};
static Adafruit_NeoPixel neoStrips[HPCOUNT];

// ---- board globals the fork references (main.cpp) ----
struct LEDCmd {
  byte LEDFunction = 0;
  byte LEDOption1 = 0;
  byte LEDOption2 = 0;
  int  LEDHalt = -1;
};
static LEDCmd  LED_command[HPCOUNT];
static byte    enableTwitchLED[HPCOUNT]      = {1, 2, 2};
static boolean enableTwitchHP[HPCOUNT]       = {true, true, true};
static const byte startEnableTwitchLED[HPCOUNT] = {1, 2, 2};
static const boolean startEnableTwitchHP[HPCOUNT] = {true, true, true};   // main.cpp:575
static boolean offcoloroverride[HPCOUNT]     = {false, false, false};

// ---- board effect primitives the fork calls ----
inline void ledOFF(byte) {}
inline void varResets(byte) {}
