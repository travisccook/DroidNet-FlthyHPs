// Review harness: compiles the REAL src/main.cpp (unmodified) against a fake Arduino core.
#include <Arduino.h>
#include <Wire.h>
uint32_t g_ms = 0; long g_pulseIn = 0; long g_servoMoves = 0;
static uint64_t g_us = 0;
FakeSerial Serial; FakeWire Wire;
#include "main.cpp"
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

struct WireByte { uint64_t at; char c; };
static std::vector<WireByte> g_wire; static size_t g_wi = 0;
static const double BYTE_US = 1e6 * 10.0 / 9600.0;   // 1041.67 us per byte, 8N1
// queue a string back-to-back at 9600 baud starting at t0 (us); returns end time
static uint64_t sendAt(uint64_t t0, const char* s) {
  double t = (double)t0; if (!g_wire.empty() && g_wire.back().at + BYTE_US > t) t = g_wire.back().at + BYTE_US;
  for (; *s; s++) { g_wire.push_back({(uint64_t)t, *s}); t += BYTE_US; }
  return (uint64_t)t;
}
static long g_ledTwitchStarts = 0; static byte g_prevFn[3] = {255,255,255};
static void onePass(uint64_t pass_us) {
  g_ms = (uint32_t)(g_us / 1000);
  loop();
  g_us += pass_us; g_ms = (uint32_t)(g_us / 1000);
  while (g_wi < g_wire.size() && g_wire[g_wi].at <= g_us) Serial.rx.push_back(g_wire[g_wi++].c);
  if (Serial.available()) serialEvent();
  for (int i = 0; i < 3; i++) { byte f = LED_command[i].LEDFunction;
    if (f >= 1 && f <= 7 && (g_prevFn[i] > 99)) g_ledTwitchStarts++;
    g_prevFn[i] = f; }
}
static void runFor(double seconds, uint64_t pass_us) { uint64_t end = g_us + (uint64_t)(seconds * 1e6); while (g_us < end) onePass(pass_us); }
static void say(const char* s) { sendAt(g_us, s); }
static void state(const char* tag) {
  printf("  [%s] t=%.1fs LEDFn={%d,%d,%d} LEDHalt={%d,%d,%d} twLED={%d,%d,%d} twHP={%d,%d,%d} active={%d,%d,%d} interlocked={%d,%d,%d} servoMoves=%ld ledTwitchStarts=%ld\n", tag, g_us/1e6,
    LED_command[0].LEDFunction, LED_command[1].LEDFunction, LED_command[2].LEDFunction,
    LED_command[0].LEDHalt, LED_command[1].LEDHalt, LED_command[2].LEDHalt,
    enableTwitchLED[0], enableTwitchLED[1], enableTwitchLED[2], enableTwitchHP[0], enableTwitchHP[1], enableTwitchHP[2],
    gUnit[0].active, gUnit[1].active, gUnit[2].active, gInterlocked[0], gInterlocked[1], gInterlocked[2], g_servoMoves, g_ledTwitchStarts);
}
static void px(const char* tag, int hp) { uint8_t* p = neoStrips[hp].shown; uint8_t* q = neoStrips[hp].pixels;
  printf("  [%s] hp%d latched px0=(%d,%d,%d) px1=(%d,%d,%d)  ram px1=(%d,%d,%d)  stripBrightness=%d showCount=%ld\n", tag, hp, p[0],p[1],p[2], p[3],p[4],p[5], q[3],q[4],q[5], neoStrips[hp].getBrightness(), neoStrips[hp].showCount); }

