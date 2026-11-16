// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// test/host/sim/fake/Arduino.h — the least of the Arduino core that src/main.cpp uses, so the REAL
// firmware compiles and runs on a host (test/host/sim). millis()/micros() read the simulation's clock.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <deque>
typedef uint8_t byte; typedef bool boolean; typedef unsigned int uint; typedef unsigned long ulong;
#define PROGMEM
#define F(s) (s)
#define pgm_read_word(p) (*(p))
#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define LOW 0
extern uint32_t g_ms;   // fake clock
extern long g_pulseIn;
inline unsigned long millis() { return g_ms; }
inline unsigned long micros() { return (unsigned long)g_ms * 1000UL; }
inline int analogRead(uint8_t) { return 123; }
inline void randomSeed(unsigned long s) { srand((unsigned)s); }
inline long random(long a, long b) { if (b <= a) return a; return a + rand() % (b - a); }
inline long random(long b) { return random(0, b); }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
inline void pinMode(int, int) {}
inline void noInterrupts() {}
inline void interrupts() {}
inline void digitalWrite(int, int) {}
inline unsigned long pulseIn(int, int) { return g_pulseIn; }
struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c) {}
  String(const std::string& x) : s(x) {}
  void reserve(unsigned) {}
  unsigned length() const { return (unsigned)s.size(); }
  void trim() { size_t a = 0, b = s.size(); while (a < b && isspace((unsigned char)s[a])) a++; while (b > a && isspace((unsigned char)s[b-1])) b--; s = s.substr(a, b - a); }
  int indexOf(char c) const { size_t p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned a) const { return a >= s.size() ? String("") : String(s.substr(a)); }
  String substring(unsigned a, unsigned b) const { if (a > b) { unsigned t = a; a = b; b = t; } if (a >= s.size()) return String(""); return String(s.substr(a, b - a)); }
  long toInt() const { return atol(s.c_str()); }
  void toCharArray(char* buf, unsigned n) const { if (!n) return; unsigned k = (unsigned)s.size(); if (k > n - 1) k = n - 1; memcpy(buf, s.data(), k); buf[k] = 0; }
  String& operator=(const char* c) { s = c; return *this; }
  String& operator+=(char c) { s += c; return *this; }
};
struct FakeSerial {
  std::deque<char> rx; std::string tx;
  void begin(long) {}
  int available() { return (int)rx.size(); }
  int read() { if (rx.empty()) return -1; char c = rx.front(); rx.pop_front(); return (unsigned char)c; }
  int availableForWrite() { return 63; }
  size_t write(const uint8_t* b, size_t n) { tx.append((const char*)b, n); return n; }
  void print(const char* c) { tx += c; }
  void print(int v) { tx += std::to_string(v); }
  void print(const String& x) { tx += x.s; }
  void println(const char* c) { tx += c; tx += "\r\n"; }
  void println(int v) { tx += std::to_string(v); tx += "\r\n"; }
  void println(const String& x) { tx += x.s; tx += "\r\n"; }
};
extern FakeSerial Serial;
#define AVRDIV(a,b) ((b)==0 ? -1 : (a)/(b))   /* avr-libc __divmodhi4: x/0 == -1, no trap */
