// Part of the DroidNet FlthyHPs fork's native brightness command -- an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// src/droidnet/native_ext.h -- the pure parser for the fork's one native command,
// library id flthy.led.brightness. No Arduino, NeoPixel or contract includes, so the host
// suite can test it (test/host/test_native_ext.cpp, test/host/fuzz_native_ext.cpp).
//
// Wire form, the whole line after loop()'s trim():
//
//   H <unit> B <level>          e.g. HAB128, HFB255, HTB1
//
//   unit   F R T X Y Z A, uppercase only, fanned out exactly like the stock designators
//          (src/main.cpp): F,X,Y,A -> HP0 (front); R,X,Z,A -> HP1 (rear); T,Y,Z,A -> HP2 (top)
//   B      the verb letter. It is load-bearing: if the H is ever lost, the tail ("AB128")
//          has typeState 'B'-'0' = 18, which the stock designator parser never acts on.
//          Without it the tail of "HA101" would be "A101", which centres every HP servo.
//   level  1 to 3 ASCII digits whose value is 1-255. Leading zeros are accepted (HAB007 is 7);
//          0, 00 and 000 are not, because setBrightness(0) wipes the pixel buffer.
//
// The line must end right after the level. Anything else is rejected with no side effect:
// no clamp, no reply, and mask/level are left exactly as they were.
//
// `piped` is true when loop()'s stock "|<n>" split fired on this line. The split has already
// removed the pipe, and neither `halt` (atol, 16-bit int on AVR) nor `pipeIndex` (reset to -1)
// can say so afterwards, so main.cpp records it in `dnPiped` and passes it in. Any piped line
// is rejected: HAB128|5, HAB128|-1, HAB128|65535 and HAB128| are all ignored.
//
// AVR-safe: `int` is 16 bits on the ATmega2560. At most 3 digits are accumulated, in a
// uint16_t, so the largest value ever held is 999.
#pragma once
#include <stdint.h>

// Jewel mask for a unit letter: bit 0 = HP0 (front), bit 1 = HP1 (rear), bit 2 = HP2 (top).
// Returns 0 for anything that is not an uppercase F R T X Y Z A.
// Deliberately an if-chain, not a switch: avr-gcc turns a dense switch over 'A'..'Z' into a
// 26-byte lookup table in .data, which costs SRAM on the ATmega2560. This costs none.
inline uint8_t nativeExtUnitMask(char unit) {
  uint8_t mask = 0x00U;
  if (unit == 'F' || unit == 'X' || unit == 'Y' || unit == 'A') {
    mask |= 0x01U;                           // HP0, front
  }
  if (unit == 'R' || unit == 'X' || unit == 'Z' || unit == 'A') {
    mask |= 0x02U;                           // HP1, rear
  }
  if (unit == 'T' || unit == 'Y' || unit == 'Z' || unit == 'A') {
    mask |= 0x04U;                           // HP2, top
  }
  return mask;
}

// Parses one whole line. On success writes the jewel mask and the level (1-255) and returns
// true. On any failure returns false and leaves mask and level untouched. Never reads past
// the terminating NUL.
inline bool nativeExtParse(const char* line, bool piped, uint8_t& mask, uint8_t& level) {
  if (piped || line == nullptr || line[0] != 'H') {
    return false;
  }
  const uint8_t unitMask = nativeExtUnitMask(line[1]);
  if (unitMask == 0U || line[2] != 'B') {    // line[2] is read only when line[1] was a unit
    return false;
  }
  uint16_t value = 0U;
  uint8_t digits = 0U;
  const char* cursor = &line[3];
  while (*cursor >= '0' && *cursor <= '9') {
    if (digits == 3U) {
      return false;                          // a fourth digit
    }
    value = static_cast<uint16_t>((value * 10U) + static_cast<uint16_t>(*cursor - '0'));
    ++digits;
    ++cursor;
  }
  if (digits == 0U || *cursor != '\0' || value == 0U || value > 255U) {
    return false;
  }
  mask = unitMask;
  level = static_cast<uint8_t>(value);
  return true;
}
