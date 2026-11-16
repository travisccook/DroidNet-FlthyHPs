// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// The findings of the independent review of release 1 (2026-11-16), each as a scenario, and the
// property test contract v1.3 S9 asks for: hostile line streams in, latched frames measured.
#include <algorithm>
#include <set>

// ---- servo tripwire that does not lean on the pass invariants -----------------------------------
struct ServoSnap {
  long moves; HPCmd hp[HPCOUNT]; bool tw[HPCOUNT];
  void take() { moves = g_servoMoves; memcpy(hp, HP_command, sizeof hp); for (int i = 0; i < HPCOUNT; i++) tw[i] = enableTwitchHP[i]; }
  bool touched() const {
    if (g_servoMoves != moves || memcmp(hp, HP_command, sizeof hp)) return true;
    for (int i = 0; i < HPCOUNT; i++) if (tw[i] != enableTwitchHP[i]) return true;
    return false;
  }
};
static void noStockTwitchSoon() { for (int i = 0; i < HPCOUNT; i++) twitchHPTime[i] = millis() + 100000000UL; }
static std::string vis(const std::string& s) {
  std::string o; char b[8];
  for (unsigned char c : s) { if (c == '\r') o += "<CR>"; else if (c == '\n') o += "<LF>"; else if (c < 32 || c > 126) { snprintf(b, sizeof b, "<%02X>", c); o += b; } else o += (char)c; }
  return o;
}

// ---- Finding 1: a bare 'X' or 'A' line must not replay the last stock servo command -------------
// 'X' and 'A' are contract verbs AND stock designators. One bit error ('*' 0x2A -> LF 0x0A) leaves
// such a letter alone on a line, and the stock parser used the previous command's type and function.
SCENARIO(r1_one_bit_error_cannot_replay_a_servo_command) {
  struct { const char* prime; const char* bad; } C[] = {
    {"F1013\r", "!*\nX\r"}, {"A105|2\r", "!H\nX\r"}, {"A105|2\r", "!*\nA\r"}, {"A199\r", "!*\nX\r"}, {"T1042\r", "!**\rX\r"}};
  for (auto& c : C) {
    int bad = trial([&] {
      run(8); line(c.prime, 8.0);                    // the operator's servo command, long finished
      for (int i = 0; i < HPCOUNT; i++) enableTwitchHP[i] = false;   // so any move below is not a stock twitch
      ServoSnap s; s.take(); sayAs(c.bad, true); drain(); run(3);
      if (s.touched()) { printf("  after %s the corrupted line %s: moveTo %+ld, HP functions %d %d %d, twitch %d %d %d\n", vis(c.prime).c_str(),
                                vis(c.bad).c_str(), g_servoMoves - s.moves, HP_command[0].HPFunction, HP_command[1].HPFunction,
                                HP_command[2].HPFunction, enableTwitchHP[0], enableTwitchHP[1], enableTwitchHP[2]); failures++; }
    });
    CHECK_EQ(bad, 0);
  }
}
// Every single fault in every realistic contract line, after each kind of stock command: one bit
// flipped, one byte lost, one byte replaced by NUL / 0xFF / CR / LF, a CR / LF / NUL inserted.
static const char* R1_CORPUS[] = {"!**X", "!H*X", "!HFX", "!HRX", "!HTX", "!**M:v=show,wd=15", "!**M:v=idle", "!H*M:v=show", "!HFM:v=idle",
  "!H*A:i=solid,c=ff0000", "!HFA:i=comet,c=00ff00,s=200,m=200,am=2", "!HRA:i=native:1", "!HTA:i=scan,c=0000ff",
  "!H*A:i=twinkle,c=3b82f6,at=8,am=2,m=200,ae=solid,ac=ffffff,ad=250", "!HFA:i=flash,c=ffffff,b=128,at=16,an=2,ap=1",
  "!H*P:c=ffffff,d=200", "!HFP:i=flash,c=ff0000,b=255", "!**C:bpm=120,ph=0,bpb=4,beat=0", "!**C:ph=120,beat=9", "!**C:bpm=0",
  "!H*B:b=90", "!HTB:b=255", "!H*L:v=200", "!HRQ:r=0", "!HFQ:r=12", "!**Q", "!HFV:n=12,k=a1f3", "!HTV:n=1,k=0001", "!**A", "!*FX", "!*TA:i=off"};
