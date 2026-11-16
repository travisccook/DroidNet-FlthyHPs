// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// The servo board, reduced to a tripwire: every moveTo() is counted (g_servoMoves). Nothing moves.
#pragma once
#include "Arduino.h"
extern long g_servoMoves;
struct Servos { Servos(uint8_t = 0x40) {} static void move(ulong) {}
  void moveTo(byte, ulong, uint) { g_servoMoves++; } void moveTo(byte, ulong, uint, uint) { g_servoMoves++; } };
