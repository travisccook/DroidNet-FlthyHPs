#pragma once
#include "Arduino.h"
struct FakeWire { std::deque<char> rx; void (*cb)(int) = 0;
  void begin(int) {} void onReceive(void (*f)(int)) { cb = f; }
  int available() { return (int)rx.size(); } int read() { char c = rx.front(); rx.pop_front(); return (unsigned char)c; } };
extern FakeWire Wire;