SCENARIO(r1_single_fault_sweep_touches_no_servo_state) {
  ledOnlyChecked = holdChecked = false;              // this scenario counts for itself, per mutation
  const char* PRIMES[] = {"", "F1013\r", "A105|2\r", "A199\r", "T102\r", "F0055\r"};
  long tested = 0, bad = 0; std::set<std::string> offenders;
  run(6);
  for (const char* prime : PRIMES) {
    byte pt = typeState, pf = functionState; bool primed = false;
    auto ensurePrimed = [&] {
      if (primed && typeState == pt && functionState == pf) return;
      if (*prime) { say(prime); drain(); run(4); }
      for (int i = 0; i < HPCOUNT; i++) HP_command[i].HPFunction = 255;    // the servo side idle; the parser's leftovers stay
      pt = typeState; pf = functionState; primed = true;
    };
    auto fault = [&](const std::string& m, const char* kind) {
      ensurePrimed(); noStockTwitchSoon(); tested++;
      ServoSnap s; s.take(); sayAs(m + "\r", true); drain(3000, 3000, 0.25);
      if (s.touched()) { bad++; if (offenders.size() < 12) offenders.insert(std::string(kind) + " " + vis(m) + " after " + (*prime ? vis(prime) : "(no stock command)"));
                         for (int i = 0; i < HPCOUNT; i++) { HP_command[i].HPFunction = 255; enableTwitchHP[i] = true; } primed = false; }
    };
    for (const char* line0 : R1_CORPUS) {
      std::string l = line0;
      fault(l, "clean");
      for (size_t i = 0; i < l.size(); i++) {
        for (int b = 0; b < 8; b++) { std::string m = l; m[i] ^= (char)(1 << b); fault(m, "bit flipped:"); }
        { std::string m = l; m.erase(i, 1); fault(m, "byte lost:"); }
        for (char x : {'\0', '\xFF', '\r', '\n'}) { std::string m = l; m[i] = x; fault(m, "byte replaced:"); }
        for (char x : {'\r', '\n', '\0'}) { std::string m = l; m.insert(i, 1, x); fault(m, "byte inserted:"); }
      }
    }
  }
  printf("  %ld single-fault mutations, %ld touched servo state\n", tested, bad);
  for (auto& o : offenders) printf("    %s\n", o.c_str());
  CHECK(tested > 40000);  CHECK_EQ(bad, 0);
}
// The RX ring overflowing mid-line splices lines: the ring ends on a terminator and the next byte it
// accepts is the verb. Fixed pass times phase-lock and never produce it; random ones do.
SCENARIO(r1_random_pass_splices_touch_no_servo_state) {
  int bad = 0, spliced = 0; const int RUNS = 300;
  for (int k = 0; k < RUNS; k++) {
    fflush(stdout); pid_t pid = fork();
    if (pid == 0) {
      failures = 0; ledOnlyViolations = 0; holdViolations = 0;
      srand(100 + k); run(8); line(k % 2 ? "A105|2\r" : "F1013\r", 8.0);
      for (int i = 0; i < HPCOUNT; i++) enableTwitchHP[i] = false;
      ServoSnap s; s.take(); std::string b;
      const char* L[] = {"!**X\r", "!H*X\r", "!**A:i=solid,c=ff0000\r", "!**M:v=show\r", "!HFX\r", "!**C:bpm=120\r"};
      for (int i = 0; i < 120; i++) b += (k % 3 == 2) ? L[rand() % 6] : L[k % 3];
      say(b.c_str()); drain(6000, 30000, 1.0);
      _exit(((s.touched() || ledOnlyViolations) ? 1 : 0) | (rxOverruns ? 2 : 0));
    }
    int st = 0; waitpid(pid, &st, 0); int code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    if (code & 1) bad++;  if (code & 2) spliced++;
  }
  printf("  %d runs of 120 gapless lines at 6-30 ms passes: %d lost bytes (so spliced lines), %d touched servo state\n", RUNS, spliced, bad);
  CHECK(spliced >= RUNS / 2);                        // the test must keep producing the splice it is about
  CHECK_EQ(bad, 0);
}

// ---- Finding 2: letting go of a unit is never the moment its overdue servo twitch fires ---------
// With no show armed a contract look still holds the servo twitch. Three ways out of that hold
// did not re-arm the timer, so all three holoprojectors moved at once.
SCENARIO(r2_live_look_replaced_by_a_native_look_moves_no_servo) {
  run(10); line("!H*A:i=solid,c=ff0000\r", 240.0);  // a live look, 4 minutes: every twitch timer is overdue
  long m = g_servoMoves; line("!H*A:i=native:6\r", 8.0);
  CHECK_EQ(g_servoMoves - m, 0);
  m = g_servoMoves; run(600); CHECK(g_servoMoves - m >= 3);   // and the stock twitch does come back, by its own timer
}
SCENARIO(r2_score_with_no_show_reaching_a_native_section_moves_no_servo) {
  run(10); say("!H*A:i=solid,c=ff0000,at=0\r!H*A:i=native:6,at=480\r!**C:bpm=120,beat=0\r"); run(238.0);
  CHECK(lookUp(0)); long m = g_servoMoves; run(10.0);          // beat 480 is 240 s after the seed
  CHECK(!lookUp(0)); CHECK_EQ(LED_command[0].LEDFunction, 6);  CHECK_EQ(g_servoMoves - m, 0);
}
SCENARIO(r2_the_last_overlay_ending_moves_no_servo) {
  run(10); for (int i = 0; i < 480; i++) { say("!H*P:c=ffffff,d=2550\r"); run(0.5); }   // 4 minutes of pulses, no show
  long m = g_servoMoves; run(8.0); CHECK_EQ(g_servoMoves - m, 0);
  m = g_servoMoves; long t = twitches(); run(600); CHECK(g_servoMoves - m >= 3);  CHECK(twitches() - t >= 3);
}

