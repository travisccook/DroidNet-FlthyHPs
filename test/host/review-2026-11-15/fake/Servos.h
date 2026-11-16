#pragma once
#include "Arduino.h"
extern long g_servoMoves;
struct Servos { Servos(uint8_t = 0x40) {} static void move(ulong) {} 
  void moveTo(byte, ulong, uint) { g_servoMoves++; } void moveTo(byte, ulong, uint, uint) { g_servoMoves++; } };
