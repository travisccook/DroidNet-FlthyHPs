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
// Calls that BLOCK on the board (a write to a full TX ring, pulseIn) let simulated time pass through
// this hook, so bytes keep arriving on RX while loop() is stuck, as they do on the Mega.
extern void (*g_simBlock)(uint64_t us);
extern uint64_t (*g_simNowUs)();
inline unsigned long millis() { return g_ms; }
inline unsigned long micros() { return (unsigned long)g_ms * 1000UL; }
inline int analogRead(uint8_t) { return 123; }
inline void randomSeed(unsigned long s) { srand((unsigned)s); }
extern long g_dice10[10];   // every random(0, 10) the firmware draws, by value (the projector's slot dice, v1.4 N7)
inline long random(long a, long b) { if (b <= a) return a; const long r = a + rand() % (b - a); if (a == 0 && b == 10) g_dice10[r]++; return r; }
inline long random(long b) { return random(0, b); }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
inline void pinMode(int, int) {}
inline void noInterrupts() {}
inline void interrupts() {}
inline void digitalWrite(int, int) {}
// pulseIn() as RCHP() calls it (no timeout argument: the core's default is 1 s). With no signal it
// blocks for the whole second. With one, it waits for the next pulse and measures it: the bound
// modelled here is the worst case, one full 20 ms RC frame plus the pulse.
#define SIM_RC_FRAME_US 20000UL
inline unsigned long pulseIn(int, int, unsigned long timeout = 1000000UL) {
  if (g_pulseIn <= 0) { g_simBlock(timeout); return 0; }
  g_simBlock(SIM_RC_FRAME_US + (uint64_t)g_pulseIn);
  return (unsigned long)g_pulseIn;
}
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
// Serial as an AVR HardwareSerial: the TX ring holds 63 bytes and drains at 9600 baud (10 bits a
// byte). A write that finds it full BLOCKS until a byte has left, exactly as the core's write() does.
struct FakeSerial {
  std::deque<char> rx; std::string tx;
  double txDoneAt = 0;                                  // when the last queued byte will have left
  uint64_t blockedUs = 0, maxStallUs = 0;               // time write() spent waiting: total, longest single call
  static constexpr double BYTE_US = 1e6 * 10.0 / 9600.0;
  void begin(long) {}
  int available() { return (int)rx.size(); }
  int read() { if (rx.empty()) return -1; char c = rx.front(); rx.pop_front(); return (unsigned char)c; }
  int pending() { double n = (txDoneAt - (double)g_simNowUs()) / BYTE_US; return n <= 0 ? 0 : (int)ceil(n); }
  int availableForWrite() { int p = pending(); return p >= 63 ? 0 : 63 - p; }
  uint64_t put(char c) {
    uint64_t waited = 0;
    while (pending() >= 64) { g_simBlock(200); waited += 200; }    // 63 in the ring + the one being shifted out
    double now = (double)g_simNowUs(); if (txDoneAt < now) txDoneAt = now;
    txDoneAt += BYTE_US; tx += c; blockedUs += waited;
    return waited;
  }
  size_t write(const uint8_t* b, size_t n) { uint64_t w = 0; for (size_t i = 0; i < n; i++) w += put((char)b[i]); if (w > maxStallUs) maxStallUs = w; return n; }
  void print(const char* c) { write((const uint8_t*)c, strlen(c)); }
  void print(int v) { print(std::to_string(v).c_str()); }
  void print(const String& x) { print(x.s.c_str()); }
  void println(const char* c) { print(c); print("\r\n"); }
  void println(int v) { print(v); print("\r\n"); }
  void println(const String& x) { print(x); print("\r\n"); }
};
extern FakeSerial Serial;
#define AVRDIV(a,b) ((b)==0 ? -1 : (a)/(b))   /* avr-libc __divmodhi4: x/0 == -1, no trap */