// ---- Finding 4: a unit that left a watched show is still watched --------------------------------
static bool stockAgain(int hp) {
  return !armed(hp) && !lookUp(hp) && scoreLen(hp) == 0 && !SIM_HELD(hp) && !simHasLeds(hp);
}
SCENARIO(r4_a_unit_taken_out_of_a_watched_show_is_still_watched) {
  run(10); line("!**M:v=show,wd=15\r"); line("!**C:bpm=120\r"); line("!H*A:i=solid,c=ff0000\r", 2.0);
  line("F0075|5\r", 1.0);                            // the operator's button takes the front HP out of the show (L6)
  line("!H*A:i=solid,c=00ff00\r", 1.0);              // the host's next live look reaches it again, outside the show
  CHECK(!armed(0));  CHECK(lookUp(0));
  run(20.0);                                         // the host dies: silent for more than wd
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK(stockAgain(hp));
  long t = ledTwitchStarts[0], m = hpMoves(0); run(1800);
  CHECK(ledTwitchStarts[0] - t >= 3);  CHECK(hpMoves(0) - m >= 3);
}
SCENARIO(r4_a_bad_load_drops_late_scored_lines) {    // spec section 7: until the next M:v=show
  run(10); line("!**M:v=show,wd=15\r");
  line("!HFA:i=solid,c=ff0000,at=0\r"); line("!HFA:i=solid,c=00ff00,at=8\r");
  line("!HFV:n=3,k=0000\r");                         // one line never arrived: the front HP idles, ok=2
  CHECK(!armed(0));  CHECK_EQ(scoreLen(0), 0);
  line("!HFA:i=solid,c=0000ff,at=16\r");             // the late line (a mesh retry)
  CHECK_EQ(scoreLen(0), 0);
  line("!**C:bpm=120,beat=16\r", 2.0); CHECK(!lookUp(0));  CHECK(stockAgain(0));
  line("!HFM:v=show\r"); line("!HFA:i=solid,c=0000ff,at=16\r"); CHECK_EQ(scoreLen(0), 1);   // a new show loads again
}

// ---- Finding 6: the Q reply never stalls loop(), and is never mixed with a WDP announce ---------
static int qReplies() { int n = 0; size_t p = 0; while ((p = Serial.tx.find("q:ver=", p)) != std::string::npos) { n++; p++; } return n; }
static bool txLinesWhole() {                         // every reply and every announce on TX is one unbroken line
  const std::string& t = Serial.tx;
  for (size_t p = 0; (p = t.find("@WDP1", p)) != std::string::npos; p++) {
    size_t e = t.find('\n', p); if (e == std::string::npos) e = t.size();
    if (t.substr(p, e - p).find(";S0!") != std::string::npos) return false;
  }
  for (size_t p = 0; (p = t.find(";S0!", p)) != std::string::npos; p++) {
    size_t e = t.find('\r', p); if (e == std::string::npos) return false;
    std::string r = t.substr(p, e - p);
    if (r.find("@WDP1") != std::string::npos || r.find("q:ver=1.3,rel=1,cap=32,n=") == std::string::npos || r.find(",bpm=") == std::string::npos) return false;
  }
  return true;
}
SCENARIO(r6_q_reply_never_stalls_the_loop) {
  run(8); drain(); Serial.tx.clear(); Serial.maxStallUs = 0; long lost = rxOverruns, n0 = linesHandled;
  std::string b; for (int i = 0; i < 40; i++) b += "!HFQ:r=0\r";
  say(b.c_str()); drain(2000, 2000, 3.0);
  printf("  40 gapless Q: %ld lines handled, %d replies, %ld RX bytes lost, longest stall of loop() %.1f ms\n",
         linesHandled - n0, qReplies(), rxOverruns - lost, Serial.maxStallUs / 1000.0);
  CHECK_EQ(linesHandled - n0, 40);  CHECK_EQ(rxOverruns - lost, 0);  CHECK_EQ(Serial.maxStallUs, 0);
  CHECK(qReplies() >= 1);  CHECK(txLinesWhole());
  // a Q that lands while a WDP announce is leaving: the reply follows the announce, whole
  while (gWdp.len == 0) pass(500);                   // an announce has just started
  Serial.tx.clear(); Serial.maxStallUs = 0; say("!HRQ:r=3\r"); drain(2000, 2000, 1.0);
  CHECK_EQ(Serial.maxStallUs, 0);  CHECK_EQ(qReplies(), 1);  CHECK(txLinesWhole());
  CHECK(Serial.tx.find(";W3,;S0!HRq:ver=1.3,rel=1,cap=32,n=0,ok=0,bpm=0\r") != std::string::npos);
}

// ---- Finding 7: "enable servo twitch" works during a show ---------------------------------------
SCENARIO(r7_a199_in_a_show_brings_the_servo_twitch_back) {
  run(10); line("!**M:v=show\r"); line("!H*A:i=solid,c=ff0000\r");   // no wd=, and then the host dies
  long m = g_servoMoves; run(900); CHECK_EQ(g_servoMoves - m, 0);     // the show holds every servo, by spec
  line("A199\r"); m = g_servoMoves; run(900);
  CHECK(hpMoves(0) > 0 && hpMoves(1) > 0 && hpMoves(2) > 0);  CHECK(g_servoMoves - m >= 6);   // the button works
  CHECK(armed(0) && lookUp(0));                                       // and the lights are still the show's
  line("A198\r"); run(30); m = g_servoMoves; run(900); CHECK_EQ(g_servoMoves - m, 0);         // and so does "disable"
  line("A199\r"); line("!**M:v=show\r"); m = g_servoMoves; run(900); CHECK_EQ(g_servoMoves - m, 0);   // a new show holds again
}

