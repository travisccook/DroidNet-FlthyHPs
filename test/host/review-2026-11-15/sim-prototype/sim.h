// Whole-firmware host simulation: the REAL src/main.cpp (one mechanical substitution, see
// run_sim.sh) on a fake Arduino core, a 9600-baud wire model and a controllable loop-pass time.
#pragma once
#include <Arduino.h>
#include <Wire.h>
uint32_t g_ms = 0; long g_pulseIn = 0; long g_servoMoves = 0;
FakeSerial Serial; FakeWire Wire;
#include "main.cpp"                      // the firmware under test
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
static bool ledOnlyChecked = true;                       // a scenario may switch the invariant off
static long ledTwitchStarts[HPCOUNT] = {0, 0, 0};        // stock LED auto twitches begun, per jewel
static byte prevFn[HPCOUNT] = {255, 255, 255};

// SIM_HELD(i): the firmware's own "the contract holds unit i" test, as the auto-twitch gate
// in main.cpp spells it. Used only to excuse a stock twitch that was due in this pass anyway.
#ifndef SIM_HELD
#define SIM_HELD(i) (gUnit[i].active)
#endif

// Queue text on the wire, back to back at 9600 baud, no earlier than now. A line that starts
// with '!' is tagged contract-origin, and so are its bytes if the firmware beheads it.
inline uint64_t say(const char* s) {
  double t = (double)us;
  if (!wire.empty() && wire.back().at + BYTE_US > t) t = wire.back().at + BYTE_US;
  bool lineStart = true, contract = false;
  for (; *s; s++) {
    if (lineStart) { contract = (*s == '!'); lineStart = false; }
    wire.push_back({(uint64_t)t, *s, contract});
    if (*s == '\r' || *s == '\n') lineStart = true;
    t += BYTE_US;
  }
  return (uint64_t)t;
}
inline void i2c(const char* s) {                         // one I2C write to the board, as the TWI interrupt delivers it
  for (const char* p = s; *p; p++) Wire.rx.push_back(*p);
  i2cEvent((int)strlen(s));
}
inline void pass(uint64_t pass_us = PASS) {              // one loop() pass, then serialEvent(), as the Arduino core runs them
  g_ms = (uint32_t)(us / 1000);
  bool stringCompleteBefore = stringComplete;            // a line is handled in this pass
  bool contractLine = stringComplete && lineContract && !lineOther;
  bool twitchDue = false;                                // a stock servo twitch due now with or without the line
  for (int i = 0; i < HPCOUNT; i++)
    if (millis() > twitchHPTime[i] && enableTwitchHP[i] && HP_command[i].HPFunction > 99 && !SIM_HELD(i)) twitchDue = true;
  long moves0 = g_servoMoves; HPCmd hp0[HPCOUNT]; memcpy(hp0, HP_command, sizeof hp0);
  if (stringComplete) lineContract = lineOther = false;
  loop();
  // THE LED-ONLY INVARIANT, behavioural: the pass that handles a contract-origin line (whole,
  // spliced or beheaded) starts no servo move and writes no HP_command.
  if (ledOnlyChecked && contractLine &&
      ((g_servoMoves != moves0 && !twitchDue) || memcmp(hp0, HP_command, sizeof hp0) != 0)) {
    if (!ledOnlyViolations) printf("  LED-ONLY VIOLATION at t=%.3f s: moveTo calls %+ld, HP_command %s\n", us / 1e6,
                                   g_servoMoves - moves0, memcmp(hp0, HP_command, sizeof hp0) ? "WRITTEN" : "untouched");
    ledOnlyViolations++;
  }
  for (int i = 0; i < HPCOUNT; i++) {                    // a stock look (1..7) starting from an idle value (>99) is a twitch
    byte f = LED_command[i].LEDFunction;
    if (f >= 1 && f <= 7 && prevFn[i] > 99 && !stringCompleteBefore) ledTwitchStarts[i]++;
    prevFn[i] = f;
  }
  us += pass_us; g_ms = (uint32_t)(us / 1000);
  while (wi < wire.size() && wire[wi].at <= us) { Serial.rx.push_back(wire[wi].c); rxContract.push_back(wire[wi].contract); wi++; }
  size_t before = Serial.rx.size();
  if (Serial.available()) serialEvent();
  for (size_t n = before - Serial.rx.size(); n; n--) { (rxContract.front() ? lineContract : lineOther) = true; rxContract.pop_front(); }
}
inline void run(double seconds, uint64_t pass_us = PASS) { uint64_t end = us + (uint64_t)(seconds * 1e6); while (us < end) pass(pass_us); }
inline void line(const char* s, double thenRun = 0.2) { say(s); run(thenRun); }      // one line, then let it land
inline bool wireIdle() { return wi >= wire.size() && Serial.rx.empty() && !stringComplete; }
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
  if (pid == 0) { failures = 0; ledOnlyViolations = 0; fn(); fflush(stdout); _exit((failures || (ledOnlyChecked && ledOnlyViolations)) ? 1 : 0); }
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
