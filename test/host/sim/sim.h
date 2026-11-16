// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Whole-firmware host simulation: the REAL src/main.cpp (one mechanical substitution, see
// run_sim.sh) on a fake Arduino core, a 9600-baud wire model in both directions (the RX ring that
// loses bytes when full, the TX ring that blocks a write when full) and a controllable loop-pass time.
#pragma once
#include <Arduino.h>
#include <Wire.h>
uint32_t g_ms = 0; long g_pulseIn = 0; long g_servoMoves = 0; long g_servoPinMoves[16] = {0};
void (*g_simBlock)(uint64_t) = 0; uint64_t (*g_simNowUs)() = 0;
FakeSerial Serial; FakeWire Wire;
#include "main.cpp"                      // the firmware under test
#include "sim_state.h"                   // how the scenarios read the contract layer
#include <functional>
#include <map>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace sim {
static uint64_t us = 0;                                  // simulated time
static const double BYTE_US = 1e6 * 10.0 / 9600.0;       // 8N1 at 9600 baud: 1041.67 us a byte
static uint64_t PASS = 2000;                             // default loop-pass time, us
struct WireByte { uint64_t at; char c; bool contract; };
static std::vector<WireByte> wire; static size_t wi = 0;
static std::deque<bool> rxContract;                      // parallel to Serial.rx: did a '!' line send this byte?
static bool lineContract = false, lineOther = false;     // origin of the line being assembled
static int failures = 0;
static long ledOnlyViolations = 0;
static long holdViolations = 0;                          // a servo of a unit the contract held moved by itself
static bool holdChecked = true;
static long rxOverruns = 0;                              // bytes the 64-byte UART ring had no room for
static bool ledOnlyChecked = true;                       // a scenario may switch the invariant off
static long ledTwitchStarts[HPCOUNT] = {0, 0, 0};        // stock LED auto twitches begun, per jewel
static byte prevFn[HPCOUNT] = {255, 255, 255};

// HardwareSerial's RX ring holds SERIAL_RX_BUFFER_SIZE - 1 bytes (63 on a stock Mega). run_sim.sh
// reads the size from platformio.ini, so the simulation loses the bytes the board would lose.
#ifndef SIM_RX_RING
#define SIM_RX_RING 63
#endif
static long linesHandled = 0;                            // serial lines loop() has taken