// ---- Stock parser reaching a contract render slot: "F0:1" is LED function 101 --------------------
SCENARIO(stock_line_naming_a_contract_slot_only_shows_the_off_colour) {
  run(8); long m = g_servoMoves; for (int i = 0; i < HPCOUNT; i++) enableTwitchHP[i] = false;
  line("R0:1\r", 1.0);                               // (':' - '0') * 10 + 1 = 101, the first contract slot
  CHECK_EQ(LED_command[1].LEDFunction, 100);         // nothing of the contract's to show: the off colour
  CHECK(stockAgain(1));  CHECK_EQ(g_servoMoves - m, 0);
  line("F0;5\r", 1.0); CHECK(stockAgain(0));         // 115, the last one
}

// ---- RC mode (stock F102 / F103): pulseIn() blocks the loop -------------------------------------
// Stock behaviour, stated so nobody reads the burst results below as covering it: with an RC signal
// each pass waits up to a 20 ms frame per HP in RC mode; with none it waits pulseIn()'s full second.
SCENARIO(rc_mode_blocks_the_loop_and_contract_lines_stay_led_only) {
  run(8); g_pulseIn = 1500; line("F102\r", 1.0);
  uint64_t t0 = us; long p0 = linesHandled; for (int i = 0; i < 20; i++) pass(2000);
  double ms = (us - t0) / 20000.0; printf("  one HP in RC mode with a signal: %.1f ms a pass\n", ms);
  CHECK(ms >= 20.0 && ms <= 26.0);
  long lost = rxOverruns; std::string b; for (int i = 0; i < 80; i++) b += "!HRX\r";
  say(b.c_str()); drain(2000, 2000, 1.0);
  printf("  80 five-byte lines sent gapless meanwhile: %ld handled, %ld bytes lost\n", linesHandled - p0, rxOverruns - lost);
  line("F1013\r", 3.0);                              // leave RC mode
}

// ---- Finding 5: sentences the spec gained after release 1, which live in the shared core ---------
SCENARIO(r5_a_malformed_bpm_cannot_hold_or_seed_the_clock) {     // S1
  run(6); line("!**M:v=show\r"); line("!HFA:i=solid,c=ff0000,m=200,am=2\r"); line("!**C:bpm=120,ph=0,bpb=4\r");
  for (const char* bad : {"!**C:bpm=\r", "!**C:bpm=-0\r", "!**C:bpm=+120\r", "!**C:bpm=12x\r", "!**C:bpm=0x\r", "!**C:bpm=\x01\r"}) {
    line(bad); CHECK(clockRunning());  CHECK_EQ(clockBpm(), 120);
  }
  line("!**C:bpm=0\r"); CHECK(!clockRunning());       // the real hold still works
}
SCENARIO(r5_a_scored_line_with_no_effect_is_dropped) {            // section 7
  run(6); line("!**M:v=show\r"); line("!HFA:c=ff0000,at=0\r"); CHECK_EQ(scoreLen(0), 0);
  line("!HFA:i=solid,c=ff0000,at=0\r"); CHECK_EQ(scoreLen(0), 1);
}
SCENARIO(r5_a_repeated_scored_line_does_not_restart_the_look) {   // section 7: a mesh retry changes nothing
  run(6); line("!**M:v=show\r"); line("!HFA:i=flash,c=ffffff,s=0,at=0\r"); line("!**C:bpm=120,beat=0\r", 0.5);
  CHECK(lookUp(0)); int on = 0;
  for (int k = 0; k < 20; k++) { say("!HFA:i=flash,c=ffffff,s=0,at=0\r"); run(0.3); }   // 6 s of retries, every 300 ms
  for (int i = 0; i < 850; i++) { pass(1000); on += latched(0).r != 0; }                 // one whole 850 ms flash cycle
  CHECK(on > 300 && on < 550);                       // still half on, half off: the flash was never restarted
  uint32_t t = gUnit[0].s.startMs; line("!HFA:i=flash,c=ffffff,s=0,at=0\r"); CHECK_EQ(gUnit[0].s.startMs, t);
}
SCENARIO(r5_a_q_with_an_invalid_route_is_not_answered) {          // section 10
  run(8); drain(); Serial.tx.clear();
  for (const char* q : {"!HFQ:r=100\r", "!HFQ:r=-1\r", "!HFQ:r=\r", "!HFQ:r=1x\r", "!HFQ:r=999\r"}) { Serial.tx.clear(); line(q, 0.4); CHECK_EQ(qReplies(), 0); }
  Serial.tx.clear(); line("!HFQ:r=99\r", 0.4); CHECK_EQ(qReplies(), 1);
}

// ---- C6, as this board does it: a held clock shows a pumped look at its b= level -----------------
SCENARIO(c6_hold_shows_a_pumped_look_at_its_own_level) {
  run(6); line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r", 0.5); const int full = latched(0).r;
  line("!HFA:i=solid,c=ffffff,m=200,am=2\r"); line("!**C:bpm=60,ph=0,bpb=4\r", 0.75);   // between beats: dipped
  CHECK(latched(0).r < full * 3 / 4);
  line("!**C:bpm=0\r", 1.0);                         // Pause
  int lo = 255, hi = 0; for (int i = 0; i < 3000; i++) { pass(1000); lo = std::min(lo, latched(0).r); hi = std::max(hi, latched(0).r); }
  CHECK(lo == hi);                                   // still
  CHECK(abs(hi - full) <= 2);                        // and at the look's own level: on screen, not frozen in a dip
}

