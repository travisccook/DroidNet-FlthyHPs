// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// The I2C slave intake: the simulation fills rx and calls the firmware's i2cEvent() itself.
#pragma once
#include "Arduino.h"
struct FakeWire { std::deque<char> rx; void (*cb)(int) = 0;
  void begin(int) {} void onReceive(void (*f)(int)) { cb = f; }
  int available() { return (int)rx.size(); } int read() { char c = rx.front(); rx.pop_front(); return (unsigned char)c; } };
extern FakeWire Wire;
