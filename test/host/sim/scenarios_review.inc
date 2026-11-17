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

// Found by the S9 soak: a live look that S4 is still holding (it arrived under 170 ms after the last
// one) is contract state too. The operator's button must drop it, or it comes up after the button.
SCENARIO(l6_button_also_drops_a_live_look_s4_is_holding) {
  run(6); say("!HFA:i=solid,c=ff0000\r"); run(0.05);
  say("!HFX\r"); run(0.03);
  say("!HFA:i=solid,c=00ff00\r"); say("F0055|1\r"); run(0.06);   // the green look is held; the button lands before it is due
  CHECK_EQ(LED_command[0].LEDFunction, 5);
  run(3.0); CHECK(!lookUp(0));  CHECK(stockAgain(0));  CHECK_EQ(LED_command[0].LEDFunction, 100);   // the off colour, after the 1 s
}

// The L3 restore above the ceiling (found by the soak, the minimal case is the reviewer's): a stock
// Short Circuit that has finished leaves its last frame held, and nothing repaints it. A verb-P
// overlay saves that frame; the owner lowers the level while the overlay is up; the frame that
// comes back must be at the new level.
static int peakChannel(int hp) { int m = 0; for (int i = 0; i < 21; i++) m = std::max(m, (int)neoStrips[hp].shown[i]); return m; }
SCENARIO(l3_overlay_restore_follows_the_owners_level) {
  run(8); line("T0075\r", 40.0);
  for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;          // nothing else repaints it
  CHECK(peakChannel(2) > 60);                        // the held stock frame, at BRIGHT 100
  uint8_t held[21]; memcpy(held, neoStrips[2].shown, 21);
  line("!HTP:c=00ff00,d=400\r", 1.5);                // no level change: the frame comes back byte for byte
  CHECK(memcmp(held, neoStrips[2].shown, 21) == 0);
  line("!HTP:c=00ff00,d=2500\r", 0.5);
  line("HTB020\r", 0.3);                             // the owner lowers the level to 20 while the overlay is up
  CHECK_EQ(neoStrips[2].getBrightness(), 20);
  int worst = 0; for (int k = 0; k < 23000; k++) { pass(1000); worst = std::max(worst, peakChannel(2)); }
  CHECK(worst <= 20);                                // (255 * 21) >> 8: a full channel at level 20
  CHECK(peakChannel(2) > 0);                         // and the stock frame did come back
}

// ---- Round 2, finding 2: re-arming the twitch at the end of a hold must not starve it -----------
// A short hold every 40 s (a pulse from a WCB macro, with no show armed) pushed both timers a full
// interval ahead every time, and the owner's auto twitch never came due.
SCENARIO(r2_short_holds_do_not_starve_the_auto_twitch) {
  run(10); long m0[3], t0 = twitches(); for (int i = 0; i < 3; i++) m0[i] = hpMoves(i);
  for (int k = 0; k < 90; k++) { say("!H*P:c=ffffff,d=200\r"); run(40.0); }      // one hour
  long tw[3]; for (int i = 0; i < 3; i++) tw[i] = (hpMoves(i) - m0[i]) / 2;
  printf("  one hour with a 0.2 s hold every 40 s: servo twitches F/R/T %ld %ld %ld, LED twitches %ld\n", tw[0], tw[1], tw[2], twitches() - t0);
  CHECK(tw[0] >= 25 && tw[1] >= 18 && tw[2] >= 18);  // stock alone gives about 44 / 32 / 31 in an hour
  CHECK(twitches() - t0 >= 50);                      // and about 83 LED twitches
}