// ===== S9: one limit for every path, tested as a property on the latched frames ==================
// What an eye at the dome sees of one jewel: the mean, over its seven pixels, of each pixel's
// brightest channel, as a fraction of the owner's ceiling (the strip's level; BRIGHT 100 at boot).
// DARK is a mean of 5 % or less, LIT is 25 % or more; a dark-to-lit step is an ONSET. S9: at most
// three onsets in any second, whatever lines arrive, and never brighter than the ceiling.
// Only the contract's onsets count: one that happens while the layer has the jewel's LEDs, or within
// 300 ms of it letting go (so a reset that hands back to a lit off colour counts). The stock
// firmware's own looks may strobe faster, as they always did (stock function 2, Short Circuit).
static int fullScale(int level) { return level >= 255 ? 255 : (255 * (level + 1)) >> 8; }
struct Photo {
  int hp, ceil; bool isDark = true, started = false; std::vector<uint64_t> onsets; uint64_t lastHad = 0; bool everHad = false;
  int peak = 0, overCeiling = 0;
  explicit Photo(int h, int c = 100) : hp(h), ceil(c) {}
  void sample() {
    const uint8_t* p = neoStrips[hp].shown; int S = 0, FS = 7 * fullScale(ceil);
    for (int i = 0; i < 7; i++) { int m = std::max(p[i * 3], std::max(p[i * 3 + 1], p[i * 3 + 2])); S += m; peak = std::max(peak, m); if (m > fullScale(ceil)) overCeiling++; }
    if (neoStrips[hp].getBrightness() > ceil) overCeiling++;
    if (simHasLeds(hp)) { lastHad = us; everHad = true; }
    const bool lit = S * 4 >= FS, dark = S * 20 <= FS;
    if (!started) { started = true; isDark = !lit; return; }
    if (isDark && lit) { isDark = false; if (everHad && us - lastHad <= 300000ULL) onsets.push_back(us); }
    else if (!isDark && dark) isDark = true;
  }
  int maxPerSecond() const {
    int best = 0;
    for (size_t i = 0; i < onsets.size(); i++) { int n = 0; for (size_t j = i; j < onsets.size() && onsets[j] < onsets[i] + 1000000ULL; j++) n++; best = std::max(best, n); }
    return best;
  }
};
// lines on a timetable (tick = ms), sampled every 1 ms pass
static int onsetsPerSecond(int hp, double secs, const std::function<void(int)>& everyMs) {
  Photo p(hp); uint64_t end = us + (uint64_t)(secs * 1e6); int tick = 0;
  while (us < end) { everyMs(tick++); pass(1000); p.sample(); }
  return p.maxPerSecond();
}
#define S9_CASE(name, hp, secs, setup, body) do { int r_ = 0; int bad_ = trial([&] { run(6); setup; int n = onsetsPerSecond(hp, secs, [](int t) { (void)t; body; }); \
    printf("  %-74s %2d onsets/s%s\n", name, n, n > 3 ? "  <-- S9" : ""); if (n > 3) failures++; }); (void)r_; worst += bad_; } while (0)
