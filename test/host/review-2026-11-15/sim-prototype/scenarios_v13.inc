// v1.3 behaviour that needs the shared core's R1 (contract_core.h). Each stays in xfail.txt
// until the plan task that adopts the core piece removes it.
static uint16_t crc16(const char* s) {               // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection
  uint16_t c = 0xFFFF;
  for (; *s; s++) { c ^= (uint16_t)((uint8_t)*s << 8); for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); }
  return c;
}
static uint16_t scored(const char* afterBang) {      // send "!<afterBang>\r"; return the CRC verb V folds in
  std::string l = std::string("!") + afterBang + "\r"; line(l.c_str());
  return crc16(afterBang);
}
static void verify(int n, uint16_t k) { char b[48]; snprintf(b, sizeof b, "!H*V:n=%d,k=%04x\r", n, k); line(b); }
static bool replied(const char* text) { bool hit = Serial.tx.find(text) != std::string::npos; Serial.tx.clear(); return hit; }

// ---- L2 / F5: the host-silence watchdog -----------------------------------------------------
SCENARIO(f5_watchdog_ends_an_orphaned_show) {
  run(600);
  line("!**M:v=show,wd=5\r"); line("!H*A:i=comet,c=ff0000,at=0,am=2,m=200,ae=solid\r");
  line("!**C:bpm=120,ph=0,bpb=4,beat=0\r");
  for (int k = 1; k <= 5; k++) {                     // 20 s of 4 s corrections: the keepalive
    run(3.8); char b[40]; snprintf(b, sizeof b, "!**C:ph=0,beat=%d\r", k * 8); line(b);
    CHECK(gInterlocked[0] && gInterlocked[1] && gInterlocked[2]);
  }
  long m = g_servoMoves;
  run(4.5); CHECK(gInterlocked[0]);                  // 4.7 s of silence: still armed
  run(1.0);                                          // 5.7 s: the board idles itself
  for (int hp = 0; hp < HPCOUNT; hp++) { CHECK(!gInterlocked[hp]); CHECK_EQ(LED_command[hp].LEDFunction, 100); }
  CHECK(!gBeat.running);  CHECK_EQ(g_servoMoves - m, 0);
  long t = twitches(); run(1800); CHECK(twitches() - t >= 15);
}
SCENARIO(f5_no_wd_means_no_watchdog) {               // a show stored in a WCB macro has no host to keep it alive
  run(6); line("!**M:v=show\r"); line("!H*A:i=solid,c=ff0000\r"); run(3600);
  CHECK(gInterlocked[0]);  CHECK(gUnit[0].active);
}

// ---- C1 / F6: a re-anchor mid-beat does not fire that beat's accent again ---------------------
SCENARIO(f6_reanchor_does_not_refire_the_accent) {
  run(6); line("!**M:v=show\r"); line("!HFA:i=comet,c=ff0000,at=0,am=2,m=200,ae=solid\r");
  line("!**C:bpm=60,ph=400,bpb=4,beat=0\r");
  int fires = 0, sent = 0; uint32_t last = gUnit[0].pulseStartMs; uint64_t t0 = us;
  while (us < t0 + 20200000ULL) {                    // about 20 beats at 60 BPM
    pass();
    if (gUnit[0].pulseStartMs != last) { fires++; last = gUnit[0].pulseStartMs; }
    BeatPos bp = beatPosAt(gBeat, millis());
    if (sent < 5 && bp.beatIndex == 3 + 4 * sent && bp.phase >= 0.6f && wireIdle()) {   // the same grid again, 600 ms into a beat
      char b[64]; snprintf(b, sizeof b, "!**C:ph=%d,beat=%d\r", (int)((1.0f - bp.phase) * 1000) - 26, (int)bp.beatIndex + 1);
      say(b); sent++;
    }
  }
  CHECK_EQ(sent, 5);  CHECK_EQ(fires, 20);           // one accent a beat, corrections or not
}

