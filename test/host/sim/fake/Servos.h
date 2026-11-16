// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// The servo board, reduced to a tripwire: every moveTo() is counted (g_servoMoves, and per pin). Nothing moves.
#pragma once
#include "Arduino.h"
extern long g_servoMoves;
extern long g_servoPinMoves[16];                        // the same, per servo pin (two pins a holoprojector)
struct Servos { Servos(uint8_t = 0x40) {} static void move(ulong) {}
  void moveTo(byte pin, ulong, uint) { g_servoMoves++; g_servoPinMoves[pin & 15]++; }
  void moveTo(byte pin, ulong, uint, uint) { g_servoMoves++; g_servoPinMoves[pin & 15]++; } };