static void sayScore32(const char* fmt, const char* odd, const char* even) {
  std::string b; for (int i = 0; i < 32; i++) { char l[96]; snprintf(l, sizeof l, fmt, (i % 2) ? odd : even, i); b += l; } say(b.c_str()); run(3);
}
SCENARIO(s9_no_path_relights_a_jewel_more_than_three_times_a_second) {
  int worst = 0;
  // the paths S2, S4 and S8 name (these held before the gate existed)
  S9_CASE("live A white / A off every 60 ms", 0, 8, line("!**M:v=show\r"), if (t % 60 == 0) say((t / 60) % 2 ? "!HFA:i=off\r" : "!HFA:i=solid,c=ffffff\r"));
  S9_CASE("P white d=1 every 50 ms over a dark baseline", 0, 8, { line("!**M:v=show\r"); line("!**C:bpm=120\r"); }, if (t % 50 == 0) say("!HFP:c=ffffff,d=1\r"));
  S9_CASE("i=flash s=255", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=flash,c=ffffff,s=255\r"); }, );
  S9_CASE("scored solid am=2 m=255 at 300 BPM", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff,at=0,am=2,m=255\r"); line("!**C:bpm=300,beat=0\r"); }, );
  // the paths they do not name
  S9_CASE("X between live A's: A white at 0, X at 85, every 170 ms", 0, 8, line("!**M:v=show\r"), { if (t % 170 == 0) say("!HFA:i=solid,c=ffffff\r"); if (t % 170 == 85) say("!HFX\r"); });
  S9_CASE("the same with no show armed", 0, 8, , { if (t % 170 == 0) say("!HFA:i=solid,c=ffffff\r"); if (t % 170 == 85) say("!HFX\r"); });
  S9_CASE("B: solid white, B:b=0 / B:b=255 every 25 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r"); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HFB:b=0\r" : "!HFB:b=255\r"));
  S9_CASE("B: the same lines gapless at 9600 baud (a WCB macro)", 0, 6, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r");
          std::string b; for (int i = 0; i < 480; i++) b += (i % 2) ? "!HFB:b=0\r" : "!HFB:b=255\r"; say(b.c_str()); }, );
  S9_CASE("L: i=meter, L:v=0 / L:v=255 every 25 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=meter,c=ffffff\r"); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HFL:v=0\r" : "!HFL:v=255\r"));
  S9_CASE("L: the same at a 10 Hz level feed", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=meter,c=ffffff\r"); }, if (t % 100 == 0) say((t / 100) % 2 ? "!HFL:v=0\r" : "!HFL:v=255\r"));
  S9_CASE("C seek across an off / white seam every 40 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=off,at=0\r"); line("!HFA:i=solid,c=ffffff,at=100\r"); line("!**C:bpm=120,beat=0\r"); },
          if (t % 40 == 0) say((t / 40) % 2 ? "!**C:bpm=120,beat=0\r" : "!**C:bpm=120,beat=100\r"));
  S9_CASE("the same with a bare C (no bpm=)", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=off,at=0\r"); line("!HFA:i=solid,c=ffffff,at=100\r"); line("!**C:bpm=120,beat=0\r"); },
          if (t % 40 == 0) say((t / 40) % 2 ? "!**C:beat=0\r" : "!**C:beat=100\r"));
  S9_CASE("re-seed parity: pump am=2 m=255 at 300 BPM, C beat=0 / beat=1 every 100 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff,at=0,am=2,m=255\r"); line("!**C:bpm=300,beat=0\r"); },
          if (t % 100 == 0) say((t / 100) % 2 ? "!**C:bpm=300,beat=1\r" : "!**C:bpm=300,beat=0\r"));
  S9_CASE("re-seed parity: pump am=1 at 120 BPM, C beat=0 / beat=1 every 100 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff,at=0,am=1,m=255\r"); line("!**C:bpm=120,bpb=4,beat=0\r"); },
          if (t % 100 == 0) say((t / 100) % 2 ? "!**C:bpm=120,bpb=4,beat=1\r" : "!**C:bpm=120,bpb=4,beat=0\r"));
  S9_CASE("re-seed: pump am=2 m=255 at 120 BPM, C:bpm=120,ph=0 every 120 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff,at=0,am=2,m=255\r"); line("!**C:bpm=120,beat=0\r"); },
          if (t % 120 == 0) say("!**C:bpm=120,ph=0\r"));
  S9_CASE("seam parity: 32 one-beat sections at 300 BPM, am=2 with and without an=2,ap=1", 0, 6.3, { line("!**M:v=show\r");
          sayScore32("!HFA:i=solid,c=ffffff,am=2,m=255%s,at=%d\r", ",an=2,ap=1", ""); line("!**C:bpm=300,beat=0\r"); }, );
  S9_CASE("seam: 32 one-beat i=flash sections at 300 BPM", 0, 6.3, { line("!**M:v=show\r"); sayScore32("!HFA:i=flash,c=%s,s=255,at=%d\r", "fffffe", "ffffff"); line("!**C:bpm=300,beat=0\r"); }, );
  S9_CASE("the same at 240 BPM", 0, 7.9, { line("!**M:v=show\r"); sayScore32("!HFA:i=flash,c=%s,s=255,at=%d\r", "fffffe", "ffffff"); line("!**C:bpm=240,beat=0\r"); }, );
  S9_CASE("live A i=flash s=255 re-sent every 200 ms", 0, 8, line("!**M:v=show\r"), if (t % 200 == 0) say((t / 200) % 2 ? "!HFA:i=flash,c=ffffff,s=255\r" : "!HFA:i=flash,c=fffffe,s=255\r"));
  S9_CASE("reset loop (rear, white off colour): M:v=show + C / M:v=idle every 60 ms", 1, 8, , { if (t % 60 == 0) say("!**M:v=show\r!**C:bpm=120\r"); if (t % 60 == 30) say("!**M:v=idle\r"); });
  S9_CASE("Resend loop (rear): M:v=show / C every 60 ms", 1, 8, { line("!**M:v=show\r"); line("!HRA:i=solid,c=ffffff\r"); }, { if (t % 60 == 0) say("!**M:v=show\r"); if (t % 60 == 30) say("!**C:bpm=120\r"); });
  S9_CASE("P + X + A + X inside each 340 ms", 0, 8, line("!**M:v=show\r"),
          { int k = t % 340; if (k == 0) say("!HFP:c=ffffff,d=170\r"); if (k == 60) say("!HFX\r"); if (k == 175) say("!HFA:i=solid,c=ffffff\r"); if (k == 235) say("!HFX\r"); });
  S9_CASE("P i=off every 340 ms over solid white, with a B dip between", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r"); },
          { int k = t % 340; if (k == 0) say("!HFP:i=off,d=170\r"); if (k == 200) say("!HFB:b=0\r"); if (k == 250) say("!HFB:b=255\r"); });
  CHECK_EQ(worst, 0);
}