// Queue bytes on the wire, back to back at 9600 baud, no earlier than now.
// ORIGIN IS WHAT THE HOST SENT, never how the board framed it: `contract` says whether these bytes
// are (what is left of) a '!' line. A corrupted contract line keeps that origin for every byte, so a
// terminator a bit error puts inside it does not launder its tail into "stock" traffic.
inline uint64_t sayAs(const std::string& bytes, bool contract) {
  double t = (double)us;
  if (!wire.empty() && wire.back().at + BYTE_US > t) t = wire.back().at + BYTE_US;
  for (char c : bytes) { wire.push_back({(uint64_t)t, c, contract}); t += BYTE_US; }
  return (uint64_t)t;
}
// CLEAN host text, one or more whole lines: each line is tagged by its own first character.
inline uint64_t say(const char* s) {
  uint64_t end = us;
  while (*s) {
    const char* e = s; while (*e && *e != '\r' && *e != '\n') e++;
    while (*e == '\r' || *e == '\n') e++;               // the line's terminator(s) travel with it
    end = sayAs(std::string(s, e), *s == '!');
    s = e;
  }
  return end;
}
// Bytes the wire has delivered go into the UART's RX ring; one that finds it full is lost.
inline void deliverRx() {
  for (; wi < wire.size() && wire[wi].at <= us; wi++) {
    if (Serial.rx.size() >= SIM_RX_RING) { rxOverruns++; continue; }
    Serial.rx.push_back(wire[wi].c); rxContract.push_back(wire[wi].contract);
  }
}
// Simulated time passes INSIDE a blocking call (fake/Arduino.h): RX keeps arriving meanwhile.
inline void blockFor(uint64_t d) { us += d; g_ms = (uint32_t)(us / 1000); deliverRx(); }
inline uint64_t nowUs() { return us; }
static const int hooked_ = (g_simBlock = blockFor, g_simNowUs = nowUs, 0);
inline long hpMoves(int hp) { return g_servoPinMoves[HPpins[hp][0]] + g_servoPinMoves[HPpins[hp][1]]; }
inline void i2c(const char* s) {                         // one I2C write to the board, as the TWI interrupt delivers it
  for (const char* p = s; *p; p++) Wire.rx.push_back(*p);
  i2cEvent((int)strlen(s));
}
inline void pass(uint64_t pass_us = PASS) {              // one loop() pass, then serialEvent(), as the Arduino core runs them
  g_ms = (uint32_t)(us / 1000);
  bool stringCompleteBefore = stringComplete;            // a line is handled in this pass
  bool contractLine = stringComplete && lineContract && !lineOther;
  bool stockLine = stringComplete && lineOther;          // a line with bytes the host did not send as '!' traffic
  // A servo move in this pass is the STOCK firmware's own, with or without the line, when a stock
  // twitch was due on that unit (and the contract did not hold it), or a stock servo function
  // (position, RC, wag) was already running on it before the pass began.
  bool stockMove[HPCOUNT], held0[HPCOUNT]; long pm0[HPCOUNT]; byte fn0[HPCOUNT];
  for (int i = 0; i < HPCOUNT; i++) {
    held0[i] = SIM_HELD(i); pm0[i] = hpMoves(i); fn0[i] = HP_command[i].HPFunction;
    stockMove[i] = (millis() > twitchHPTime[i] && enableTwitchHP[i] && fn0[i] > 99 && !held0[i]) || (fn0[i] >= 1 && fn0[i] <= 99);
  }
  long moves0 = g_servoMoves; HPCmd hp0[HPCOUNT]; memcpy(hp0, HP_command, sizeof hp0);
  if (stringComplete) { lineContract = lineOther = false; linesHandled++; }
  loop();
  // THE LED-ONLY INVARIANT, behavioural: the pass that handles a contract-origin line (whole,
  // spliced, beheaded or corrupted) starts no servo move and writes no HP_command.
  bool moved = false; for (int i = 0; i < HPCOUNT; i++) if (hpMoves(i) != pm0[i] && !stockMove[i]) moved = true;
  if (ledOnlyChecked && contractLine && (moved || memcmp(hp0, HP_command, sizeof hp0) != 0)) {
    if (!ledOnlyViolations) printf("  LED-ONLY VIOLATION at t=%.3f s: moveTo calls %+ld, HP_command %s\n", us / 1e6,
                                   g_servoMoves - moves0, memcmp(hp0, HP_command, sizeof hp0) ? "WRITTEN" : "untouched");
    ledOnlyViolations++;
  }
  // THE HOLD INVARIANT: a unit the contract held when the pass began does not move in that pass
  // unless a stock servo command is running on it or a stock line was handled. Letting go of a
  // unit (a look replaced, an overlay ending, a score section, a reset) must never be the moment
  // its long-overdue servo twitch fires.
  for (int i = 0; i < HPCOUNT; i++)
    if (holdChecked && held0[i] && fn0[i] > 99 && !stockLine && hpMoves(i) != pm0[i]) {
      if (!holdViolations) printf("  HOLD VIOLATION at t=%.3f s: HP %d moved in the pass that released it\n", us / 1e6, i);
      holdViolations++;
    }
  for (int i = 0; i < HPCOUNT; i++) {                    // a stock look (1..7) starting from an idle value (>99) is a twitch
    byte f = LED_command[i].LEDFunction;
    if (f >= 1 && f <= 7 && prevFn[i] > 99 && !stringCompleteBefore) ledTwitchStarts[i]++;
    prevFn[i] = f;
  }
  us += pass_us; g_ms = (uint32_t)(us / 1000);
  deliverRx();
  size_t before = Serial.rx.size();
  if (Serial.available()) serialEvent();
  for (size_t n = before - Serial.rx.size(); n; n--) { (rxContract.front() ? lineContract : lineOther) = true; rxContract.pop_front(); }
}
inline void run(double seconds, uint64_t pass_us = PASS) { uint64_t end = us + (uint64_t)(seconds * 1e6); while (us < end) pass(pass_us); }
inline void line(const char* s, double thenRun = 0.2) { say(s); run(thenRun); }      // one line, then let it land
inline bool wireIdle() { return wi >= wire.size() && Serial.rx.empty() && !stringComplete; }
// Loop passes of RANDOM length, lo..hi microseconds: a fixed pass time phase-locks with the wire and
// hides what a real board's varying pass produces (an RX ring that overflows mid-line, for one).
inline uint64_t rnd(uint64_t lo, uint64_t hi) { return hi <= lo ? lo : lo + (uint64_t)rand() % (hi - lo + 1); }
inline void runRand(double seconds, uint64_t lo, uint64_t hi) { uint64_t end = us + (uint64_t)(seconds * 1e6); while (us < end) pass(rnd(lo, hi)); }
inline void drain(uint64_t lo = PASS, uint64_t hi = PASS, double extra = 0.05) { while (!wireIdle()) pass(rnd(lo, hi)); runRand(extra, lo, hi); }
// latched (last show()) and RAM colour of pixel px on jewel hp, as R,G,B
struct RGB3 { int r, g, b; bool operator==(const RGB3& o) const { return r == o.r && g == o.g && b == o.b; } };
inline RGB3 latched(int hp, int px = 1) { uint8_t* p = neoStrips[hp].shown + px * 3; return {p[0], p[1], p[2]}; }
inline bool dark(int hp) { for (int i = 0; i < 21; i++) if (neoStrips[hp].shown[i]) return false; return true; }
inline long shows(int hp) { return neoStrips[hp].showCount; }
inline long twitches() { return ledTwitchStarts[0] + ledTwitchStarts[1] + ledTwitchStarts[2]; }
// Run fn in a forked copy of the world (globals cannot be reset); returns its failure count.
inline int trial(const std::function<void()>& fn) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) { failures = 0; ledOnlyViolations = 0; holdViolations = 0; fn(); fflush(stdout);
                  _exit((failures || (ledOnlyChecked && ledOnlyViolations) || (holdChecked && holdViolations)) ? 1 : 0); }
  int st = 0; waitpid(pid, &st, 0);
  return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : 1;
}
typedef void (*ScenarioFn)();
inline std::map<std::string, ScenarioFn>& registry() { static std::map<std::string, ScenarioFn> r; return r; }
struct Reg { Reg(const char* n, ScenarioFn f) { registry()[n] = f; } };
}  // namespace sim

#define SCENARIO(name) static void scenario_##name(); static sim::Reg reg_##name(#name, scenario_##name); static void scenario_##name()
#define CHECK(cond) do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); sim::failures++; } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); if (_a != _b) { printf("  FAIL %s:%d: %s == %ld, want %ld\n", __FILE__, __LINE__, #a, _a, _b); sim::failures++; } } while (0)