// ---- Round 2, finding 3: three units asked, three replies ---------------------------------------
static bool txLinesWhole();
static std::string qUnits() { std::string u; size_t p = 0; while ((p = Serial.tx.find("q:ver=", p)) != std::string::npos) { u += Serial.tx[p - 1]; p++; } return u; }
SCENARIO(r3_three_units_asked_back_to_back_all_answer) {
  for (int gapMs : {0, 20, 60}) {
    int bad = trial([gapMs] {
      run(40); while (gWdp.len != 0) pass(500); drain(); Serial.tx.clear(); Serial.maxStallUs = 0;
      for (const char* q : {"!HFQ:r=3\r", "!HRQ:r=3\r", "!HTQ:r=3\r"}) { say(q); run(0.0104 + gapMs / 1000.0); }
      run(1.0); std::string u = qUnits(); std::sort(u.begin(), u.end());
      if (u != "FRT") { printf("  %d ms between the lines: replies from \"%s\"\n", gapMs, qUnits().c_str()); failures++; }
      CHECK(txLinesWhole());  CHECK_EQ(Serial.maxStallUs, 0);
    });
    CHECK_EQ(bad, 0);
  }
  run(8); while (gWdp.len == 0) pass(500);           // and as a WDP announce begins
  Serial.tx.clear(); for (const char* q : {"!HFQ:r=3\r", "!HRQ:r=3\r", "!HTQ:r=3\r"}) { say(q); run(0.07); }
  run(1.5); std::string u = qUnits(); std::sort(u.begin(), u.end()); CHECK(u == "FRT");  CHECK(txLinesWhole());
  // the same unit asked twice before it could answer (an announce is leaving): one reply, the newer route
  drain(); run(20); while (gWdp.len == 0) pass(500);
  Serial.tx.clear(); say("!HFQ:r=1\r!HFQ:r=2\r"); run(1.5);
  CHECK(qUnits() == "F");  CHECK(Serial.tx.find(";W2,;S0!HFq:") != std::string::npos);  CHECK(txLinesWhole());
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
// What an eye at the dome sees of one jewel: the FIELD LEVEL, the mean over its seven pixels of each
// pixel's brightest channel, as a fraction of the owner's ceiling (the strip's level; BRIGHT 100 at
// boot). An observer calls the field dark below one level and lit above another; a dark-to-lit step
// is an ONSET. S9: at most three onsets in any second, whatever lines arrive, and never brighter
// than the ceiling. The limit must not depend on where an observer draws the two lines, so three
// observers count, the same three as the logics fork's property stage: 10 % and 50 % of the ceiling,
// 19 % and 75 %, 4 % and 31 %. (The core's gate promises this for any pair at least a STEP apart, a
// quarter of the ceiling plus one level; swings smaller than that are a look's texture and are not
// limited. At a low ceiling 27 % is less than a step, so an observer's "lit" line is never drawn
// closer to its "dark" line than a step and one level.)
// WHOSE onsets count. Two modes, because the first one alone hid a real path (verb B dimming and
// relighting STOCK light on an armed unit: nobody "had" the jewel, so nothing was counted):
//   * STRICT (the path table, and every second hostile stream): every onset counts, whoever drew the
//     pixels. One thing is set aside: the relight a stock COMMAND causes by itself, for 400 ms after
//     the board handles the line (the owner's button may light a jewel when the owner likes); the
//     observers start counting afresh there. For that to be the only stock light there is, these
//     streams run with the stock LED auto twitch off and send only stock looks that hold still.
//   * ATTRIBUTED (the other streams, which do send the strobing stock looks: stock function 2, Short
//     Circuit, random twitch sequences): an onset counts while the layer has the jewel's LEDs or let
//     go of them in the last 300 ms, and light the stock firmware adds starts the count afresh.
static int fullScale(int level) { return level >= 255 ? 255 : (255 * (level + 1)) >> 8; }
struct Observer {
  int darkPct, litPct; bool lit = false; std::vector<uint64_t> onsets, recent; int worst = 0;
  int minGap = 0;                                    // 7 * (step + 1 level), set with the ceiling
  uint64_t closestUs = ~0ULL;                        // the closest two counted onsets
  bool isLit(int S, int FS) const { return S * 100 >= FS * litPct && S * 100 >= FS * darkPct + minGap * 100; }
  void see(int S, int FS, bool counts, uint64_t now) {
    if (!lit && isLit(S, FS)) {
      lit = true;
      if (counts) { if (!recent.empty() && now - recent.back() < closestUs) closestUs = now - recent.back();
                    onsets.push_back(now); recent.push_back(now); int n = 0; for (uint64_t x : recent) if (x + 1000000ULL > now) n++; worst = std::max(worst, n); }
    } else if (lit && S * 100 <= FS * darkPct) lit = false;
  }
  void reread(int S, int FS) { if (isLit(S, FS)) lit = true; else if (S * 100 <= FS * darkPct) lit = false; }
  void afresh() { recent.clear(); }
  int maxPerSecond() const { return worst; }
};
// (g) the field's COLOUR while it is lit, as one observer sees it: the mean colour, normalised to its
// brightest channel. A jump of more than 96 in any channel against the frame before is a change of
// look. S4: such changes are at least 170 ms apart, whichever path asked.
// A colour is SEEN, and so dated, at the first frame whose BRIGHTEST LED is at 4 levels or more (the
// core's LOOK_MIN_LEVEL since CA1.6; until then this observer wanted a field mean of 4 levels, which
// missed one green pixel at 23). Under that a frame shows no colour: a pulse that fades in through 1,
// 2 and 3 levels has changed the field's colour at the 4.
struct ColourObs {
  int darkPct, litPct; bool lit = false, known = false, any = false; int chroma[3] = {0, 0, 0}; uint64_t at = 0;
  void see(int S, int FS, long sr, long sg, long sb, int peak, bool counts, uint64_t now, uint64_t& closest, long& changes) {
    if (!lit && S * 100 >= FS * litPct) lit = true; else if (lit && S * 100 <= FS * darkPct) lit = false;
    const long top = std::max(sr, std::max(sg, sb));
    if (!lit || peak < 4) return;                    // no LED at 4 levels: no colour
    const int c[3] = {(int)(sr * 255 / top), (int)(sg * 255 / top), (int)(sb * 255 / top)};
    int far = 0; for (int i = 0; i < 3; i++) far = std::max(far, abs(c[i] - chroma[i]));
    if (known && far > 96) {
      if (counts) { if (any && now - at < closest) closest = now - at; at = now; any = true; changes++; }
      else any = false;
    }
    for (int i = 0; i < 3; i++) chroma[i] = c[i];
    known = true;
  }
  void afresh() { any = false; }
};
// (h) A PART OF THE FIELD (spec S12, core revision CA1.6). The whole-field gate cannot see a part of the
// jewel going dark and lit while the whole stays legal. A REGION is a set of pixels; it is lit when its
// level (the mean over its pixels of each pixel's brightest channel) reaches an observer's "lit" line
// AND at least a quarter of the field in it is not dark: two pixels of the seven. It is dark again at
// the observer's "dark" line. The limit is S9's: three onsets of one region in any one second.
// The jewel is a centre (pixel 0) and a ring of six (1 to 6). Its regions: the whole; the centre with
// each half of the ring, and each half of the ring alone (three pixels in a row, six rotations of
// each); the three opposite pairs; the six pairs of neighbours. Twenty-two, each seen by the three
// wide observers.
struct Region { const char* name; uint8_t mask; };
static std::vector<Region> jewelRegions() {
  static char names[22][24]; std::vector<Region> r; int n = 0;
  auto add = [&](const char* fmt, int a, uint8_t mask) { snprintf(names[n], sizeof names[n], fmt, a, a % 6 + 1); r.push_back({names[n], mask}); n++; };
  auto ring = [](int k) { return (uint8_t)(1u << ((k - 1) % 6 + 1)); };          // ring pixel k (1..6, wrapping)
  add("whole", 0, 0x7F);
  for (int k = 1; k <= 6; k++) add("centre+half from %d", k, (uint8_t)(1u | ring(k) | ring(k + 1) | ring(k + 2)));
  for (int k = 1; k <= 6; k++) add("half from %d", k, (uint8_t)(ring(k) | ring(k + 1) | ring(k + 2)));
  for (int k = 1; k <= 3; k++) { snprintf(names[n], sizeof names[n], "opposite %d,%d", k, k + 3); r.push_back({names[n], (uint8_t)(ring(k) | ring(k + 3))}); n++; }
  for (int k = 1; k <= 6; k++) add("neighbours %d,%d", k, (uint8_t)(ring(k) | ring(k + 1)));
  return r;
}
struct RegionObs {
  int darkPct = 0, litPct = 0; bool lit = false; std::vector<uint64_t> recent, worstTimes; int worst = 0; uint64_t closestUs = ~0ULL, worstAt = 0;
  // m: each pixel's brightest channel; full: the ceiling's full scale for one pixel
  void see(const int* m, uint8_t mask, int full, bool counts, uint64_t now) {
    int S = 0, n = 0, alive = 0;
    for (int i = 0; i < 7; i++) if (mask & (1u << i)) { S += m[i]; n++; if (m[i] * 100 > full * darkPct) alive++; }
    const int FS = n * full;
    if (!lit && S * 100 >= FS * litPct && alive >= 2) {
      lit = true;
      if (counts) { if (!recent.empty() && now - recent.back() < closestUs) closestUs = now - recent.back();
                    recent.push_back(now); int c = 0; for (uint64_t x : recent) if (x + 1000000ULL > now) c++; if (c > worst) { worst = c; worstAt = now; worstTimes.clear(); for (uint64_t x : recent) if (x + 1000000ULL > now) worstTimes.push_back(x); } }
    } else if (lit && (S * 100 <= FS * darkPct || alive < 2)) lit = false;
  }
  void reread(const int* m, uint8_t mask, int full) { int S = 0, n = 0, alive = 0; for (int i = 0; i < 7; i++) if (mask & (1u << i)) { S += m[i]; n++; if (m[i] * 100 > full * darkPct) alive++; }
    if (S * 100 >= n * full * litPct && alive >= 2) lit = true; else if (S * 100 <= n * full * darkPct || alive < 2) lit = false; }
  void afresh() { recent.clear(); }
};
struct Photo {
  int hp, ceil; bool started = false; uint64_t lastHad = 0; bool everHad = false;
  Observer obs[3] = {{10, 50}, {19, 75}, {4, 31}};   // (a) brightest channel, wide bands
  Observer narrow[4] = {{3, 15}, {40, 52}, {45, 60}, {80, 95}};   // ...and bands about a step wide (S9's tenth of the ceiling)
  Observer lum[3] = {{10, 50}, {19, 75}, {4, 31}};   // (f) Rec.709 luminance, the gate's own integer formula per LED
  ColourObs colour[5] = {{10, 50}, {19, 75}, {4, 31}, {3, 15}, {45, 60}};
  std::vector<Region> regions = jewelRegions();     // (h) parts of the field
  std::vector<RegionObs> regionObs = std::vector<RegionObs>(regions.size() * 3);
  int regionWorst(size_t r) const { int w = 0; for (int k = 0; k < 3; k++) w = std::max(w, regionObs[r * 3 + k].worst); return w; }
  int worstRegion(std::string* which = nullptr) const { int w = 0; for (size_t r = 0; r < regions.size(); r++) if (regionWorst(r) > w) { w = regionWorst(r); if (which) *which = regions[r].name; } return w; }
  uint64_t colourClosestUs = ~0ULL, colourBadAt = 0; long colourChanges = 0;
  int peak = 0, overCeiling = 0, prevS = 0;
  bool strict = false; uint64_t stockLineAt = 0; bool stockLineSeen = false;   // STRICT mode, and when the board last handled a stock command
  bool stockNativeWas = false, edgeOnset = false, edgeColour = false;   // S10: a runs-as-stock native look the contract started is up
  bool hadWas = false, tailOnset = false, tailColour = false;            // ATTRIBUTED: the first change after the layer let go is its hand-back
  bool paused = false;                               // while the owner's brightness command is on its way (see newCeiling)
  explicit Photo(int h, int c = 100) : hp(h), ceil(c) {}
  // The owner changed the strip level: every frame is rescaled, and "dark" and "lit" are fractions of
  // the NEW ceiling from here. That step is the owner's, not an onset: the next frame only re-reads
  // each observer's state (lit, dark, or in between and so unchanged).
  bool reread = false;
  void newCeiling(int c) { ceil = c; reread = true; paused = false; }
  template <class F> void every(F f) { for (auto& o : obs) f(o); for (auto& o : narrow) f(o); for (auto& o : lum) f(o); }
  // EVERY frame the strip latched since the last look is measured, in order, not only the one that is
  // up when the pass ends: the layer can latch a held frame and draw the next one 2 ms later, and a
  // photometer that missed the first dated that onset at a later, smaller rise (soak 800000 + 2315).
  long seenShows = -1;
  void sample() {
    if (paused) { seenShows = neoStrips[hp].showCount; return; }
    const long nowShows = neoStrips[hp].showCount;
    if (seenShows < 0 || nowShows < seenShows || nowShows - seenShows > Adafruit_NeoPixel::RING) seenShows = nowShows;
    if (seenShows == nowShows) { frame(neoStrips[hp].shown); return; }
    while (seenShows < nowShows) frame(neoStrips[hp].ring[seenShows++ % Adafruit_NeoPixel::RING]);
  }
  std::vector<std::pair<uint64_t, int>> colourLog;   // every counted change of colour: when, and which observer
  struct Was { uint64_t at; int fn, eff, pulse; }; std::vector<Was> wasLog;   // the unit's LED function, effect and overlay, at each change
  const Was* wasAt(uint64_t t) const { const Was* w = nullptr; for (const Was& x : wasLog) { if (x.at > t) break; w = &x; } return w; }
  void frame(const uint8_t* p) {
    int S = 0, FS = 7 * fullScale(ceil), peakLed = 0, pm[7]; long sr = 0, sg = 0, sb = 0, sy = 0;
    { const int fn = LED_command[hp].LEDFunction, eff = (int)gUnit[hp].s.effect, pu = gUnit[hp].s.pulseActive;
      if (wasLog.empty() || wasLog.back().fn != fn || wasLog.back().eff != eff || wasLog.back().pulse != pu) wasLog.push_back({us, fn, eff, pu}); }
    if (regionObs[0].litPct == 0) { static const int BAND[3][2] = {{10, 50}, {19, 75}, {4, 31}}; for (size_t i = 0; i < regionObs.size(); i++) { regionObs[i].darkPct = BAND[i % 3][0]; regionObs[i].litPct = BAND[i % 3][1]; } }
    const int step = ceil < 10 ? 2 : ceil / 10 + 1;
    every([&](Observer& o) { o.minGap = 7 * (step + 1); });
    for (int i = 0; i < 7; i++) { const int g = p[i * 3], r = p[i * 3 + 1], b = p[i * 3 + 2];      // the strip keeps G,R,B
      const int m = std::max(r, std::max(g, b)); S += m; pm[i] = m; peak = std::max(peak, m); peakLed = std::max(peakLed, m); if (m > fullScale(ceil)) overCeiling++;
      sr += r; sg += g; sb += b; sy += (54 * r + 183 * g + 19 * b) >> 8; }
    if (neoStrips[hp].getBrightness() > ceil) overCeiling++;
    if (simHasLeds(hp)) { lastHad = us; everHad = true; }
    if (!started || reread) { started = true; reread = false; prevS = S; for (auto& o : obs) o.reread(S, FS); for (auto& o : narrow) o.reread(S, FS); for (auto& o : lum) o.reread((int)sy, FS);
      for (size_t i = 0; i < regionObs.size(); i++) regionObs[i].reread(pm, regions[i / 3].mask, fullScale(ceil)); return; }
    const bool afterStockLine = stockLineSeen && us - stockLineAt <= 400000ULL;
    // S10: the own frames of a runs-as-stock native look the contract started are the stock look's, like
    // a stock command's own relight. Its START is the contract's edge: the first onset after it counts.
    const bool stockNative = simStockNativeUp(hp), has = simHasLeds(hp);
    if (stockNative && !stockNativeWas) edgeOnset = edgeColour = true;
    stockNativeWas = stockNative;
    if (hadWas && !has) tailOnset = tailColour = true;               // the layer has just let go
    hadWas = has;
    bool counts = strict ? true : (everHad && us - lastHad <= 300000ULL);
    bool cOnset = counts, cColour = counts;
    // ATTRIBUTED: after the layer let go, the FIRST onset and the first change of colour are its
    // hand-back; what the stock look does after that is its own.
    if (!strict && !has) { cOnset = cOnset && tailOnset; cColour = cColour && tailColour; }
    // S10: the own frames of a runs-as-stock native look are the stock look's; its start is the contract's edge.
    if (stockNative) { cOnset = cOnset && edgeOnset; cColour = cColour && edgeColour; }
    if (afterStockLine) cOnset = cColour = false;                    // a stock command's own relight, in both modes
    const bool fresh = afterStockLine || (stockNative && !edgeOnset) || (!strict && !counts && S > prevS);
    if (fresh) { every([](Observer& o) { o.afresh(); }); for (auto& c : colour) c.afresh(); for (auto& o : regionObs) o.afresh(); }   // the owner's own light
    prevS = S;
    size_t n0 = 0; every([&](Observer& o) { n0 += o.onsets.size(); }); const long c0 = colourChanges;
    for (auto& o : obs) o.see(S, FS, cOnset, us);
    for (auto& o : narrow) o.see(S, FS, cOnset, us);
    for (auto& o : lum) o.see((int)sy, FS, cOnset, us);
    for (size_t i = 0; i < regionObs.size(); i++) regionObs[i].see(pm, regions[i / 3].mask, fullScale(ceil), cOnset, us);
    { const uint64_t was = colourClosestUs; for (auto& c : colour) { const long n = colourChanges; c.see(S, FS, sr, sg, sb, peakLed, cColour, us, colourClosestUs, colourChanges);
        if (colourChanges != n) colourLog.push_back({c.at, (int)(&c - colour)}); }
      if (colourClosestUs < was && colourClosestUs < 165000ULL) colourBadAt = us; }
    size_t n1 = 0; every([&](Observer& o) { n1 += o.onsets.size(); });
    if (n1 != n0) { edgeOnset = false; tailOnset = false; }
    if (colourChanges != c0) { edgeColour = false; tailColour = false; }
  }
  int maxPerSecond() const { int w = 0; for (auto& o : obs) w = std::max(w, o.worst); for (auto& o : narrow) w = std::max(w, o.worst); for (auto& o : lum) w = std::max(w, o.worst); return w; }
  uint64_t closestOnsetsUs() const { uint64_t c = ~0ULL; for (auto& o : obs) c = std::min(c, o.closestUs); for (auto& o : narrow) c = std::min(c, o.closestUs); for (auto& o : lum) c = std::min(c, o.closestUs); return c; }
  // Broken: more than three onsets in a second for any observer, two onsets under 334 ms apart (340 less
  // the photometer's own pass), or two changes of colour under 165 ms apart (170 likewise).
  bool broken() const { return maxPerSecond() > 3 || closestOnsetsUs() < 334000ULL || colourClosestUs < 165000ULL; }
};
// lines on a timetable (tick = ms), sampled every 1 ms pass
static int onsetsPerSecond(int hp, double secs, const std::function<void(int)>& everyMs) {
  Photo p(hp); p.strict = true; uint64_t end = us + (uint64_t)(secs * 1e6); int tick = 0;
  while (us < end) { everyMs(tick++); pass(1000); p.sample(); }
  if (p.closestOnsetsUs() < 334000ULL) { printf("    two onsets %.0f ms apart\n", p.closestOnsetsUs() / 1000.0); return 99; }
  if (p.colourClosestUs < 165000ULL) { printf("    two changes of colour %.0f ms apart\n", p.colourClosestUs / 1000.0); return 98; }
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
  // the logics fork's hostile profiles that the table above lacked, and the two things its property stage found
  S9_CASE("P b=0 / b=255 over solid white every 50 ms (b= on P is the overlay's alone)", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r"); },
          { if (t % 100 == 0) say("!HFP:i=solid,c=ffffff,b=0\r"); if (t % 100 == 50) say("!HFP:i=solid,c=ffffff,b=255\r"); });
  S9_CASE("A cancelling an overlay, with P", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=off\r"); },
          { int k = t % 680; if (k == 0) say("!HFP:i=solid,c=ffffff\r"); if (k == 90) say("!HFA:i=off\r"); if (k == 260) say("!HFA:i=solid,c=ffffff\r");
            if (k == 340) say("!HFP:i=off\r"); if (k == 430) say("!HFA:i=solid,c=ffffff\r"); if (k == 600) say("!HFA:i=off\r"); });
  S9_CASE("X and M:v=idle alternation (dark, then the off colour), rear", 1, 8, line("!HRA:i=solid,c=ffffff\r"),
          { if (t % 100 == 0) say("!HRX\r"); if (t % 100 == 50) say("!HRM:v=idle\r"); });
  S9_CASE("live A restarting a pulse every 170 ms", 0, 8, line("!**M:v=show\r"), if (t % 170 == 0) say("!HFA:i=pulse,c=ffffff,s=255\r"));
  S9_CASE("live A between white, X and a native look", 0, 8, line("!**M:v=show\r"),
          { int k = t % 240; if (k == 0) say("!HFA:i=solid,c=ffffff\r"); if (k == 50) say("!HFX\r"); if (k == 60) say("!HFA:i=native:6\r"); if (k == 120) say("!HFA:i=solid,c=ffffff\r"); });
  S9_CASE("a scored flash section delivered again and again", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=flash,c=ffffff,s=255,at=0\r"); line("!**C:bpm=120\r"); },
          if (t % 200 == 0) say("!HFA:i=flash,c=ffffff,s=255,at=0\r"));
  S9_CASE("C:bpm=300 re-seed restarting the pump every 150 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff,am=2,m=255\r"); }, if (t % 150 == 0) say("!**C:bpm=300\r"));
  S9_CASE("B 0 / 255 over a native look (rainbow)", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=native:6\r"); }, if (t % 50 == 0) say((t / 50) % 2 ? "!HFB:b=0\r" : "!HFB:b=255\r"));
  S9_CASE("the owner's level coming back at idle: show + look + B:b=0, then idle (rear)", 1, 8, ,
          { int k = t % 120; if (k == 0) say("!HRM:v=show\r!HRA:i=solid,c=ffffff\r"); if (k == 40) say("!HRB:b=0\r"); if (k == 80) say("!HRM:v=idle\r"); });
  // S4 for every path: colour flips, and luminance onsets that the brightest channel does not see
  S9_CASE("live A red / A blue every 60 ms", 0, 8, line("!**M:v=show\r"), if (t % 60 == 0) say((t / 60) % 2 ? "!HFA:i=solid,c=ff0000\r" : "!HFA:i=solid,c=0000ff\r"));
  S9_CASE("live A blue / A white every 60 ms (a luminance onset, no brightness onset)", 0, 8, line("!**M:v=show\r"), if (t % 60 == 0) say((t / 60) % 2 ? "!HFA:i=solid,c=0000ff\r" : "!HFA:i=solid,c=ffffff\r"));
  S9_CASE("P red / P blue every 180 ms over a green look", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=00ff00\r"); }, if (t % 180 == 0) say((t / 180) % 2 ? "!HFP:c=ff0000,d=170\r" : "!HFP:c=0000ff,d=170\r"));
  S9_CASE("a white accent between two colours of a live look", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ff0000\r"); },
          { int k = t % 680; if (k == 0) say("!HFP:c=ffffff,d=170\r"); if (k == 20) say("!HFA:i=solid,c=0000ff\r"); if (k == 340) say("!HFP:c=ffffff,d=300\r"); if (k == 360) say("!HFA:i=solid,c=ff0000\r"); });
  S9_CASE("C seek across a red / green seam every 40 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ff0000,at=0\r"); line("!HFA:i=solid,c=00ff00,at=100\r"); line("!**C:bpm=120,beat=0\r"); },
          if (t % 40 == 0) say((t / 40) % 2 ? "!**C:bpm=120,beat=0\r" : "!**C:bpm=120,beat=100\r"));
  S9_CASE("32 one-beat sections at 300 BPM, red and blue in turn", 0, 6.3, { line("!**M:v=show\r"); sayScore32("!HFA:i=solid,c=%s,at=%d\r", "ff0000", "0000ff"); line("!**C:bpm=300,beat=0\r"); }, );
  S9_CASE("idle and a red look in turn every 60 ms (rear: the white off colour and red)", 1, 8, line("R0055|1\r", 3.0), { if (t % 120 == 0) say("!HRA:i=solid,c=ff0000\r"); if (t % 120 == 60) say("!HRM:v=idle\r"); });
  S9_CASE("B 0 / 62 over solid white every 25 ms (a quarter of the ceiling: under the first gate's step)", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r"); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HFB:b=0\r" : "!HFB:b=62\r"));
  S9_CASE("L 0 / 62 on a meter every 25 ms", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=meter,c=ffffff\r"); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HFL:v=0\r" : "!HFL:v=62\r"));
  S9_CASE("L 0 / 255 while a native look is up (L feeds the meter look only)", 0, 8, { line("!**M:v=show\r"); line("!HFA:i=native:6\r", 1.0); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HFL:v=0\r" : "!HFL:v=255\r"));
  S9_CASE("a native look that clears the jewel, started again and again over a lit look", 0, 8, { line("!**M:v=show\r"); },
          { int k = t % 200; if (k == 0) say("!HFA:i=solid,c=ffffff\r"); if (k == 100) say("!HFA:i=native:1\r"); });
  // round 2 of the review: verb B over light the contract did not draw (a show armed and not begun keeps the stock look)
  S9_CASE("B 0 / 255 over the stock off colour, show armed and not begun (rear), every 25 ms", 1, 8, { line("R0055|1\r", 3.0); line("!**M:v=show\r"); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HRB:b=0\r" : "!HRB:b=255\r"));
  S9_CASE("the same at one B every 100 ms", 1, 8, { line("R0055|1\r", 3.0); line("!**M:v=show\r"); }, if (t % 100 == 0) say((t / 100) % 2 ? "!HRB:b=0\r" : "!HRB:b=255\r"));
  S9_CASE("B 0 / 255 over the operator's own look (F0059), show armed", 0, 8, { line("F0059\r", 1.0); line("!**M:v=show\r"); }, if (t % 25 == 0) say((t / 25) % 2 ? "!HFB:b=0\r" : "!HFB:b=255\r"));
  S9_CASE("a ride ended by a boundary: M:v=show + B:b=0, then M:v=idle, every 40 ms (rear)", 1, 8, line("R0055|1\r", 3.0), { if (t % 40 == 0) say("!HRM:v=show\r!HRB:b=0\r"); if (t % 40 == 20) say("!HRM:v=idle\r"); });
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
// The stock commands whose look holds still (no flicker, no strobe) and that leave the LED auto twitch off.
static const char* G_CALM[] = {"F0055", "A0059|3", "A0055|1", "R006|2", "A096", "A098", "S4", "S7",
                               "F1013", "A104", "A105|2", "A198", "A199", "T1042", "X0045|1"};
// One latched frame as the core's gates measure it, for the REPLAY (S9_REPLAY=1): the core's own
// contractFieldAsk()/contractFieldTell() run again, from a fresh state, on what the jewel showed.
struct FrameRec { uint32_t ms; uint8_t level, lum, ceil; bool ours; uint32_t sig; byte fn; uint8_t eff; bool pulse; uint8_t chroma[3]; int peak; };
struct Stream {                                      // drives the wire and measures the three jewels
  Photo ph[3] = {Photo(0), Photo(1), Photo(2)};
  std::vector<FrameRec> rec[3]; long recShows[3] = {-1, -1, -1}; bool recOn = getenv("S9_REPLAY") != 0;
  void record() {                                    // every frame latched since the last pass, like the photometer
    for (int hp = 0; hp < 3; hp++) {
      const long nowShows = neoStrips[hp].showCount;
      if (recShows[hp] < 0 || nowShows < recShows[hp] || nowShows - recShows[hp] > Adafruit_NeoPixel::RING) recShows[hp] = nowShows;
      while (recShows[hp] < nowShows) {
        const uint32_t at = neoStrips[hp].ringMs[recShows[hp] % Adafruit_NeoPixel::RING];   // the board's own clock at the latch
        const uint8_t* p = neoStrips[hp].ring[recShows[hp]++ % Adafruit_NeoPixel::RING];
        int sum = 0, sumY = 0, pk = 0; long c[3] = {0, 0, 0};
        for (int i = 0; i < 7; i++) { const int g = p[i * 3], r = p[i * 3 + 1], b = p[i * 3 + 2]; sum += std::max(r, std::max(g, b)); pk = std::max(pk, std::max(r, std::max(g, b))); sumY += (54 * r + 183 * g + 19 * b) >> 8; c[0] += r; c[1] += g; c[2] += b; }
        const long top = std::max(c[0], std::max(c[1], c[2]));
        FrameRec f = {at, (uint8_t)(sum / 7), (uint8_t)(sumY / 7), gUnit[hp].s.ceil,
                      simHasLeds(hp) || (gLatch[hp].touched && (millis() - gLatch[hp].touchMs < 340 || gLatch[hp].owed)), gUnit[hp].s.lookSig,
                      LED_command[hp].LEDFunction, (uint8_t)gUnit[hp].s.effect, gUnit[hp].s.pulseActive,
                      {(uint8_t)(top ? c[0] * 255 / top : 0), (uint8_t)(top ? c[1] * 255 / top : 0), (uint8_t)(top ? c[2] * 255 / top : 0)}, pk};
        if (!rec[hp].empty()) { const FrameRec& l = rec[hp].back(); if (l.level == f.level && l.lum == f.lum && l.peak == f.peak && !memcmp(l.chroma, f.chroma, 3)) continue; }
        rec[hp].push_back(f);
      }
    }
  }
  // THE REPLAY. The core's two gates, asked afresh about every frame a jewel latched (whoever drew it),
  // from a state that was told every frame before it and nothing else:
  //   * the field gate (contractFieldAsk / contractFieldTell) with the frame's level and luminance as the
  //     layer measures them (whole levels); the reference is rescaled when the owner changes the level;
  //   * the look gate (contractLookAllowed / contractLookForced), fed the FRAME: a lit frame whose mean
  //     colour is more than 96 from the lit frame before it in any channel (the colour observer's own
  //     test, and its floor: a LED at 4 levels) is a new look.
  // A frame either gate would have held is marked: F! (field) or C! (look); "up" is a frame at which
  // the field lit or got brighter, "colour" a change of colour. Times are the board's own clock at the
  // latch, the same the layer gave the gate.
  int replay(int hp, uint64_t fromUs, uint64_t toUs) const {
    ContractUnitState rs; int heldAsked = 0, heldNot = 0, heldLook = 0, shown = 0; uint8_t wasCeil = 0;
    int chroma[3] = {0, 0, 0}; bool known = false; uint32_t sig = 1;
    printf("    replay hp%d %.0f..%.0f ms:", hp, fromUs / 1000.0, toUs / 1000.0);
    for (const FrameRec& f : rec[hp]) {
      if (wasCeil && f.ceil != wasCeil) { rs.fieldRef = (uint8_t)((unsigned)rs.fieldRef * f.ceil / wasCeil); rs.fieldRefY = (uint8_t)((unsigned)rs.fieldRefY * f.ceil / wasCeil); }
      rs.ceil = wasCeil = f.ceil;
      const bool in = (uint64_t)f.ms * 1000 + 4000 >= fromUs && (uint64_t)f.ms * 1000 <= toUs + 4000;
      const bool fHeld = contractFieldAsk(rs, f.level, f.lum, f.ms) != 255;
      bool change = false, cHeld = false;
      if (f.peak >= 4) {
        int far = 0; for (int i = 0; i < 3; i++) far = std::max(far, abs((int)f.chroma[i] - chroma[i]));
        if (known && far > 96) { change = true; sig++; cHeld = !contractLookAllowed(rs, sig, f.ms); }
        for (int i = 0; i < 3; i++) chroma[i] = f.chroma[i];
        known = true;
      }
      if (in) { if (fHeld) (f.ours ? heldAsked : heldNot)++; if (cHeld) heldLook++; }
      contractFieldTell(rs, f.level, f.lum, f.ms);
      const bool onset = rs.fieldOnsetMs == f.ms;      // the field lit, or got brighter while lit, at this frame
      if (change) contractLookForced(rs, sig, f.ms);
      if (in && shown++ < 16) printf(" [%u L%d Y%d %s fn%d e%d%s c%02x%02x%02x%s%s%s%s]", f.ms, f.level, f.lum, f.ours ? "asked" : "stock", f.fn, f.eff, f.pulse ? " P" : "",
                                     f.chroma[0], f.chroma[1], f.chroma[2], onset ? " up" : "", change ? " colour" : "", fHeld ? " F!" : "", cHeld ? " C!" : "");
    }
    printf("\n    (the layer's own state now: ceil %d, strip %d, ride %d/%d; the observer's ceiling %d)", gUnit[hp].s.ceil, neoStrips[hp].getBrightness(), gUnit[hp].ride, gUnit[hp].rideWant, ph[hp].ceil);
    printf("\n    -> replayed on these frames the core's field gate would have held %d (%d the layer asked about, %d it did not) and its look gate %d: %s\n",
           heldAsked + heldNot, heldAsked, heldNot, heldLook, (heldAsked + heldNot + heldLook) ? "LAYER" : "ORACLE");
    return heldAsked + heldNot + heldLook;
  }
  uint64_t lo = 1000, hi = 4000; bool strict = false;
  void setStrict() { strict = true; for (auto& p : ph) p.strict = true; for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0; }
  void step() {
    // a stock command is being handled (the owner's brightness command H<unit>B<level> is one: it rescales the field)
    const bool stockLine = stringComplete && !inputString.s.empty() && inputString.s[0] != '!';
    pass(rnd(lo, hi));
    if (stockLine) for (auto& p : ph) { p.stockLineAt = us; p.stockLineSeen = true; }
    for (auto& p : ph) p.sample();
    if (recOn) record();
  }
  void until(uint64_t t) { while (us < t) step(); }
  void settle() { while (!wireIdle()) step(); }
  void one(bool watched, int faultPct, int stockPct) {
    if (R(100) < stockPct) {
      if (R(6) == 0) { int lv = 20 + R(236); char b[16]; snprintf(b, sizeof b, "HAB%03d\r", lv);
        settle(); for (auto& p : ph) p.paused = true;                   // not measured while the level changes under it
        say(b); settle(); until(us + 20000); for (auto& p : ph) p.newCeiling(lv); }
      else if (strict) say((std::string(G_CALM[R(sizeof G_CALM / sizeof *G_CALM)]) + "\r").c_str());
      else say((std::string(G_STOCK[R(sizeof G_STOCK / sizeof *G_STOCK)]) + "\r").c_str());
    } else { std::string l = genLine(watched); if (R(100) < faultPct) l = mutate(l); sayAs(l + "\r", true); }
  }
  // A HOSTILE SCRIPT: two lines that undo each other, alternated on one unit 20 to 80 ms apart. Random
  // lines alone almost never do this, and it is how every path that relit a jewel too often was found.
  void hostile(bool watched) {
    static const char* PAIR[][2] = {
      {"B:b=0", "B:b=255"}, {"L:v=0", "L:v=255"}, {"X", "A:i=solid,c=ffffff"}, {"A:i=off", "A:i=solid,c=ffffff"},
      {"P:c=ffffff,b=0,d=400", "P:c=ffffff,b=255,d=400"}, {"C:bpm=120,beat=0", "C:bpm=120,beat=20"}, {"M:v=idle", "M:v=show,wd=5"},
      {"M:v=show,wd=5", "B:b=0"}, {"A:i=meter,c=ffffff", "L:v=0"}, {"A:i=native:6", "X"}, {"B:b=0", "M:v=idle"}, {"A:i=flash,c=ffffff,s=255", "A:i=flash,c=fffffe,s=255"},
      // colour flips (S4 for every path) and sub-quarter swings (S9's tenth)
      {"A:i=solid,c=ff0000", "A:i=solid,c=0000ff"}, {"A:i=solid,c=0000ff", "A:i=solid,c=ffffff"}, {"P:c=ff0000,d=170", "P:c=0000ff,d=170"},
      {"A:i=solid,c=00ff00", "P:c=ff00ff,d=170"}, {"A:i=solid,c=ff0000,at=0", "C:bpm=120,beat=0"}, {"A:i=solid,c=00ff00,at=20", "C:bpm=120,beat=20"},
      {"C:bpm=120,beat=0", "C:bpm=120,beat=20"}, {"A:i=native:1", "A:i=solid,c=ffffff"}, {"A:i=native:6", "A:i=native:1"}, {"A:i=solid,c=ff0000", "M:v=idle"},
      {"B:b=0", "B:b=62"}, {"L:v=0", "L:v=62"}, {"A:i=solid,c=ffffff,b=0", "A:i=solid,c=ffffff,b=40"}};
    const int k = R(sizeof PAIR / sizeof *PAIR); const std::string u = std::string("!") + G_UNIT[R(4)];   // H*, HF, HR or HT
    (void)watched;
    for (int n = 6 + R(16); n > 0; n--) for (int half = 0; half < 2; half++) { sayAs(u + PAIR[k][half] + "\r", true); until(us + 20000 + (uint64_t)R(60000)); }
  }
  void traffic(double secs, bool watched, int faultPct, int stockPct) {
    uint64_t end = us + (uint64_t)(secs * 1e6);
    while (us < end) {
      if (R(12) == 0) hostile(watched);
      else { int burst = R(4) ? 1 : 2 + R(12); for (int i = 0; i < burst; i++) one(watched, faultPct, stockPct); }
      until(us + (uint64_t)R(R(5) ? 400000 : 2500000));
    }
    settle();
  }
  int worstOnsets() const { return std::max(ph[0].maxPerSecond(), std::max(ph[1].maxPerSecond(), ph[2].maxPerSecond())); }
  bool broken() const { return ph[0].broken() || ph[1].broken() || ph[2].broken(); }
  double closestOnsetsMs() const { return std::min(ph[0].closestOnsetsUs(), std::min(ph[1].closestOnsetsUs(), ph[2].closestOnsetsUs())) / 1000.0; }
  double closestColourMs() const { return std::min(ph[0].colourClosestUs, std::min(ph[1].colourClosestUs, ph[2].colourClosestUs)) / 1000.0; }
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
// A longer soak: SIM_DEFS="-DS9_SEEDS=5000 -DS9_SEED0=90000" ./run_sim.sh
// THE SIX STREAMS OF SEED BASE 800000 that broke after the CA1.5 round (layer a2b6e5e), as the replay
// (S9_REPLAY=1: Stream::replay(), the core's two gates asked afresh about every latched frame) names them:
//   494, 1785, 2315 (two onsets 288 to 291 ms apart, observer 45/60; all three on the old sparkle):
//     ORACLE. The gate held a frame, released it, and the look drew its next frame in the same loop
//     pass; the photometer looked once a pass and never saw the first, so it dated that onset at a
//     later, smaller rise. Replayed with every frame: 340, 342 and 389 ms. The photometer now measures
//     every latched frame (the strip keeps the last sixteen). It counts more, and leaves nothing out.
//   1296, 2625 (two changes of colour 77 and 78 ms apart): LAYER. A look that came up through 3 and 6
//     levels was timed from its first frame; its colour shows from the 6. Scenario (6) below.
//   3799 (63 ms): LAYER. The stock light's own change of colour was not one to the look gate. Scenario (2).
// The soaks after that named four more, all LAYER, scenarios (1), (3), (4), (5) below:
//   800000 + 2812, 2834 and 1200000 + 1086: the hand-back window ran from the hand-back, not from its
//     frame; 800000 + 220, 1140, 1302 and 1600000 + 1512: a verb-B relight of stock light opened no
//     window; 1200000 + 131: the gate was told a rainbow's level for its luminance; 1600000 + 1324:
//     Leia's colour drifting in two half-steps.
// Three fresh seed bases (2000000, 2400000, 2800000) then broke on six streams, one fault, scenario (7):
//   a native look the contract started stayed up while another waited, and a boundary left it as stock.
// After that: 4,000 streams at each of nine seed bases (those six and 3200000, 3600000, 4000000), none broken.
// Two streams of the soak at seed base 500000 are kept in mind here because no hand-made scenario
// reproduces what they found: 643 (ending 1) and 3597 (ending 0). A verb-P overlay over the stock look
// saved that look's frame, the owner lowered the strip level while the overlay was up, and the frame
// that came back was still scaled for the old level: brighter than the new ceiling until the stock
// look repainted. (Fixed in _expireOverlay(): the saved frame is rescaled to the level of the moment.)
#ifndef S9_SEEDS
#define S9_SEEDS 150
#endif
#ifndef S9_SEED0
#define S9_SEED0 4000
#endif
// THE PROPERTY (S9, and the lifecycle's promise): whatever arrives, at most three onsets a second on
// any jewel, nothing above the ceiling, no servo moved by '!' traffic or by a hold ending, and once
// the show ends (M:v=idle, the watchdog, or the operator's button) the board is stock again.
SCENARIO(s9_property_hostile_streams) {
  const int SEEDS = S9_SEEDS; int bad = 0, worst = 0, regionOver = 0, regionMax = 0;
  // S9_ONLY=<n>,<n>,...: run only those streams of the soak again (with S9_REPLAY=1 for what they show)
  std::set<int> only; if (const char* o = getenv("S9_ONLY")) { for (const char* q = o; *q; ) { only.insert(atoi(q)); while (*q && *q != ',') q++; if (*q) q++; } }
  for (int seed = 0; seed < SEEDS; seed++) {
    if (!only.empty() && !only.count(seed)) continue;
    fflush(stdout); pid_t pid = fork();
    if (pid == 0) {
      failures = 0; ledOnlyViolations = 0; holdViolations = 0; srand(S9_SEED0 + seed);
      Stream st; if ((S9_SEED0 + seed) % 2 == 0) st.setStrict();      // every second stream: every onset counts
      st.until(us + 6000000);
      // 0: M:v=idle   1: the host dies (watchdog)   2: the operator's button (L6). To run ONE stream of a soak
      // again: SIM_DEFS="-DS9_SEEDS=1 -DS9_SEED0=<seed0 + n>" and S9_ENDING=<(n / 2) % 3> in the environment.
      const int ending = getenv("S9_ENDING") ? atoi(getenv("S9_ENDING")) : (seed / 2) % 3;
      st.traffic(40.0, ending == 1, 10, 12);
      if (ending == 1) { say("!**M:v=show,wd=3\r"); st.settle(); for (int i = 0; i < 12; i++) { std::string l = genLine(true); if (l.find("M:v=idle") == std::string::npos) sayAs(l + "\r", true); } st.settle(); st.until(us + 8000000); }
      else if (ending == 2) { st.settle(); say("A0055|2\r"); st.settle(); st.until(us + 4000000); }   // solid blue for 2 s on all three, then the off colour
      else { say("!**M:v=idle\r"); st.settle(); st.until(us + 500000); }
      std::string why; bool stock = backOnStock(st, 400.0, why);
      int n = st.worstOnsets();
      if (brk && getenv("S9_TRACE")) for (auto& p : st.ph) p.every([&](Observer& o) { if (o.maxPerSecond() > 3 || o.closestUs < 334000ULL) {
        printf("  hp%d observer %s%d/%d onsets (ms):", p.hp, (&o >= p.lum && &o < p.lum + 3) ? "lum " : "", o.darkPct, o.litPct); for (auto x : o.onsets) printf(" %.0f", x / 1000.0); printf("\n"); } });
      const bool brk = st.broken();
      // S12: the regions of each jewel. Measured on every stream and reported; not part of `broken` (see the
      // S12 scenarios at the end of this file for what is asserted, and the fork's notes for what a stream can still do).
      int regionWorst = 0; for (auto& p : st.ph) regionWorst = std::max(regionWorst, p.worstRegion());
      if (regionWorst > 3 && st.recOn) for (auto& p : st.ph) for (size_t r = 0; r < p.regions.size(); r++) for (int k = 0; k < 3; k++) { const RegionObs& o = p.regionObs[r * 3 + k];
        if (o.worst == regionWorst) { printf("  REGION seed %d hp%d %s (%d/%d): %d onsets in the second to %.0f ms:", seed, p.hp, p.regions[r].name, o.darkPct, o.litPct, o.worst, o.worstAt / 1000.0);
          for (uint64_t x : o.worstTimes) { const Photo::Was* w = p.wasAt(x); printf(" %.0f(fn%d e%d%s)", x / 1000.0, w ? w->fn : -1, w ? w->eff : -1, w && w->pulse ? " P" : ""); } printf("\n"); r = p.regions.size(); break; } }
      if (brk || st.overCeiling() || !stock || ledOnlyViolations || holdViolations)
        printf("  seed %2d (%s, %s): %d onsets/s, %d frames over the ceiling, LED-only %ld, hold %ld%s%s\n", seed, ending == 0 ? "idle" : ending == 1 ? "host dies" : "button", st.strict ? "strict" : "attributed",
               n, st.overCeiling(), ledOnlyViolations, holdViolations, stock ? "" : ", NOT back on stock: ", why.c_str());
      fflush(stdout);
      if (brk && st.recOn) for (auto& p : st.ph) {
        p.every([&](Observer& o) { for (size_t i = 1; i < o.onsets.size(); i++) if (o.onsets[i] - o.onsets[i - 1] < 334000ULL) {
          printf("    hp%d observer %s%d/%d: onsets at %.0f and %.0f ms\n", p.hp, (&o >= p.lum && &o < p.lum + 3) ? "lum " : "", o.darkPct, o.litPct, o.onsets[i - 1] / 1000.0, o.onsets[i] / 1000.0);
          st.replay(p.hp, o.onsets[i - 1] - 400000, o.onsets[i]); } });
        if (p.colourClosestUs < 165000ULL) {
          printf("    hp%d colour changes %.0f ms apart, the second at %.0f ms; as the observers dated them:", p.hp, p.colourClosestUs / 1000.0, p.colourBadAt / 1000.0);
          for (auto& e : p.colourLog) if (e.first + 400000ULL >= p.colourBadAt && e.first <= p.colourBadAt) printf(" %.0f(%d/%d)", e.first / 1000.0, p.colour[e.second].darkPct, p.colour[e.second].litPct);
          printf("\n"); st.replay(p.hp, p.colourBadAt - 400000, p.colourBadAt); }
      }
      if (st.recOn && getenv("S9_SHOW")) { int h = 0; double a = 0, b = 0; if (sscanf(getenv("S9_SHOW"), "%d,%lf,%lf", &h, &a, &b) == 3) st.replay(h, (uint64_t)(a * 1000), (uint64_t)(b * 1000)); }
      if (brk) printf("      closest two onsets %.0f ms, closest two changes of colour %.0f ms\n", st.closestOnsetsMs() > 1e9 ? -1.0 : st.closestOnsetsMs(), st.closestColourMs() > 1e9 ? -1.0 : st.closestColourMs());
      fflush(stdout);
      _exit((brk || st.overCeiling() || !stock || ledOnlyViolations || holdViolations) ? std::min(n > 3 ? n : 99, 120) : (regionWorst > 3 ? 200 + std::min(regionWorst, 50) : 0));
    }
    int stt = 0; waitpid(pid, &stt, 0); int code = WIFEXITED(stt) ? WEXITSTATUS(stt) : 99;
    if (code >= 200) { regionOver++; regionMax = std::max(regionMax, code - 200); }
    else if (code) { bad++; if (code < 99) worst = std::max(worst, code); }
  }
  printf("  %d of %d streams broke the property (worst %d onsets in a second)\n", bad, SEEDS, worst);
  printf("  regions (S12, measured): %d of %d streams lit one part of a jewel more than three times in a second (at most %d)\n", regionOver, SEEDS, regionMax);
  CHECK_EQ(bad, 0);
}

// ===== S10: the allow-listed native looks, classified by how they run as STOCK ===================
// S10 sorts an allow-listed native look into one of two kinds by what its stock form does to the whole
// field: RUNS AS STOCK when the field does not swing by a tenth of the ceiling or more faster than
// every 340 ms, GATED when it does (and this board can pass its frames through the gate). This
// scenario measures every code on the board's allow-list, as the stock command starts it, two ways
// (the brightest channel, which is what the gate measures, and Rec.709 luminance), and compares the
// kind with the table below. A code added to the list with no row here fails; so does a row whose
// look no longer measures as its kind. The list cannot drift from what the looks do.
enum NativeKind { RUNS_AS_STOCK, GATED };
static const struct { int code; const char* name; } S10_NATIVE[] = {
  {1, "Leia"},                                       // all seven pixels re-roll together, several times a second: GATED
  {6, "rainbow"},                                    // the colours move, the field level does not: RUNS AS STOCK
};
// How the layer treats a native look the contract started: how many passes of 20 s left a frame held back.
static long contractNativeHeld(int code) {
  char cmd[40]; snprintf(cmd, sizeof cmd, "!HFA:i=native:%d\r", code);
  run(8); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
  line("!**M:v=show\r"); line(cmd, 1.5);
  if (LED_command[0].LEDFunction != code) { printf("  native:%d did not start\n", code); failures++; }
  long held = 0; for (int k = 0; k < 20000; k++) { pass(1000); held += gLatch[0].pending; }
  return held;
}
struct FieldSwing { int lo = 100000, hi = 0, rises = 0; double closestMs = 1e9; int trough = 100000; uint64_t last = 0;
  void see(int v, int tenth, uint64_t now) {         // v and tenth in the same unit; trough-following, as the gate does
    lo = std::min(lo, v); hi = std::max(hi, v);
    if (v < trough) trough = v;
    if (v - trough >= tenth) { rises++; if (last) closestMs = std::min(closestMs, (now - last) / 1000.0); last = now; trough = v; }
  }
  bool swingsFast() const { return closestMs < 340.0; }
};
static void measureStockLook(int code, FieldSwing& chan, FieldSwing& lum) {   // 20 s of the stock look on the front HP
  char cmd[16]; snprintf(cmd, sizeof cmd, "F0%02d\r", code);
  run(8); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
  line(cmd, 1.0);
  const int full = fullScale(neoStrips[0].getBrightness());                 // one channel at the ceiling
  for (int k = 0; k < 20000; k++) {
    pass(1000); long c = 0, y = 0;
    for (int px = 0; px < 7; px++) { RGB3 p = latched(0, px); c += std::max(p.r, std::max(p.g, p.b)); y += 2126L * p.r + 7152L * p.g + 722L * p.b; }
    chan.see((int)(c * 10 / 7), full, us);           // tenths of a level; a tenth of the ceiling is `full` tenths
    lum.see((int)(y / 7000), full, us);              // Rec.709, the same scale: white at the ceiling is full * 10
  }
}
SCENARIO(s10_native_looks_are_classified_by_their_stock_field_swing) {
  int bad = 0, onList = 0;
  for (int code = 0; code < 100; code++) {
    if (!nativeAllowedFlthy(code)) continue;
    onList++; const char* name = 0;
    const NativeKind kind = nativeGatedFlthy(code) ? GATED : RUNS_AS_STOCK;   // the core's list, the one the layer follows
    for (auto& row : S10_NATIVE) if (row.code == code) name = row.name;
    if (!name) { printf("  native:%d is on the allow-list and has no row in S10_NATIVE\n", code); bad++; continue; }
    bad += trial([=] {
      FieldSwing chan, lum; measureStockLook(code, chan, lum);
      const bool fast = chan.swingsFast() || lum.swingsFast();
      printf("  native:%d %-8s as stock, 20 s: brightest channel %d..%d %% of the ceiling, %d rises of a tenth or more, closest %s ms;"
             " Rec.709 luminance %d..%d %%, %d rises, closest %s ms -> %s\n", code, name,
             chan.lo * 10 / fullScale(100), chan.hi * 10 / fullScale(100), chan.rises, chan.rises > 1 ? std::to_string((int)chan.closestMs).c_str() : "-",
             lum.lo * 10 / fullScale(100), lum.hi * 10 / fullScale(100), lum.rises, lum.rises > 1 ? std::to_string((int)lum.closestMs).c_str() : "-",
             fast ? "its whole field swings faster than every 340 ms: GATED" : "RUNS AS STOCK");
      if (fast != (kind == GATED)) { printf("  FAIL: nativeGatedFlthy() says %s\n", kind == GATED ? "GATED" : "RUNS AS STOCK"); failures++; }
    });
    // ...and the layer does what the kind says: a RUNS-AS-STOCK look started by the contract has no
    // frame held back, a GATED one has (its flicker goes through the gate).
    bad += trial([=] {
      const long held = contractNativeHeld(code);
      printf("  native:%d %-8s through the contract, 20 s: a frame was waiting in %ld of 20000 passes\n", code, name, held);
      if (kind == RUNS_AS_STOCK ? held != 0 : held == 0) failures++;
    });
  }
  CHECK_EQ(onList, (int)(sizeof S10_NATIVE / sizeof *S10_NATIVE));
  CHECK_EQ(bad, 0);
  // and the look S10 names as off the list really is one that could not be on it
  CHECK(!nativeAllowedFlthy(7));
  int sc = trial([] { FieldSwing chan, lum; measureStockLook(7, chan, lum);
    printf("  (off the list) Short Circuit as stock: %d rises of a tenth or more by brightest channel, closest %d ms\n", chan.rises, (int)chan.closestMs);
    if (!chan.swingsFast()) failures++; });
  CHECK_EQ(sc, 0);
}

// ===== What the gates cost a look on a seven-pixel jewel: the held-frames table =====================
// Each contract look, alone, 30 s at three speeds, loop passes of 1 to 4 ms and of 10 to 15 ms: in how
// many passes was a frame waiting at the latch (held back by the field gate or the look gate)? A look
// the gates never touch reads 0. This is a measurement first; the asserts at the end pin what it found.
static double heldPct(const char* setup, uint64_t lo, uint64_t hi, double secs = 30.0) {
  int fd[2]; if (pipe(fd)) return -1;
  fflush(stdout); pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]); srand(77); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
    run(6); line("!**M:v=show\r"); say(setup); runRand(2.0, lo, hi);
    long held = 0, n = 0; uint64_t end = us + (uint64_t)(secs * 1e6);
    while (us < end) { pass(rnd(lo, hi)); held += gLatch[0].pending; n++; }
    double pct = 100.0 * held / n; (void)!write(fd[1], &pct, sizeof pct); _exit(0);
  }
  close(fd[1]); double pct = -1; (void)!read(fd[0], &pct, sizeof pct); close(fd[0]); int st; waitpid(pid, &st, 0);
  return pct;
}
SCENARIO(gate_held_frames_table) {
  const char* LOOKS[] = {"solid", "flash", "pulse", "rainbow", "scan", "sparkle", "meter", "comet", "chase", "wipe", "gradient", "colorcycle", "twinkle"};
  const int SPEEDS[] = {0, 128, 255};
  double worstQuiet = 0, worstTwinkle = 0;
  printf("  %% of passes with a frame waiting at the latch (passes 1-4 ms | passes 10-15 ms), white, at s=0 / s=128 / s=255\n");
  for (const char* look : LOOKS) {
    printf("  %-11s", look);
    for (int pass = 0; pass < 2; pass++) {
      for (int s : SPEEDS) {
        char l[80]; snprintf(l, sizeof l, "!HFA:i=%s,c=ffffff,s=%d\r", look, s);
        std::string setup = l; if (!strcmp(look, "meter")) setup += "!HFL:v=200\r";
        const double p = pass ? heldPct(setup.c_str(), 10000, 15000) : heldPct(setup.c_str(), 1000, 4000);
        printf(" %5.1f", p);
        if (!strcmp(look, "twinkle")) worstTwinkle = std::max(worstTwinkle, p);
        else worstQuiet = std::max(worstQuiet, p);
      }
      printf(pass ? "\n" : "  |");
    }
  }
  // CA1.6, one rise is one onset: a coloured look that fades in crosses the step in its brightest
  // channel before it does in luminance. Until CA1.6 the second crossing was held for 340 ms and
  // the look then jumped. Pulse and wipe in five colours, at the slowest and the fastest speed.
  double worstColour = 0;
  for (const char* look : {"pulse", "wipe"}) for (const char* c : {"ff0000", "00ff00", "0000ff", "116530", "ffffff"}) {
    printf("  %-5s %s", look, c);
    for (int s : {0, 255}) {
      char l[80]; snprintf(l, sizeof l, "!HFA:i=%s,c=%s,s=%d\r", look, c, s);
      const double a = heldPct(l, 1000, 4000), b = heldPct(l, 10000, 15000);
      printf("   s=%-3d %5.1f | %5.1f", s, a, b); worstColour = std::max(worstColour, std::max(a, b));
    }
    printf("\n");
  }
  CHECK(worstColour == 0.0);
  double worstPump = 0;
  for (int m : {64, 255}) for (int bpm : {60, 120, 240}) {
    char l[96]; snprintf(l, sizeof l, "!HFA:i=solid,c=ffffff,m=%d,am=1\r!**C:bpm=%d,ph=0,bpb=4\r", m, bpm);
    const double a = heldPct(l, 1000, 4000), b = heldPct(l, 10000, 15000);
    printf("  beat pump on solid, am=1, m=%3d, %3d BPM: %5.1f | %5.1f\n", m, bpm, a, b); worstPump = std::max(worstPump, std::max(a, b));
  }
  const double leia = heldPct("!HFA:i=native:1\r", 1000, 4000), bow = heldPct("!HFA:i=native:6\r", 1000, 4000);
  printf("  native:1 Leia (gated): %5.1f     native:6 rainbow (runs as stock): %5.1f\n", leia, bow);
  printf("  worst of the twelve other looks: %.1f %%; twinkle: %.1f %%; the beat pump: %.1f %%\n", worstQuiet, worstTwinkle, worstPump);
  CHECK(worstQuiet == 0.0);  CHECK(worstPump == 0.0);  CHECK(bow == 0.0);   // never touched, sparkle included:
  // it was held on 16 to 37 % of passes until it was redrawn with a constant number of lit pixels
  CHECK(worstTwinkle <= 2.0);                        // at s=255 only, under 1 %
  CHECK(leia > 5.0);                                 // a GATED native look is held, by design (S10)
}


// ---- Sparkle, redrawn for seven pixels: a constant number lit, one moving at a time ---------------
SCENARIO(sparkle_keeps_a_constant_number_of_sparks_on_a_bed) {
  for (int s : {0, 64, 128, 192, 255}) {
    int bad = trial([s] {
      run(6); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
      line("!**M:v=show\r"); char l[64]; snprintf(l, sizeof l, "!HFA:i=sparkle,c=ffffff,s=%d\r", s); line(l, 1.0);
      const int want = 2 + (s >> 6); long moves = 0, held = 0; uint8_t prev[21]; memcpy(prev, neoStrips[0].shown, 21); std::set<int> seen;
      for (int k = 0; k < 20000; k++) {
        pass(1000); held += gLatch[0].pending; int top = 0, sparks = 0, changed = 0;
        for (int px = 0; px < 7; px++) top = std::max(top, (int)latched(0, px).r);
        for (int px = 0; px < 7; px++) { RGB3 c = latched(0, px); CHECK(c.r == c.g && c.g == c.b);
          if (c.r == top) { sparks++; seen.insert(px); }
          else if (abs(c.r * 5 - top * 2) > 5) { printf("  s=%d: a pixel between the sparks at %d of %d, want two fifths\n", s, c.r, top); failures++; }   // S12: the bed
          if (memcmp(prev + px * 3, neoStrips[0].shown + px * 3, 3)) changed++; }
        if (failures) break;
        if (sparks != want) { printf("  s=%d: %d sparks, want %d\n", s, sparks, want); failures++; break; }
        if (changed) { moves++; if (changed != 2) { printf("  s=%d: %d pixels changed in one step, want 2 (one spark out, one in)\n", s, changed); failures++; break; } }
        memcpy(prev, neoStrips[0].shown, 21);
      }
      const double stepMs = 150.0 - s * 110.0 / 255.0;
      printf("  s=%3d: %d of 7 at full and the rest at two fifths at every frame, %ld moves in 20 s (one every %.0f ms asked), every pixel used: %s, frames held: %ld\n", s, want, moves, stepMs, seen.size() == 7 ? "yes" : "NO", held);
      CHECK(moves > 20000 / (stepMs + 6) && moves <= 20000 / stepMs + 1);  CHECK_EQ((int)seen.size(), 7);  CHECK_EQ(held, 0);
    });
    CHECK_EQ(bad, 0);
  }
}

// ---- The latch measures finely enough for S9's tenth at a low owner level --------------------------
// At owner level 100 a tenth is 11 levels, at 40 it is 5, at 20 it is 3. Verb B swings a white field by
// just over a step (step + 2 levels) every 25 ms: the swing must be seen as an onset and held to three
// a second at every level. A swing of one level less than a step is texture and passes.
SCENARIO(s9_gate_resolves_a_tenth_at_owner_levels_100_40_20) {
  for (int level : {100, 40, 20}) {
    int bad = trial([level] {
      run(6); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
      char l[32]; snprintf(l, sizeof l, "HFB%03d\r", level); line(l, 0.5);
      line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r", 1.0);
      const int step = level < 10 ? 2 : level / 10 + 1;
      CHECK_EQ(contractFieldStep(gUnit[0].s.ceil), step);  CHECK_EQ(latched(0).r, fullScale(level));
      auto rideFor = [&](int levels) { for (int r = 1; r < 256; r++) if (fullScale(contractLevel((uint8_t)level, (uint8_t)r)) >= levels) return r; return 255; };
      const int big = rideFor(step + 2), small = rideFor(step - 1);
      // an observer whose two lines are exactly a step apart, at the bottom of the range
      Observer o{0, 0}; o.lit = true; long onsets = 0; uint64_t closest = ~0ULL, lastOn = 0; int peak = 0;   // the white look is up
      char up[24], dn[] = "!HFB:b=0\r"; snprintf(up, sizeof up, "!HFB:b=%d\r", big);
      for (int t = 0; t < 8000; t++) {
        if (t % 25 == 0) say((t / 25) % 2 ? dn : up);
        pass(1000); const int lv = latched(0).r; if (t > 100) peak = std::max(peak, lv);
        if (!o.lit && lv >= step) { o.lit = true; onsets++; if (lastOn) closest = std::min(closest, us - lastOn); lastOn = us; } else if (o.lit && lv == 0) o.lit = false;
      }
      printf("  owner level %3d: step %2d levels; B 0 / %d (a swing of %d levels) every 25 ms: %ld onsets in 8 s, the closest two %.0f ms apart\n",
             level, step, big, peak, onsets, closest / 1000.0);
      CHECK(peak >= step + 2 && peak <= step + 3);  CHECK(onsets >= 15 && onsets <= 25);  CHECK(closest >= 334000ULL);
      // one level under a step is texture: every swing passes, 20 a second
      line("!HFB:b=0\r", 1.0); snprintf(up, sizeof up, "!HFB:b=%d\r", small); long swings = 0; bool hi = false;
      for (int t = 0; t < 4000; t++) { if (t % 25 == 0) say((t / 25) % 2 ? dn : up); pass(1000); const int lv = latched(0).r;
        if (!hi && lv >= step - 1 && lv > 0) { hi = true; swings++; } else if (hi && lv == 0) hi = false; }
      printf("                   B 0 / %d (%d levels, under a step): %ld swings in 4 s, none held\n", small, step - 1, swings);
      if (step - 1 >= 1) CHECK(swings >= 70);
    });
    CHECK_EQ(bad, 0);
  }
}

// ---- Two layer faults the soak's replay named (seed base 800000 + 3799; + 2812 and 2834 after the
// sparkle was redrawn), each as its smallest case ---------------------------------------------------
// The photometer for both: every frame latched, every onset and every change of colour counted.
static void watchFor(Photo& p, double secs) { const uint64_t end = us + (uint64_t)(secs * 1e6); while (us < end) { pass(1000); p.sample(); } }
// (1) THE HAND-BACK WINDOW RUNS FROM THE HAND-BACK'S LIGHT. The stock light is asked of the gates for
// 340 ms after the layer lets go. When the gate held the hand-back's own frame (the unit had lit a
// moment before), that frame went up as the 340 ms ran out, and the stock look's NEXT change, a timed
// function ending 170 ms later, was nobody's: a second onset and a second colour inside the limits.
// Here a stock solid blue runs on a 2 s timer and one pulse lands near its end: the pulse lights the
// field (luminance), the blue it gives back is an onset again (level) and is held 340 ms, and the
// timer's end (dark, then the off colour) is swept across the 700 ms after the pulse.
// The stock LED auto twitch off, and whatever sequence it had going over (it runs for some seconds).
static void calmStock() {
  for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
  for (int n = 0; n < 60; n++) { run(0.5); bool busy = false; for (int i = 0; i < HPCOUNT; i++) if (LED_command[i].LEDFunction >= 1 && LED_command[i].LEDFunction <= 7) busy = true; if (!busy) break; }
  run(0.5);
}
static bool tooClose(const Photo& p, const char* what, int n) {
  const double on = p.closestOnsetsUs() / 1000.0, col = p.colourClosestUs / 1000.0;
  if (on >= 334.0 && col >= 165.0) return false;
  printf("  %s %d:%s%s\n", what, n, on < 334.0 ? " two onsets too close" : "", col < 165.0 ? " two changes of colour too close" : "");
  if (on < 334.0) printf("    onsets %.0f ms apart\n", on);
  if (col < 165.0) printf("    changes of colour %.0f ms apart\n", col);
  return true;
}
SCENARIO(s9_a_stock_look_ending_after_a_held_hand_back_keeps_its_distance) {
  int bad = 0;
  for (int x = 100; x <= 800; x += 20) bad += trial([x] {
    run(9); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
    run(1); say("F0055|2\r"); run(0.3);                                        // the owner's own relight, before the photometer looks
    Photo p(0); p.strict = true; watchFor(p, 1.7 - x / 1000.0);                // the timer ends x ms from here
    say("!HFP:c=3b82f6,d=69,b=202\r"); watchFor(p, 2.5);
    CHECK_EQ(LED_command[0].LEDFunction, 100);                                 // the stock timer ran out: the off colour
    CHECK(!dark(0));
    if (tooClose(p, "a pulse this long before the stock timer ends (ms)", x)) failures++;
  });
  CHECK_EQ(bad, 0);
}
// (2) THE STOCK LIGHT'S OWN CHANGE OF COLOUR IS A CHANGE OF COLOUR. The look gate hears of stock light
// by its function number, and a stock function draws what colours it likes under one number (function
// 2, the colour projector, mixes white into its colour pixel by pixel, a new mix every 100 ms). A
// pulse that took the jewel 63 ms after the stock frame's mean colour moved was "170 ms after the last
// look" by the function number and 63 ms after it on the LEDs. The latch now measures every frame's
// mean colour and moves the look gate's time when the stock light changes it.
// Here the colour projector runs under the hostile pair that found it (pulses red and blue alternately,
// 20 to 80 ms apart); forty timings.
SCENARIO(s4_a_contract_look_keeps_its_170_ms_from_the_stock_lights_own_colour_change) {
  int bad = 0;
  for (int n = 0; n < 40; n++) bad += trial([n] {
    run(9); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
    run(1); srand(4100 + n); Photo p(0);             // ATTRIBUTED: the projector's own frames are the stock look's
    say("F0022|12\r"); watchFor(p, 0.6);
    for (int i = 0; i < 160; i++) { say((i % 2) ? "!HFP:c=0000ff,d=170\r" : "!HFP:c=ff0000,d=170\r"); watchFor(p, (20 + rand() % 61) / 1000.0); }
    watchFor(p, 1.0);
    CHECK_EQ(LED_command[0].LEDFunction, 2);         // the stock look is still the unit's
    if (tooClose(p, "pulses over the stock colour projector, timing", n)) failures++;
  });
  CHECK_EQ(bad, 0);
}
// (3) A VERB-B STEP ON STOCK LIGHT IS THIS LAYER'S LIGHT CHANGE. On an armed unit showing stock light,
// B:b=0 darkens the jewel and the next show brings it back: the gate times that relight, and the stock
// code draws the frame. The stock look's next change (its timer ending 37 ms later: dark, then the off
// colour) was not asked, because no hand-back had happened. A ride step now opens the same 340 ms.
// The hostile pair that found it (soak 800000 + 220), its phase against the stock timer swept.
SCENARIO(s9_a_verb_b_relight_of_stock_light_keeps_the_stock_looks_next_change_away) {
  int bad = 0;
  for (int d = 0; d < 400; d += 20) bad += trial([d] {
    run(9); for (int i = 0; i < HPCOUNT; i++) enableTwitchLED[i] = 0;
    run(1); say("F0055|2\r"); run(0.3);
    Photo p(0); p.strict = true; watchFor(p, d / 1000.0);
    for (int i = 0; i < 60; i++) { say((i % 2) ? "!HFM:v=show,wd=5\r" : "!HFB:b=0\r"); watchFor(p, 0.05); }
    watchFor(p, 1.5);
    if (tooClose(p, "B:b=0 and a new show over a stock solid on a timer, phase (ms)", d)) failures++;
  });
  CHECK_EQ(bad, 0);
}
// (4) THE GATE KNOWS A RUNS-AS-STOCK LOOK'S LUMINANCE. The core takes one figure for a native look and
// records it for both measures; the rainbow stands at 75 % of the ceiling by its brightest channel and
// 33 % by luminance. A white look at half level after it is dimmer by the first and brighter by the
// second: an onset the gate did not see, so the relight that followed 326 ms later went out (soak
// 1200000 + 131). The latch now tells the gate the luminance it measures on the look's first frame.
// Two parts. The state: with the rainbow up, the gate's two references are what the jewel shows. And
// the lines of that stream, swept (in the stream the white frame was latched 14 ms after the rainbow
// was let go, which is what made the 326; at 1 ms passes it is 1 ms, so this part is a guard that
// passes on the old layer too, and the state check is the one that fails there).
SCENARIO(s9_a_look_brighter_only_by_luminance_after_the_rainbow_is_an_onset) {
  run(9); calmStock();
  say("!HFM:v=show\r!HFA:i=off\r"); run(1); say("!HFA:i=native:6\r"); run(1);
  { int S = 0, Y = 0; for (int i = 0; i < 7; i++) { const uint8_t* q = neoStrips[0].shown + i * 3; S += std::max(q[0], std::max(q[1], q[2])); Y += (54 * q[1] + 183 * q[0] + 19 * q[2]) >> 8; }
    printf("  the rainbow on the jewel: level %d, luminance %d; the gate's references: %d, %d\n", S / 7, Y / 7, gUnit[0].s.fieldRef, gUnit[0].s.fieldRefY);
    CHECK(abs((int)gUnit[0].s.fieldRef - S / 7) <= 3);  CHECK(abs((int)gUnit[0].s.fieldRefY - Y / 7) <= 3);  CHECK(S / 7 - Y / 7 > 30); }
  int bad = 0;
  for (int d = 0; d <= 120; d += 10) bad += trial([d] {
    run(9); calmStock();
    say("!HFM:v=show\r!HFA:i=off\r"); run(1);                                  // armed and dark
    Photo p(0); p.strict = true; watchFor(p, 0.5);
    say("!HFA:i=native:6\r"); watchFor(p, 0.02 + d / 1000.0);                   // the rainbow has just started...
    say("!HFA:i=solid,c=ffffff,b=160\r"); watchFor(p, 0.2);                     // ...so S4 holds this look: luminance up, level down
    say("!HFB:b=5\r"); watchFor(p, 0.05);
    say("!HFB:b=255\r"); watchFor(p, 1.0);                                      // the relight keeps 340 ms from the white FRAME
    CHECK(lookUp(0));  CHECK(!dark(0));
    if (tooClose(p, "white this long after the rainbow started, then B down and up (ms)", d)) failures++;
  });
  CHECK_EQ(bad, 0);
}
// (5) A LOOK'S OWN COLOURS, WHICHEVER FRAMES AN EYE COMPARES. Stock light and the gated Leia draw their
// own colours under one name. The latch judged each frame against the one before: two half-steps 141 ms
// apart were one change to the observer whose band missed the dim frame between (soak 1600000 + 1324,
// on Leia). Since S12 Leia's frames are paced 340 ms apart, so the case is drawn on STOCK light in the
// hand-back window: the test stands the loop still, opens the window as a hand-back does, and sends
// each frame to the latch as the stock code would.
static void jewelFrame(int r, int g, int b) {
  uint8_t* px = neoStrips[0].getPixels();
  for (int i = 0; i < 7; i++) { px[i * 3] = (uint8_t)g; px[i * 3 + 1] = (uint8_t)r; px[i * 3 + 2] = (uint8_t)b; }
  neoStrips[0].show();
}
static bool shownIs(int r, int g, int b) { const uint8_t* q = neoStrips[0].shown; for (int i = 0; i < 7; i++) if (q[i * 3] != g || q[i * 3 + 1] != r || q[i * 3 + 2] != b) return false; return true; }
static void later(int ms) { us += (uint64_t)ms * 1000; g_ms = (uint32_t)(us / 1000); }
static void handBackNow() { gLatch[0].touchMs = millis(); gLatch[0].touched = true; gLatch[0].extended = true; }   // the window, and no second one
SCENARIO(s4_a_looks_own_colours_are_judged_against_the_colour_of_the_last_change) {
  run(9); calmStock();
  CHECK(!lookUp(0));  CHECK_EQ(LED_command[0].LEDFunction, 100);               // stock light: the off colour
  // a drift in two half-steps: light blue, then 47 and 50 nearer to blue, 70 and 141 ms later
  later(1000); handBackNow(); jewelFrame(100, 0, 0);    CHECK(shownIs(100, 0, 0));
  later(400);  handBackNow(); jewelFrame(54, 54, 100);  CHECK(shownIs(54, 54, 100));   // a change of colour: the 170 ms run from here
  later(70);   handBackNow(); jewelFrame(35, 35, 100);  CHECK(shownIs(35, 35, 100));   // 47 of 255 from it: the same colour
  later(71);   handBackNow(); jewelFrame(16, 16, 100);  CHECK(shownIs(35, 35, 100));   // 97 from it, after 141 ms: held
  later(40);   handBackNow(); jewelFrame(16, 16, 100);  CHECK(shownIs(16, 16, 100));   // after 181 ms: shown
}
// (5b) S12: LEIA'S FRAMES ARE 340 ms APART WHEN A LED GETS BRIGHTER. Leia re-rolls every pixel, and a frame
// that leaves the whole field's level where it was can still light two pixels the frame before left
// dark: a quarter of the field. Through the contract Leia lit one half of the ring four times in a
// second. The test draws Leia's frames itself, as above.
static void jewelPixels(const int* v) {              // white, one level a pixel
  uint8_t* px = neoStrips[0].getPixels();
  for (int i = 0; i < 7; i++) px[i * 3] = px[i * 3 + 1] = px[i * 3 + 2] = (uint8_t)v[i];
  neoStrips[0].show();
}
static bool shownPixels(const int* v) { const uint8_t* q = neoStrips[0].shown; for (int i = 0; i < 7; i++) if (q[i * 3] != v[i]) return false; return true; }
SCENARIO(s12_leias_frames_that_brighten_a_pixel_are_340_ms_apart) {
  run(9); calmStock();
  say("!HFM:v=show\r!HFA:i=native:1\r"); run(3);
  CHECK_EQ(LED_command[0].LEDFunction, 1);  CHECK(gUnit[0].s.base == CB_NATIVE);  CHECK(!gUnit[0].nativeWait);
  static const int A[7] = {50, 0, 0, 50, 50, 50, 50}, B[7] = {50, 50, 50, 0, 0, 50, 50}, C[7] = {50, 40, 40, 0, 0, 50, 50};
  later(1000); jewelPixels(A); CHECK(shownPixels(A));
  later(100);  jewelPixels(B); CHECK(shownPixels(A));   // the same level, another pair lit: it waits
  later(250);  jewelPixels(B); CHECK(shownPixels(B));   // 350 ms after A
  later(50);   jewelPixels(C); CHECK(shownPixels(C));   // no LED brighter: a fall, at once
}
// (6) A LOOK THAT COMES UP DIM HAS CHANGED THE FIELD'S COLOUR WHEN A FRAME SHOWS IT. A green pulse drawn
// as a wipe lights one pixel, then two: 3 levels, then 6. The look gate timed it from its first frame;
// an eye can tell green from the 6. The white look under it came back 171 ms after the first frame and
// 77 ms after the green could be seen (soak 800000 + 2625, and + 1296 with a colour cycle). The latch
// now moves the look gate's time when a contract look's colour first shows. Here: a dim white look
// (the eye is on it), the green wipe pulse over it at seven lengths, and the white coming back.
SCENARIO(s4_a_look_that_comes_up_dim_counts_from_the_frame_that_shows_its_colour) {
  int bad = 0;
  for (int d = 100; d <= 400; d += 50) bad += trial([d] {
    run(9); calmStock();
    say("!HRM:v=show\r!HRA:i=solid,c=ffffff\r"); run(2);
    Photo p(1); p.strict = true; watchFor(p, 0.5);
    say("!HRA:i=solid,c=ffffff,b=12\r"); watchFor(p, 1.0);                      // four levels of white: lit, to an eye that was on it
    char l[48]; snprintf(l, sizeof l, "!HRP:c=00ff00,b=59,i=wipe,d=%d\r", d);
    say(l); watchFor(p, 2.0);
    CHECK(lookUp(1));  CHECK(!dark(1));
    if (tooClose(p, "a green wipe pulse over a dim white look, pulse length (ms)", d)) failures++;
  });
  CHECK_EQ(bad, 0);
}
// (7) THE LOOK THAT STAYS UP WHILE A NATIVE LOOK WAITS IS STILL THIS LAYER'S. Leia is up through the
// contract; the rainbow is asked for and waits for its window (S10: what is up stays up); the unit is
// idled before the window opens. The layer knew its LED slot by the native code it now wanted (6), saw
// stock function 1 on the slot, took it for the operator's and left it: Leia ran on as stock light,
// ungated, until its own timer (fresh soak, seed base 2400000 + 870, and five more like it). The
// rainbow is asked for at forty moments of Leia; whenever it has to wait, the idle takes Leia down.
SCENARIO(s10_idle_while_a_native_look_waits_takes_down_the_look_that_stayed_up) {
  int bad = 0, waited = 0;
  for (int d = 0; d < 400; d += 10) {
    fflush(stdout); pid_t pid = fork();
    if (pid == 0) {
      failures = 0; run(9); calmStock();
      say("!HTA:i=native:1\r"); run(2.0 + d / 1000.0);
      CHECK_EQ(LED_command[2].LEDFunction, 1);
      say("!HTA:i=native:6\r");
      for (int i = 0; i < 200 && gUnit[2].s.nativeCode != 6; i++) run(0.002);   // until the line is in (S4 may hold it 170 ms)
      const bool waits = gUnit[2].nativeWait;        // the rainbow has to wait: Leia stays up meanwhile
      say("!HTM:v=idle\r"); run(3);
      CHECK(gUnit[2].s.base == CB_NONE);
      CHECK_EQ(LED_command[2].LEDFunction, 100);     // the off colour, not Leia running on as stock light
      fflush(stdout); _exit(failures ? 1 : (waits ? 7 : 0));
    }
    int st = 0; waitpid(pid, &st, 0); const int code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    if (code == 1) bad++; else if (code == 7) waited++;
  }
  printf("  the rainbow had to wait at %d of 40 moments\n", waited);
  CHECK_EQ(bad, 0);  CHECK(waited >= 5);
}

// ===== S12: a part of the field. Every contract look alone, at its fastest ========================
// Each look runs by itself for 30 s at s=255 on the front jewel, in white and in 116530, and the
// photometer's twenty-two regions count their onsets. The table is the measurement; the assert is the
// limit, three onsets of one region in any one second.
struct RegionRow { int worst, whole; char region[24]; uint64_t closestUs; };
static RegionRow regionRate(const char* setup, double secs = 30.0) {
  RegionRow row = {-1, -1, "", 0}; int fd[2]; if (pipe(fd)) return row;
  fflush(stdout); pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]); run(9); calmStock();
    say("!HFM:v=show\r"); say(setup); run(2);
    Photo p(0); p.strict = true; watchFor(p, secs);
    std::string which; row.worst = p.worstRegion(&which); row.whole = p.maxPerSecond(); snprintf(row.region, sizeof row.region, "%s", which.c_str());
    row.closestUs = ~0ULL; for (auto& o : p.regionObs) row.closestUs = std::min(row.closestUs, o.closestUs);
    (void)!write(fd[1], &row, sizeof row); _exit(0);
  }
  close(fd[1]); (void)!read(fd[0], &row, sizeof row); close(fd[0]); int st; waitpid(pid, &st, 0);
  return row;
}
SCENARIO(s12_region_rates_each_look_alone_at_its_fastest) {
  static const char* LOOKS[] = {"solid", "flash", "pulse", "rainbow", "scan", "sparkle", "meter", "comet", "chase", "wipe", "gradient", "colorcycle", "twinkle", "native:1", "native:6"};
  int worst = 0;
  printf("  the most often one region lit in a second (and the whole field), s=255: white | 116530; the region; its closest two onsets\n");
  for (const char* look : LOOKS) {
    printf("  %-11s", look);
    for (const char* c : {"ffffff", "116530"}) {
      char l[96]; snprintf(l, sizeof l, "!HFA:i=%s,c=%s,s=255\r%s", look, c, !strcmp(look, "meter") ? "!HFL:v=200\r" : "");
      const RegionRow r = regionRate(l);
      printf("  %d (%d) %-20s %4.0f ms |", r.worst, r.whole, r.worst ? r.region : "-", r.closestUs > 1e15 ? 0.0 : r.closestUs / 1000.0);
      worst = std::max(worst, r.worst);
      if (r.worst < 0) failures++;
    }
    printf("\n");
  }
  CHECK(worst <= 3);
}
// S12, a look by turns with light on the whole jewel. The logics property stage records one case as a
// known failure: a comet and a full look by turns (A:i=comet for 510 ms, A:i=solid for 170 ms). The
// comet's head passes a part of the field and the full look lights it again, while the field's level
// only ever rises from a field that is lit, which no whole-field gate holds. On the jewel the same
// script lit one part 4 times in a second, and a comet under a train of pulses 5 and 6 times (soak
// 5000000 + 608, + 1900). Every look is put through three such scripts here: a solid look by turns
// (510 ms and 170 ms), a white pulse every 50 ms, and the look sent again in another colour every
// 170 ms. The table is the measurement; the limit is three.
static RegionRow regionScript(const char* look, int script, int s) {
  RegionRow row = {-1, -1, "", 0}; int fd[2]; if (pipe(fd)) return row;
  fflush(stdout); pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]); run(9); calmStock(); say("!HFM:v=show\r"); if (!strcmp(look, "meter")) say("!HFL:v=200\r"); run(1);
    Photo p(0); p.strict = true; char a[80], b[80];
    snprintf(a, sizeof a, "!HFA:i=%s,c=ffffff,s=%d\r", look, s); snprintf(b, sizeof b, "!HFA:i=%s,c=00ff00,s=%d\r", look, s);
    if (script == 0) for (int i = 0; i < 30; i++) { say(a); watchFor(p, 0.51); say("!HFA:i=solid,c=ffffff\r"); watchFor(p, 0.17); }
    else if (script == 1) { say(a); for (int i = 0; i < 400; i++) { say("!HFP:c=ffffff,d=170\r"); watchFor(p, 0.05); } }
    else for (int i = 0; i < 120; i++) { say((i % 2) ? b : a); watchFor(p, 0.17); }
    std::string which; row.worst = p.worstRegion(&which); row.whole = p.maxPerSecond(); snprintf(row.region, sizeof row.region, "%s", which.c_str());
    (void)!write(fd[1], &row, sizeof row); _exit(0);
  }
  close(fd[1]); (void)!read(fd[0], &row, sizeof row); close(fd[0]); int st; waitpid(pid, &st, 0);
  return row;
}
SCENARIO(s12_region_rates_each_look_by_turns_with_full_light) {
  static const char* LOOKS[] = {"flash", "pulse", "rainbow", "scan", "sparkle", "meter", "comet", "chase", "wipe", "gradient", "colorcycle", "twinkle"};
  int worst = 0;
  printf("  the most often one region lit in a second: by turns with solid | under pulses | sent again in another colour (each at s=255, s=0)\n");
  for (const char* look : LOOKS) {
    printf("  %-11s", look);
    for (int script = 0; script < 3; script++) {
      for (int s : {255, 0}) { const RegionRow r = regionScript(look, script, s); printf(" %d", r.worst); if (r.worst > 3) printf("(%s)", r.region); worst = std::max(worst, r.worst); if (r.worst < 0) failures++; }
      printf(script < 2 ? "  |" : "\n");
    }
  }
  CHECK(worst <= 3);
}
// S12: Leia and the look that follows it. Leia's frames that brighten a pixel are 340 ms apart, which is
// three a second for a part of the jewel by itself; the look that replaced Leia lit the whole jewel a
// fourth time in that second, because Leia's frames are no onsets of the whole field to the gate (soak,
// stream 14 of the 150: 21032, 21371, 21711 by Leia and 21900 by the hand-back). The gate's time now
// moves with each such frame of Leia. Leia and a white look by turns, at forty lengths of Leia.
SCENARIO(s12_leia_and_the_look_that_follows_it) {
  int bad = 0;
  for (int d = 0; d < 340; d += 34) bad += trial([d] {
    run(9); calmStock(); say("!HFM:v=show\r"); run(1);
    Photo p(0); p.strict = true;
    for (int i = 0; i < 12; i++) { say("!HFA:i=native:1\r"); watchFor(p, 1.1 + d / 1000.0 + i * 0.023); say("!HFA:i=solid,c=ffffff\r"); watchFor(p, 0.2); }
    std::string which; const int w = p.worstRegion(&which);
    if (w > 3) { printf("  Leia for %d ms more: %s lit %d times in a second\n", d, which.c_str(), w); failures++; }
    if (tooClose(p, "Leia and a white look by turns, Leia longer by (ms)", d)) failures++;
  });
  CHECK_EQ(bad, 0);
}