// ---- random and hostile line streams -------------------------------------------------------------
static int R(int n) { return rand() % n; }
static std::string N(int v) { return std::to_string(v); }
static const char* G_FX[] = {"off", "solid", "flash", "pulse", "rainbow", "scan", "sparkle", "meter", "comet", "chase", "wipe", "gradient",
                             "colorcycle", "twinkle", "native:1", "native:6", "native:7", "native:3", "bogus"};
static const char* G_COL[] = {"ffffff", "ff0000", "00ff00", "0000ff", "000000", "3b82f6", "ffff00"};
static const char* G_UNIT[] = {"H*", "HF", "HR", "HT", "**", "*F", "*R", "*T"};
// One contract line, no terminator. watched: every M:v=show carries wd= (a host that is Studio).
static std::string genLine(bool watched = false) {
  std::string u = G_UNIT[R(8)], fx = G_FX[R(19)], c = G_COL[R(7)];
  auto pump = [] { return ",m=" + N(R(2) ? 255 : R(256)) + ",am=" + N(R(5)); };
  switch (R(23)) {
    case 0: case 1: case 2: return "!" + u + "A:i=" + fx + ",c=" + c + (R(2) ? ",b=" + N(R(2) ? 255 : R(256)) : "") + (R(3) ? "" : ",s=" + N(R(256))) + (R(3) ? "" : pump());
    case 3: return "!" + u + "A:c=" + c + (R(2) ? ",b=" + N(R(256)) : "");
    case 4: case 5: case 6: return "!" + u + "A:i=" + fx + ",c=" + c + ",at=" + N(R(40)) + (R(2) ? pump() : "") + (R(3) ? "" : ",an=" + N(1 + R(4)) + ",ap=" + N(R(4))) +
                                   (R(2) ? ",ae=" + std::string(G_FX[R(19)]) + ",ac=" + G_COL[R(7)] + ",ad=" + N(R(3000)) : "");
    case 7: case 8: return "!" + u + "P:c=" + c + (R(2) ? ",d=" + N(R(3000)) : "") + (R(2) ? ",b=" + N(R(256)) : "") + (R(3) ? "" : ",i=" + fx);
    case 9: case 10: return "!" + u + "C:bpm=" + N(R(4) ? 60 + R(241) : R(400)) + (R(2) ? ",ph=" + N(R(600)) : "") + (R(2) ? ",bpb=" + N(R(14)) : "") + (R(2) ? ",beat=" + N(R(40)) : "");
    case 11: return "!" + u + "C:" + (R(2) ? "ph=" + N(R(600)) + "," : "") + "beat=" + N(R(40));
    case 12: return "!" + u + "C:bpm=0";
    case 13: return "!" + u + "B:b=" + N(R(2) ? (R(2) ? 0 : 255) : R(256));
    case 14: return "!" + u + "L:v=" + N(R(2) ? (R(2) ? 0 : 255) : R(256));
    case 15: case 16: return "!" + u + "X";
    case 17: return "!" + u + "M:v=show" + ((watched || R(2)) ? ",wd=" + N(2 + R(4)) : "");
    case 18: return "!" + u + "M:v=idle";
    case 19: return "!" + u + "Q" + (R(2) ? ":r=" + N(R(120)) : "");
    case 20: { char b[40]; snprintf(b, sizeof b, "V:n=%d,k=%04x", R(4), R(3) ? 0 : R(65536)); return "!" + u + b; }
    case 21: return "!" + u + "A:i=meter,c=ffffff";
    default: return "!" + u + "A:i=solid,c=ffffff" + (R(2) ? pump() : "");
  }
}
static std::string mutate(std::string l) {           // one wire fault
  if (l.empty()) return l;
  switch (R(6)) {
    case 0: l[R((int)l.size())] ^= (char)(1 << R(8)); break;
    case 1: l.erase(R((int)l.size()), 1); break;
    case 2: l = l.substr(0, R((int)l.size())); break;
    case 3: l = l.substr(R((int)l.size())); break;
    case 4: l.insert(R((int)l.size()), 1, "\r\n\0\xff|"[R(5)]); break;
    default: l += "|" + N(R(9)); break;
  }
  return l;
}
// What the operator's controller sends: stock LED looks (strobing ones included), settings,
// sequences, servo commands and the brightness command. Returns the owner's new level, or -1.
static const char* G_STOCK[] = {"F0055", "A0059|3", "R0071", "A006", "T001", "A0022|2", "F0034|4", "A0075", "A096", "A0971", "A098", "A0992",
                                "S1|4", "S4", "S5", "S7", "S8", "S9", "F1013", "A104", "A105|2", "A198", "A199", "T1042"};