// ---- S1 / S2 / S4 / S5 ------------------------------------------------------------------------
SCENARIO(s1_bpm_outside_30_to_300_is_ignored) {
  run(6);
  line("!**C:bpm=20,ph=0\r");   CHECK(!gBeat.running);
  line("!**C:bpm=301,ph=0\r");  CHECK(!gBeat.running);
  line("!**C:bpm=120,ph=0\r");  CHECK(gBeat.running);  CHECK_EQ(gBeat.bpm, 120);
  line("!**C:bpm=9999,ph=0\r"); CHECK(gBeat.running);  CHECK_EQ(gBeat.bpm, 120);   // ignored whole: the clock keeps its state
  line("!**C:bpm=120,bpb=40\r"); CHECK_EQ(gBeat.bpb, 4);
}
SCENARIO(s2_pump_is_rate_limited_above_176_bpm) {
  run(6); line("!**M:v=show\r"); line("!**C:bpm=200,ph=0,bpb=4,beat=0\r");
  line("!HFA:i=solid,c=ffffff,m=255,am=2\r", 1.0);
  int peaks = 0, prev = -1, dir = 0; uint64_t t0 = us;
  while (us < t0 + 6000000ULL) { pass(1000); int v = latched(0).r;
    if (prev >= 0) { int d = v > prev ? 1 : v < prev ? -1 : dir; if (dir == 1 && d == -1) peaks++; dir = d; } prev = v; }
  printf("  pump at 200 BPM: %.2f swings a second\n", peaks / 6.0);
  CHECK(peaks / 6.0 <= 2.95);                        // every 2nd beat (600 ms), not every beat (300 ms)
}
SCENARIO(s4_live_looks_are_170_ms_apart) {
  run(6); line("!**M:v=show\r");
  say("!HFA:i=solid,c=ff0000\r"); run(0.06);
  say("!HFA:i=solid,c=00ff00\r"); run(0.06);
  say("!HFA:i=solid,c=0000ff\r"); run(0.03);         // three looks inside 150 ms
  CHECK(latched(0).r > 0 && latched(0).g == 0 && latched(0).b == 0);   // the first shows; the others are held
  run(0.3);
  CHECK(latched(0).b > 0 && latched(0).r == 0 && latched(0).g == 0);   // the newest held one wins
}
SCENARIO(s5_parameters_saturate) {
  run(6); line("!**M:v=show\r");
  line("!HFA:i=solid,c=ffffff,b=300,m=999,s=-4\r");
  CHECK_EQ(gUnit[0].brightBase, 255);  CHECK_EQ(gUnit[0].beatMod, 255);  CHECK_EQ(gUnit[0].speed, 0);
  line("!HFA:i=solid,c=ffffff,b=-1\r"); CHECK_EQ(gUnit[0].brightBase, 0);
  line("!HFM:v=shower\r"); CHECK(gUnit[0].active);   // not exactly "show": ignored, no reset
  line("!HFP:c=ff0000,d=99999\r"); CHECK_EQ(gUnit[0].pulseDurMs, 2550);
}

// ---- section 6 and 7: capacity, b= in a score, and load verification -------------------------
SCENARIO(score_holds_32_and_carries_b) {
  run(6); line("!**M:v=show\r"); uint16_t k = 0;
  for (int i = 0; i < 32; i++) { char b[64]; snprintf(b, sizeof b, "HFA:i=solid,c=ff0000,b=%d,at=%d", 64 + i, i * 4); k ^= scored(b); }
  CHECK_EQ(gScoreCount[0], 32);
  verify(32, k); line("!HFQ\r"); CHECK(replied("!HFq:ver=1.3,cap=32,n=32,ok=1,bpm=0"));
  line("!**C:bpm=120,ph=0,bpb=4,beat=0\r", 1.0); CHECK_EQ(gUnit[0].brightBase, 64);
  run(2.0); CHECK_EQ(gUnit[0].brightBase, 65);       // beat 4: the second entry's own b=
}
SCENARIO(score_overflow_is_a_bad_load) {
  run(6); line("!**M:v=show\r"); uint16_t k = 0;
  for (int i = 0; i < 33; i++) { char b[64]; snprintf(b, sizeof b, "HFA:i=solid,c=ff0000,at=%d", i * 4); k ^= scored(b); }
  verify(33, k);                                     // the numbers match, but the table overflowed
  CHECK(!gInterlocked[0]);  CHECK_EQ(LED_command[0].LEDFunction, 100);
  line("!HFQ\r"); CHECK(replied(",ok=2,"));
}
SCENARIO(v_good_load_plays_and_bad_load_idles) {
  run(6); line("!**M:v=show\r"); uint16_t k = 0;
  k ^= scored("H*A:i=comet,c=ff0000,at=0,am=2,m=200,ae=solid");
  k ^= scored("HFA:i=twinkle,c=00ff00,at=16");
  k ^= scored("HFA:i=native:1,at=32");
  line("!HFQ\r"); CHECK(replied("!HFq:ver=1.3,cap=32,n=3,ok=0,bpm=0"));   // no V yet
  line("!HRQ\r"); CHECK(replied("!HRq:ver=1.3,cap=32,n=1,ok=0,bpm=0"));   // the wildcard line counted on R too
  char b[48]; snprintf(b, sizeof b, "!HFV:n=3,k=%04x\r", k); line(b);
  CHECK(gInterlocked[0]);  line("!HFQ\r"); CHECK(replied(",n=3,ok=1,"));
  snprintf(b, sizeof b, "!HRV:n=2,k=%04x\r", crc16("H*A:i=comet,c=ff0000,at=0,am=2,m=200,ae=solid")); line(b);   // R got 1 of 2
  CHECK(!gInterlocked[1]);  CHECK_EQ(LED_command[1].LEDFunction, 100);      // back to its native look
  CHECK(gInterlocked[0]);                                                 // F is untouched
  line("!HRQ\r"); CHECK(replied(",ok=2,"));
  long m = g_servoMoves; run(0.3); CHECK_EQ(g_servoMoves - m, 0);
}
SCENARIO(q_reply_is_the_v13_line) {
  run(6); line("!**C:bpm=128,ph=0,bpb=4\r"); line("!HTQ\r");
  CHECK(replied("!HTq:ver=1.3,cap=32,n=0,ok=0,bpm=128\r\n"));
  line("!H*Q\r"); CHECK(Serial.tx.find("q:") == std::string::npos);       // never answer a broadcast Q
}
