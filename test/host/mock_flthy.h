// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
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
// Mirrors src/main.cpp's serial line buffer. The fork's '!' lines are parsed out of it and it
// TRUNCATES SILENTLY, so a v1.2 scored line (base look + at=/am=/m= + the ae=/ac=/ad= accent)
// that overruns it is a real, invisible bug. run.sh asserts this has not drifted from the
// firmware's value — the wire-budget guard only means something if the two agree.
#define INPUTBUFFERLEN 96

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

// ================== SERVO TRIPWIRES — the LED-ONLY invariant ==================
// This fork promises, in its README and in ContractFlthy.h's header, that the '!' path is
// LED-EFFECTS-ONLY: it must NEVER move a holoprojector servo. main.cpp reaches the servos
// exactly two ways — by writing HP_command[] (dispatched in the HP switch at main.cpp:982)
// and by calling positionHP/twitchHP/wagHP/RCHP directly (main.cpp:1082/1138/1164/1203).
// flushCommandArray(hp, 1) also clears an HP (servo) command array.
//
// So every one of them is mocked here as a TRIPWIRE that bumps _mock_servoTouches. That
// turns the invariant into something a host guard can ASSERT on and watch go RED, instead of
// relying on these symbols merely not existing in the mock — which says nothing about a path
// that compiles fine but fires a servo at RUN time (e.g. the new beat-driven accent).
static int _mock_servoTouches = 0;

struct _ServoByte {                                  // a byte field of HP_command
  byte v = 0;
  _ServoByte& operator=(byte x) { v = x; _mock_servoTouches++; return *this; }
  operator byte() const { return v; }
};
struct _ServoInt {                                   // HP_command's int field (HPHalt)
  int v = -1;
  _ServoInt& operator=(int x) { v = x; _mock_servoTouches++; return *this; }
  operator int() const { return v; }
};
struct HPCmd {                                       // mirrors main.cpp:616-621
  _ServoByte HPFunction;
  _ServoByte HPOption1;
  _ServoInt  HPHalt;
};
static HPCmd HP_command[HPCOUNT];

inline void positionHP(byte, byte, int)  { _mock_servoTouches++; }   // main.cpp:1082
inline void twitchHP(byte, byte)         { _mock_servoTouches++; }   // main.cpp:1138
inline void RCHP(byte, byte)             { _mock_servoTouches++; }   // main.cpp:1164
inline void wagHP(byte, byte)            { _mock_servoTouches++; }   // main.cpp:1203
inline void flushCommandArray(byte, byte type) {                     // main.cpp:1463
  if (type == 1) _mock_servoTouches++;               // type 1 == the HP (servo) command array
}