struct Stream {                                      // drives the wire and measures the three jewels
  Photo ph[3] = {Photo(0), Photo(1), Photo(2)};
  uint64_t lo = 1000, hi = 4000;
  void step() { pass(rnd(lo, hi)); for (auto& p : ph) p.sample(); }
  void until(uint64_t t) { while (us < t) step(); }
  void settle() { while (!wireIdle()) step(); }
  void one(bool watched, int faultPct, int stockPct) {
    if (R(100) < stockPct) {
      if (R(6) == 0) { int lv = 20 + R(236); char b[16]; snprintf(b, sizeof b, "HAB%03d\r", lv);
        settle(); for (auto& p : ph) p.ceil = std::max(p.ceil, lv);     // the owner's level may rise the moment the line lands
        say(b); settle(); until(us + 20000); for (auto& p : ph) p.ceil = lv; }
      else say((std::string(G_STOCK[R(sizeof G_STOCK / sizeof *G_STOCK)]) + "\r").c_str());
    } else { std::string l = genLine(watched); if (R(100) < faultPct) l = mutate(l); sayAs(l + "\r", true); }
  }
  void traffic(double secs, bool watched, int faultPct, int stockPct) {
    uint64_t end = us + (uint64_t)(secs * 1e6);
    while (us < end) { int burst = R(4) ? 1 : 2 + R(12); for (int i = 0; i < burst; i++) one(watched, faultPct, stockPct); until(us + (uint64_t)R(R(5) ? 400000 : 2500000)); }
    settle();
  }
  int worstOnsets() const { return std::max(ph[0].maxPerSecond(), std::max(ph[1].maxPerSecond(), ph[2].maxPerSecond())); }
  int overCeiling() const { return ph[0].overCeiling + ph[1].overCeiling + ph[2].overCeiling; }
};
// After the show is over the board is the stock firmware's again, and stays that way.
static bool backOnStock(Stream& st, double watch, std::string& why) {
  char b[96]; why.clear();
  for (int hp = 0; hp < HPCOUNT; hp++) if (!stockAgain(hp)) { snprintf(b, sizeof b, "hp%d still the contract's (armed %d look %d score %d fn %d); ", hp, armed(hp), lookUp(hp), scoreLen(hp), LED_command[hp].LEDFunction); why += b; }
  if (!why.empty()) return false;
  long t0[3], m0[3]; for (int i = 0; i < 3; i++) { t0[i] = ledTwitchStarts[i]; m0[i] = hpMoves(i); }
  uint64_t end = us + (uint64_t)(watch * 1e6);
  while (us < end) { st.step(); for (int hp = 0; hp < HPCOUNT; hp++) if (!stockAgain(hp)) { snprintf(b, sizeof b, "hp%d taken again with no line sent; ", hp); why += b; return false; } }
  for (int hp = 0; hp < HPCOUNT; hp++) {
    if (enableTwitchLED[hp] && LED_command[hp].LEDFunction > 99 && ledTwitchStarts[hp] == t0[hp]) { snprintf(b, sizeof b, "hp%d no LED twitch in %.0f s; ", hp, watch); why += b; }
    if (enableTwitchHP[hp] && HP_command[hp].HPFunction > 99 && hpMoves(hp) == m0[hp]) { snprintf(b, sizeof b, "hp%d no servo twitch in %.0f s; ", hp, watch); why += b; }
  }
  return why.empty();
}
// THE PROPERTY (S9, and the lifecycle's promise): whatever arrives, at most three onsets a second on
// any jewel, nothing above the ceiling, no servo moved by '!' traffic or by a hold ending, and once
// the show ends (M:v=idle, the watchdog, or the operator's button) the board is stock again.
SCENARIO(s9_property_hostile_streams) {
  const int SEEDS = 150; int bad = 0, worst = 0;
  for (int seed = 0; seed < SEEDS; seed++) {
    fflush(stdout); pid_t pid = fork();
    if (pid == 0) {
      failures = 0; ledOnlyViolations = 0; holdViolations = 0; srand(4000 + seed);
      Stream st; st.until(us + 6000000);
      const int ending = seed % 3;                   // 0: M:v=idle   1: the host dies (watchdog)   2: the operator's button (L6)
      st.traffic(40.0, ending == 1, 10, 12);
      if (ending == 1) { say("!**M:v=show,wd=3\r"); st.settle(); for (int i = 0; i < 12; i++) { std::string l = genLine(true); if (l.find("M:v=idle") == std::string::npos) sayAs(l + "\r", true); } st.settle(); st.until(us + 8000000); }
      else if (ending == 2) { st.settle(); say("A0055|2\r"); st.settle(); st.until(us + 4000000); }   // solid blue for 2 s on all three, then the off colour
      else { say("!**M:v=idle\r"); st.settle(); st.until(us + 500000); }
      std::string why; bool stock = backOnStock(st, 400.0, why);
      int n = st.worstOnsets();
      if (n > 3 || st.overCeiling() || !stock || ledOnlyViolations || holdViolations)
        printf("  seed %2d (%s): %d onsets/s, %d frames over the ceiling, LED-only %ld, hold %ld%s%s\n", seed, ending == 0 ? "idle" : ending == 1 ? "host dies" : "button",
               n, st.overCeiling(), ledOnlyViolations, holdViolations, stock ? "" : ", NOT back on stock: ", why.c_str());
      fflush(stdout);
      _exit((n > 3 || st.overCeiling() || !stock || ledOnlyViolations || holdViolations) ? std::min(n > 3 ? n : 99, 120) : 0);
    }
    int stt = 0; waitpid(pid, &stt, 0); int code = WIFEXITED(stt) ? WEXITSTATUS(stt) : 99;
    if (code) { bad++; if (code < 99) worst = std::max(worst, code); }
  }
  printf("  %d of %d streams broke the property (worst %d onsets in a second)\n", bad, SEEDS, worst);
  CHECK_EQ(bad, 0);
}