int main(int argc, char** argv) {
  std::string sc = argc > 1 ? argv[1] : ""; long arg = argc > 2 ? atol(argv[2]) : 0; long arg2 = argc > 3 ? atol(argv[3]) : 0;
  srand(argc > 4 ? atoi(argv[4]) : 7);
  setup();
  const uint64_t P = 2000;   // default pass time 2 ms
  if (sc == "idle") {          // does M:v=idle hand the jewels back to the native auto-twitch?
    runFor(600, P); state("native baseline, 600 s");
    long s0 = g_servoMoves, l0 = g_ledTwitchStarts;
    say("!**M:v=show\r"); runFor(0.2, P); say("!H*A:i=solid,c=ff0000\r"); runFor(10, P); state("in show");
    if (arg == 0) { say("!**X\r"); runFor(0.2, P); }
    say("!**M:v=idle\r"); runFor(0.5, P); state("just after idle"); px("after idle", 0);
    long s1 = g_servoMoves, l1 = g_ledTwitchStarts;
    runFor(1800, P); state("30 min after idle"); px("30 min after idle", 0);
    printf("  RESULT in-show: servo moves %ld, LED twitches %ld | 30 min after idle: servo moves %ld, LED twitch starts %ld (baseline 10 min: %ld LED twitch starts)\n", s1 - s0, l1 - l0, g_servoMoves - s1, g_ledTwitchStarts - l1, l0);
  } else if (sc == "stalehalt") { // a native twitch in flight when the show starts
    while (!(LED_command[0].LEDFunction >= 1 && LED_command[0].LEDFunction <= 7 && LED_command[0].LEDHalt > 6)) onePass(P);
    runFor(1.0, P); state("native twitch in flight on front");
    say("!**M:v=show\r"); runFor(0.2, P); say("!HFA:i=solid,c=ff0000\r"); runFor(0.2, P); state("show started"); px("show", 0);
    uint64_t t0 = g_us; bool reported = false;
    while (g_us < t0 + 40000000ULL) { onePass(P);
      if (!reported && gUnit[0].active && LED_command[0].LEDFunction != 102) { printf("  RESULT %.1f s into the show the front look was flushed: LEDFunction=%d, gUnit.active=%d\n", (g_us - t0)/1e6, LED_command[0].LEDFunction, gUnit[0].active); px("flushed", 0); reported = true; } }
    state("40 s later"); px("40 s later", 0);
    if (!reported) printf("  RESULT look survived\n");
  } else if (sc == "concat") {   // back-to-back lines (WCB '^' batch), pass time = arg us
    uint64_t pass = (uint64_t)arg; runFor(1.0, pass);
    g_us += (uint64_t)arg2;       // phase
    say("!**M:v=show\r!HFA:i=solid,c=ff0000\r!HRA:i=comet,c=00ff00\r!HTA:i=twinkle,c=0000ff\r"); runFor(1.0, pass);
    int ok = (gUnit[0].active && gUnit[0].effect == CE_SOLID) + (gUnit[1].active && gUnit[1].effect == CE_COMET) + (gUnit[2].active && gUnit[2].effect == CE_TWINKLE);
    int show = gInterlocked[0] && gInterlocked[1] && gInterlocked[2];
    say("!**X\r!**M:v=idle\r"); runFor(1.0, pass);
    int idleOk = !gInterlocked[0] && !gInterlocked[1] && !gInterlocked[2];
    return (ok) | (show << 2) | (idleOk << 3);
  } else if (sc == "replay") {   // stale typeState replay through a beheaded '!**X'
    uint64_t pass = (uint64_t)arg;
    say("A105\r"); runFor(3.0, pass); say("A198\r"); runFor(1.0, pass);    // operator wagged the HPs, then disabled servo twitch + cleared
    say("A199\r"); runFor(0.5,pass); say("A105\r"); runFor(2.0, pass); say("S7\r"); runFor(8.0, pass);  // ... wag again, then S7 = clear all, servos off
    long s0 = g_servoMoves; g_us += (uint64_t)arg2;
    say("!**C:bpm=120,ph=0,bpb=4,beat=64\r!**X\r!**M:v=idle\r"); runFor(5.0, pass);
    return (g_servoMoves - s0) > 0 ? 1 : 0;
  } else if (sc == "bright") {
    px("boot (BRIGHT 100)", 0);
    say("!**M:v=show\r"); runFor(0.2, P); say("!HFA:i=solid,c=ffffff,b=255\r"); runFor(0.5, P); px("solid ffffff b=255", 0);
    say("!HFB:b=40\r"); runFor(0.5, P); px("after !HFB:b=40", 0);
    say("!HFB:b=1\r"); runFor(0.5, P); say("!HFB:b=200\r"); runFor(0.5, P); px("after B 1 then B 200", 0);
    say("!HFP:c=00ff00,d=200\r"); runFor(1.0, P); px("after an accent (repaint)", 0);
    say("!HFB:b=20\r"); runFor(0.2, P); say("!**X\r"); runFor(0.2, P); say("!**M:v=idle\r"); runFor(0.5, P);
    say("F0055\r"); runFor(0.5, P); px("native F0055 (solid blue) after the show", 0);
  } else if (sc == "native") {
    say("!**M:v=show\r"); runFor(0.2, P);
    say(arg == 3 ? "!HFA:i=native:3,c=0000ff\r" : arg == 2 ? "!HFA:i=native:2,c=0000ff\r" : "!HFA:i=native:5,c=0000ff\r"); runFor(0.05, P);
    printf("  native:%ld -> LEDFunction=%d LEDOption1=%d LEDOption2=%d ; dimPulse inter would be map(%d,0,9,75,5)=%ld\n", arg, LED_command[0].LEDFunction, LED_command[0].LEDOption1, LED_command[0].LEDOption2, LED_command[0].LEDOption2, map(LED_command[0].LEDOption2, 0, 9, 75, 5));
    fflush(stdout); runFor(3.0, P); px("3 s of native look", 0);
  } else if (sc == "nativeaccent") {
    say("!**M:v=show\r"); runFor(0.2, P); say("!HFA:i=comet,c=00ff00\r"); runFor(1, P); say("!HFA:i=native:1,c=0000ff\r"); runFor(1, P); state("Leia (native:1) section"); 
    say("!HFP:c=ffffff,d=180\r"); runFor(1.0, P); state("1 s after one accent");
    printf("  RESULT LEDFunction=%d (1 = Leia restored; else the native look is gone), gUnit.effect=%d active=%d\n", LED_command[0].LEDFunction, (int)gUnit[0].effect, gUnit[0].active);
  } else if (sc == "sc7") {
    say("!**M:v=show\r"); runFor(0.2, P); say("!HFA:i=native:7\r"); runFor(20, P); long c0 = neoStrips[0].showCount; printf("  first native:7: SCloop=%d\n", SCloop[0]);
    say("!HFA:i=solid,c=ff0000\r"); runFor(1, P); say("!HFA:i=native:7\r"); long c1 = neoStrips[0].showCount; runFor(20, P);
    printf("  RESULT second native:7: SCloop=%d, show() calls during it=%ld (first run made %ld)\n", SCloop[0], neoStrips[0].showCount - c1, c0);
  } else if (sc == "dur") {
    say("!HFA:i=solid,c=ff0000,d=500\r"); runFor(1.5, P); state("after d=500 expired");
    say("!HFA:i=solid,c=00ff00\r"); runFor(0.2, P); px("new look, no d=", 0); runFor(1.0, P); px("1.2 s later", 0); state("1.2 s later");
    printf("  RESULT durMs carried into the d-less look = %lu, active=%d\n", (unsigned long)gUnit[0].durMs, gUnit[0].active);
  } else if (sc == "showrate") {
    const char* fx[] = {"solid","flash","pulse","rainbow","scan","sparkle","comet","chase","wipe","gradient","colorcycle","twinkle"};
    say("!**M:v=show\r"); runFor(0.2, P);
    for (auto f : fx) { char b[80]; snprintf(b, sizeof b, "!H*A:i=%s,c=ff0000\r", f); say(b); runFor(0.5, P);
      long c = neoStrips[0].showCount; runFor(2.0, P); printf("  %-10s show() per pass (front jewel): %.2f\n", f, (neoStrips[0].showCount - c) / 1000.0); }
    say("!**C:bpm=120,ph=0,bpb=4\r"); runFor(0.1,P); say("!H*A:i=solid,c=ff0000,m=200,am=2\r"); runFor(0.5, P); long c = neoStrips[0].showCount; runFor(2.0, P);
    printf("  solid+pump show() per pass: %.2f\n", (neoStrips[0].showCount - c) / 1000.0);
    say("!**X\r"); runFor(0.5,P); say("!**M:v=idle\r"); runFor(0.5,P); say("A099\r"); runFor(400, P); // back to native; wait for off-colour hold
    for (int i = 0; i < 3; i++) if (LED_command[i].LEDFunction == 100) { long c2 = neoStrips[i].showCount; runFor(2.0, P); printf("  native off-colour hold (LEDFunction 100) hp%d show() per pass: %.2f\n", i, (neoStrips[i].showCount - c2) / 1000.0); break; }
  } else if (sc == "pulsehz") {
    say("!**M:v=show\r"); runFor(0.2, 1000); char b[64]; snprintf(b, sizeof b, "!HFA:i=pulse,c=ffffff,s=%ld\r", arg); say(b); runFor(0.5, 1000);
    int peaks = 0, lo = 255, hi = 0, prev = -1, dir = 0; uint64_t t0 = g_us;
    while (g_us < t0 + 3000000ULL) { onePass(1000); int v = neoStrips[0].shown[0]; if (v < lo) lo = v; if (v > hi) hi = v;
      if (prev >= 0) { int d = v > prev ? 1 : v < prev ? -1 : dir; if (dir == 1 && d == -1) peaks++; dir = d; } prev = v; }
    printf("  pulse s=%ld: %.2f full dark->bright->dark cycles per second, level %d..%d\n", arg, peaks / 3.0, lo, hi);
  } else if (sc == "opoff") {
    say("!**M:v=show\r"); runFor(1, P); say("A198\r"); runFor(1, P); state("operator sent A198 (servo twitch OFF) during the show");
    say("!**M:v=idle\r"); runFor(1, P); state("after idle");
    printf("  RESULT enableTwitchHP after idle = {%d,%d,%d}\n", enableTwitchHP[0], enableTwitchHP[1], enableTwitchHP[2]);
  } else if (sc == "hostdead") {
    say("!**M:v=show\r"); runFor(0.3, P); say("!H*A:i=comet,c=ff0000,at=0,am=2,m=200,ae=solid\r"); runFor(0.3, P); say("!**C:bpm=120,ph=0,bpb=4,beat=0\r"); runFor(10, P); state("show running, host goes silent now");
    runFor(3 * 3600.0, 5000); long s0 = g_servoMoves, l0 = g_ledTwitchStarts; uint32_t a0 = gUnit[0].pulseStartMs; runFor(60, 5000);
    state("3 h after the last byte from the host");
    printf("  RESULT still accenting=%d, clock running=%d, servo moves in the 3 h = %ld, native LED twitches = %ld\n", gUnit[0].pulseStartMs != a0, gBeat.running, g_servoMoves, g_ledTwitchStarts);
  } else if (sc == "xnoshow") {
    runFor(5, P); say("!HFA:i=solid,c=ff0000\r"); runFor(5, P); say("!HFX\r"); runFor(1, P); long l0 = g_ledTwitchStarts; g_prevFn[1] = g_prevFn[2] = 0; 
    long before = 0; (void)before; uint64_t t0 = g_us; long frontTw = 0; byte pf = LED_command[0].LEDFunction;
    while (g_us < t0 + 1800000000ULL) { onePass(P); byte f = LED_command[0].LEDFunction; if (f >= 1 && f <= 7 && pf > 99) frontTw++; pf = f; }
    state("30 min after !HFX (no show mode)"); printf("  RESULT front LED twitch starts in 30 min after X: %ld (all HPs: %ld)\n", frontTw, g_ledTwitchStarts - l0);
  }
#include "extra.inc"
  return 0;
}
