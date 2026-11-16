// One scenario per finding of the 2026-11-15 review. Each fails on the firmware as reviewed
// (test/host/sim/xfail.txt lists it) until the plan task that fixes it removes that line.
static void untilTwitchInFlight(int hp) {            // a stock auto twitch running on hp with > 6 s left
  while (!(LED_command[hp].LEDFunction >= 1 && LED_command[hp].LEDFunction <= 7 && LED_command[hp].LEDHalt > 6)) pass();
}

// ---- F1: back-to-back lines (the WCB writes a '^' batch with no gap) -------------------------
SCENARIO(f1_back_to_back_lines) {
  const uint64_t passes[] = {300, 700, 1000, 1500, 2000, 3000, 4000, 6000};
  for (uint64_t p : passes) {
    int bad = 0, n = 0;
    for (uint64_t phase = 0; phase < p && n < 40; phase += 97, n++)
      bad += trial([=] {
        run(1.0, p); us += phase;
        say("!**M:v=show\r!HFA:i=solid,c=ff0000\r!HRA:i=comet,c=00ff00\r!HTA:i=twinkle,c=0000ff\r"); run(1.0, p);
        CHECK(gInterlocked[0] && gInterlocked[1] && gInterlocked[2]);
        CHECK(gUnit[0].active && gUnit[0].effect == CE_SOLID);
        CHECK(gUnit[1].active && gUnit[1].effect == CE_COMET);
        CHECK(gUnit[2].active && gUnit[2].effect == CE_TWINKLE);
        say("!**X\r!**M:v=idle\r"); run(1.0, p);
        CHECK(!gInterlocked[0] && !gInterlocked[1] && !gInterlocked[2]);
      });
    printf("  loop pass %4llu us: %d of %d phases lost a line\n", (unsigned long long)p, bad, n);
    CHECK_EQ(bad, 0);
  }
}
SCENARIO(f1_crlf_and_blank_lines) {                  // \r\n and stray terminators cost nothing and lose nothing
  run(6);
  say("!**M:v=show\r\n!HFA:i=solid,c=ff0000\r\n\r\n\n!HRA:i=comet,c=00ff00\r\n"); run(1.0, 3000);
  CHECK(gInterlocked[0]);  CHECK(gUnit[0].active && gUnit[0].effect == CE_SOLID);  CHECK(gUnit[1].active && gUnit[1].effect == CE_COMET);
}
SCENARIO(r11_burst_of_400_bytes_loses_no_line) {     // spec section 11: 408 bytes, no gap, at 9600 baud
  const uint64_t passes[] = {300, 1000, 2000, 4000, 6000};
  for (uint64_t p : passes) {
    int bad = trial([=] {
      run(1.0, p); say("!**M:v=show\r"); run(0.5, p);
      std::string burst;
      for (int i = 0; i < 4; i++) for (const char* u : {"F", "R", "T"}) {
        char b[48]; snprintf(b, sizeof b, "!H%sA:i=solid,c=ff0000,at=%d,am=2\r", u, 100 + i * 8); burst += b;   // 34 bytes each
      }
      CHECK_EQ(burst.size(), 408);
      say(burst.c_str()); run(1.5, p);
      for (int hp = 0; hp < HPCOUNT; hp++) CHECK_EQ(gScoreCount[hp], 4);
      CHECK_EQ(rxOverruns, 0);
    });
    printf("  loop pass %4llu us: %s\n", (unsigned long long)p, bad ? "LOST A LINE" : "all 12 lines landed");
    CHECK_EQ(bad, 0);
  }
}
SCENARIO(r11_longest_v13_line_lands_whole) {         // 102 characters (core plan, amendment 16)
  const char* l = "!H*A:i=colorcycle,c=3b82f6,b=255,s=128,at=123456,am=2,an=16,ap=15,m=200,ae=colorcycle,ac=ffffff,ad=250";
  CHECK_EQ(strlen(l), 102);
  run(6); line("!**M:v=show\r"); say(l); say("\r"); run(0.5);
  CHECK_EQ(gScoreCount[2], 1);  CHECK_EQ(gScore[2][0].atBeat, 123456);  CHECK_EQ(gScore[2][0].accentDur10, 25);
}
SCENARIO(f1_beheaded_tail_cannot_reach_the_servos) { // the hand-placed interleaving from the review
  ledOnlyChecked = false;                            // loop() is driven by hand here; the checks are explicit
  line("A105|5\r", 12.0);                            // the operator wagged all three HPs for 5 s, long finished
  enableTwitchHP[0] = enableTwitchHP[1] = enableTwitchHP[2] = false;   // so any move below is not a stock twitch
  long m = g_servoMoves;
  for (const char* p = "!**C:bpm=120\r!**"; *p; p++) Serial.rx.push_back(*p);
  serialEvent(); us += 3500; g_ms = (uint32_t)(us / 1000); loop();
  for (const char* p = "X\r"; *p; p++) Serial.rx.push_back(*p);
  serialEvent(); us += 2000; g_ms = (uint32_t)(us / 1000); loop();
  for (int k = 0; k < 5000; k++) { if (Serial.available()) serialEvent(); us += 2000; g_ms = (uint32_t)(us / 1000); loop(); }
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK_EQ(HP_command[hp].HPFunction, 255);
  CHECK_EQ(g_servoMoves - m, 0);
}
SCENARIO(f1_led_only_fuzz) {                         // no '!' traffic, in any timing, starts a move or writes HP_command
  const char* T[] = {"!**M:v=show\r", "!**M:v=idle\r", "!**X\r", "!HFX\r", "!H*X\r", "!HRM:v=idle\r", "!HTM:v=show\r",
    "!H*A:i=solid,c=ff0000\r", "!HFA:i=comet,c=00ff00,s=200,m=200,am=2\r", "!HRA:i=native:1\r", "!HTA:i=scan,c=0000ff\r",
    "!H*A:i=twinkle,c=3b82f6,at=8,am=2,m=200,ae=solid\r", "!HFA:i=pulse,c=ffffff,d=400\r", "!H*P:c=ffffff,d=200\r",
    "!HFP:i=flash,c=ff0000\r", "!**C:bpm=120,ph=0,bpb=4,beat=0\r", "!**C:ph=120,beat=9\r", "!H*B:b=90\r", "!H*L:v=200\r", "!HRQ\r"};
  const int NT = sizeof(T) / sizeof(T[0]);
  for (int round = 0; round < 6; round++) {
    line("A105|2\r", 4.0);                           // a stock servo command stays the last one the stock parser saw
    for (int k = 0; k < 400; k++) {
      int burst = 1 + rand() % 4; std::string b;
      for (int j = 0; j < burst; j++) b += T[rand() % NT];
      say(b.c_str());
      uint64_t end = us + (uint64_t)(200000 + rand() % 600000);
      while (us < end || !wireIdle()) pass((uint64_t)(300 + rand() % 5700));
    }
    run(200);                                        // long enough for both twitch timers to be overdue
    say(round % 2 ? "!**M:v=idle\r" : "!**X\r!**M:v=idle\r"); run(1.0, 3000);
  }
  printf("  %ld LED-only violation(s) over 2400 bursts\n", ledOnlyViolations);
}

// ---- F2: M:v=idle gives the board back, and moves nothing while doing it ---------------------
SCENARIO(f2_idle_gives_the_leds_back) {
  run(600); line("!**M:v=show\r"); line("!H*A:i=solid,c=ff0000\r", 10.0);
  line("!**X\r"); line("!**M:v=idle\r", 0.5);
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK_EQ(LED_command[hp].LEDFunction, 100);   // each HP's off colour, as after a stock flush
  CHECK(latched(0) == (RGB3{0, 100, 100}));  CHECK(latched(1) == (RGB3{100, 100, 100}));  CHECK(latched(2) == (RGB3{0, 0, 100}));
  long t = twitches(); run(1800);
  CHECK(twitches() - t >= 15);                       // the stock LED auto twitch is alive again (about 45 in 30 min)
}
SCENARIO(f2_idle_without_x_leaves_nothing_frozen) {
  run(6); line("!**M:v=show\r"); line("!HFA:i=solid,c=ff0000\r", 2.0); line("!**M:v=idle\r", 0.5);
  CHECK_EQ(LED_command[0].LEDFunction, 100);  CHECK(latched(0) == (RGB3{0, 100, 100}));
}
SCENARIO(f2_idle_moves_no_servo) {
  run(20); line("!**M:v=show\r", 240.0);             // a 4-minute show: both twitch timers are long overdue
  long m = g_servoMoves;
  line("!**X\r"); line("!**M:v=idle\r", 0.3);
  CHECK_EQ(g_servoMoves - m, 0);                     // not in that pass, not in the next 300 ms
  run(44); CHECK_EQ(g_servoMoves - m, 0);            // the timers were re-armed a full interval (>= 45 s) from idle
  run(200); CHECK(g_servoMoves - m >= 6);            // and then the stock servo twitch is back
}
SCENARIO(f2_stray_idle_touches_nothing) {            // an idle with no show before it: the operator's look stays
  run(6); line("F0055\r"); line("!**M:v=idle\r", 0.5);
  CHECK_EQ(LED_command[0].LEDFunction, 5);  CHECK(latched(0) == (RGB3{0, 0, 100}));
}
SCENARIO(f2_x_outside_a_show_keeps_the_auto_twitch) {
  run(600); line("!HFA:i=solid,c=ff0000\r", 300.0);
  long m = g_servoMoves; line("!HFX\r", 0.3);
  CHECK(dark(0));  CHECK_EQ(LED_command[0].LEDFunction, 255);  CHECK_EQ(g_servoMoves - m, 0);
  long t = ledTwitchStarts[0]; run(1800); CHECK(ledTwitchStarts[0] - t >= 5);
}

// ---- F3 / S7: the native hatch is an allow-list, and an accent never replaces a native look --
SCENARIO(f3_native_allow_list) {
  run(6); line("!**M:v=show\r"); line("!HFA:i=solid,c=ff0000\r");
  const char* refused[] = {"0", "2", "3", "4", "5", "7", "96", "97", "98", "99", "100", "110", "257", "-1"};
  for (const char* n : refused) {
    std::string l = std::string("!HFA:i=native:") + n + ",c=0000ff\r"; line(l.c_str());
    CHECK_EQ(LED_command[0].LEDFunction, 102);       // ignored whole: the red solid is still there
    CHECK(gUnit[0].active);
  }
  line("!HFA:i=native:1\r");  CHECK_EQ(LED_command[0].LEDFunction, 1);    // Leia
  line("!HFA:i=native:6\r");  CHECK_EQ(LED_command[0].LEDFunction, 6);    // rainbow
  line("!HFA:i=native:3,at=4\r");  CHECK_EQ(gScoreCount[0], 0);          // refused in a score too
}
SCENARIO(f3_no_accent_replaces_a_native_look) {
  run(6); line("!**M:v=show\r"); line("!HFA:i=comet,c=00ff00\r", 1.0); line("!HFA:i=native:1,c=0000ff\r", 1.0);
  line("!HFP:c=ffffff,d=180\r", 1.0);
  CHECK_EQ(LED_command[0].LEDFunction, 1);           // still Leia after a live accent
  line("!**M:v=show\r");
  line("!HRA:i=native:1,c=0000ff,at=0,am=2,m=200,ae=solid\r");
  line("!**C:bpm=120,ph=300,bpb=4,beat=0\r", 4.0);
  CHECK_EQ(LED_command[1].LEDFunction, 1);           // and after 7 beats of a scored ae=
}

// ---- F4: a stock timer or look in flight when the contract takes the jewel -------------------
SCENARIO(f4_stale_halt_does_not_kill_a_look) {
  untilTwitchInFlight(0); run(1.0);
  line("!**M:v=show\r"); line("!HFA:i=solid,c=ff0000\r"); run(40);
  CHECK_EQ(LED_command[0].LEDFunction, 102);  CHECK(latched(0).r > 0);
  // and outside a show: a live look over a twitch in flight
  line("!**X\r"); line("!**M:v=idle\r");
  untilTwitchInFlight(1); run(1.0);
  line("!HRA:i=solid,c=00ff00\r"); run(40);
  CHECK_EQ(LED_command[1].LEDFunction, 102);  CHECK(latched(1).g > 0);
}
SCENARIO(l1_show_begin_takes_the_leds) {             // armed: the stock look stays. First C, live A or P: dark.
  untilTwitchInFlight(1);
  line("!**M:v=show\r", 0.5);
  long c = shows(1); run(1.0);
  CHECK(shows(1) - c > 0);  CHECK(LED_command[1].LEDFunction >= 1 && LED_command[1].LEDFunction <= 7);
  line("!**C:bpm=120,ph=0,bpb=4\r", 0.5);             // Play
  c = shows(1); run(2.0);
  CHECK_EQ(shows(1) - c, 0);  CHECK(dark(1));        // baseline dark means dark
  run(40); CHECK(dark(1));                           // and the stock look's timer is gone with it
}
SCENARIO(l1_armed_show_keeps_the_native_idle_look) { // between Send and Play the dome is not dark
  run(600); line("!**M:v=show\r");
  long m = g_servoMoves, t = twitches(); run(900);
  CHECK(twitches() - t >= 6);                        // the stock LED twitch carries on
  CHECK_EQ(g_servoMoves - m, 0);                     // the servo twitch is off from the moment of arming
}
SCENARIO(l1_idle_after_an_armed_show_that_never_began) {
  run(20); line("F0055\r"); line("!**M:v=show\r", 240.0);
  CHECK_EQ(LED_command[0].LEDFunction, 5);  CHECK(latched(0) == (RGB3{0, 0, 100}));   // the operator's look is still there
  long m = g_servoMoves; line("!**M:v=idle\r", 0.3);
  CHECK_EQ(g_servoMoves - m, 0);                     // the overdue servo twitch does not fire in that pass
  CHECK_EQ(LED_command[0].LEDFunction, 5);  CHECK(latched(0) == (RGB3{0, 0, 100}));
  run(200); CHECK(g_servoMoves - m >= 4);            // and the stock servo twitch is back afterwards
}
SCENARIO(f4_a_stock_command_takes_the_jewel_back) {  // outside a show the operator's command wins, and the hold ends
  run(600); line("!HFA:i=solid,c=ff0000\r", 300.0);
  long m = g_servoMoves; line("F0055|2\r", 0.3);
  CHECK(!gUnit[0].active);  CHECK_EQ(g_servoMoves - m, 0);
  long t = ledTwitchStarts[0]; run(1800); CHECK(ledTwitchStarts[0] - t >= 5);
}

// ---- F7: the operator's settings are the operator's ------------------------------------------
SCENARIO(f7_operator_settings_survive_a_show) {
  run(6); line("!**M:v=show\r", 1.0);
  line("A198\r", 1.0);                               // the operator turns the servo twitch off DURING the show
  line("!**M:v=idle\r", 1.0);
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK(!enableTwitchHP[hp]);
  line("A199\r"); line("A096\r", 1.0);               // LED twitch off and off colour overridden BEFORE the next show
  line("!**M:v=show\r", 1.0); line("!**M:v=idle\r", 1.0);
  for (int hp = 0; hp < HPCOUNT; hp++) { CHECK_EQ(enableTwitchLED[hp], 0); CHECK(offcoloroverride[hp]); CHECK(enableTwitchHP[hp]); }
}
SCENARIO(f7_no_twitch_of_either_kind_in_a_show) {
  run(6); line("!**M:v=show\r"); line("!**C:bpm=120,ph=0,bpb=4\r");
  line("A199\r");                                    // even with the operator's servo twitch switched on mid-show
  long m = g_servoMoves, t = twitches(); run(900);
  CHECK_EQ(g_servoMoves - m, 0);  CHECK_EQ(twitches() - t, 0);
}

// ---- F8 / S6: brightness ------------------------------------------------------------------
SCENARIO(s6_full_brightness_is_the_owners_level) {
  run(6); line("F0059\r"); RGB3 stockWhite = latched(0);                 // stock white at BRIGHT 100
  line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff,b=255\r");
  CHECK(latched(0) == stockWhite);                   // b=255 is exactly the owner's level, not 78 % of it
  line("!HFA:i=solid,c=ffffff\r"); CHECK(latched(0) == stockWhite);     // and so is the default
  line("!HFA:i=solid,c=ffffff,b=128\r"); CHECK(latched(0).r < stockWhite.r && latched(0).r > stockWhite.r / 3);
}
SCENARIO(f8_verb_b) {
  run(6); line("!HFB:b=10\r"); CHECK_EQ(neoStrips[0].getBrightness(), 100);   // no show armed: ignored
  line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r"); RGB3 full = latched(0);
  line("!HFB:b=128\r"); CHECK(latched(0).r > full.r * 2 / 5 && latched(0).r < full.r * 3 / 5);   // a held solid follows at once
  line("!HFB:b=0\r");   CHECK(dark(0));
  line("!HFB:b=255\r"); CHECK(latched(0) == full);   // and comes back exact: a repaint, not the lossy rescale
  line("!HFB:b=20\r");  line("!**X\r"); line("!**M:v=idle\r");
  CHECK_EQ(neoStrips[0].getBrightness(), 100);       // the ride does not outlive the show
  line("F0055\r"); CHECK(latched(0) == (RGB3{0, 0, 100}));
}
SCENARIO(s6_owner_brightness_during_a_show_sticks) {
  run(6); line("!**M:v=show\r"); line("!HFA:i=solid,c=ffffff\r"); line("!HFB:b=128\r");
  line("HFB050\r"); CHECK_EQ(neoStrips[0].getBrightness(), 50);   // the stock brightness command, mid-show
  CHECK(latched(0) == (RGB3{50, 50, 50}));           // the new ceiling, and the ride is over
  line("!**M:v=idle\r"); CHECK_EQ(neoStrips[0].getBrightness(), 50);
}

// ---- F9 / S8 / L4: pulse rate, and d= on a look is retired -------------------------------------
static double pulseHz(int s) {
  char b[64]; snprintf(b, sizeof b, "!HFA:i=pulse,c=ffffff,s=%d\r", s); line(b, 0.5);
  int peaks = 0, prev = -1, dir = 0; uint64_t t0 = us;
  while (us < t0 + 6000000ULL) { pass(1000); int v = latched(0).r;
    if (prev >= 0) { int d = v > prev ? 1 : v < prev ? -1 : dir; if (dir == 1 && d == -1) peaks++; dir = d; } prev = v; }
  return peaks / 6.0;
}
SCENARIO(f9_pulse_obeys_the_rate_floor) {
  run(6); line("!**M:v=show\r");
  double fast = pulseHz(255), mid = pulseHz(128);
  printf("  pulse s=255: %.2f Hz, s=128: %.2f Hz\n", fast, mid);
  CHECK(fast <= 3.0);  CHECK(mid > 0.9 && mid < 1.1);   // only s >= 242 changed
}
SCENARIO(l4_d_on_a_look_is_ignored) {                // v1.3 hosts never send it; a v1.3 board ignores it
  run(6); line("!**M:v=show\r");
  line("!HFA:i=solid,c=ff0000,d=500\r", 1.5);
  CHECK(gUnit[0].active);  CHECK(latched(0).r > 0);
  line("!HFA:i=solid,c=00ff00\r", 1.5);
  CHECK(gUnit[0].active);  CHECK(latched(0).g > 0);
}

// ---- L3: a pulse with no show armed overlays the stock look and returns to it ----------------
SCENARIO(l3_pulse_over_the_stock_look) {
  run(6); line("F0055\r");                           // the operator's solid blue
  say("!HFP:c=ff0000,d=400\r"); run(0.25);
  CHECK(latched(0).r > 0 && latched(0).b == 0);      // the overlay is up
  run(1.0);
  CHECK_EQ(LED_command[0].LEDFunction, 5);  CHECK(latched(0) == (RGB3{0, 0, 100}));
  line("A006|1\r", 2.0);                             // back to the off-colour hold; pulse over that
  say("!HRP:c=ff0000,d=400\r"); run(1.5);
  CHECK_EQ(LED_command[1].LEDFunction, 100);  CHECK(latched(1) == (RGB3{100, 100, 100}));
  line("T0075\r", 30.0); RGB3 held = latched(2, 3);  // a finished Short Circuit holds its last frame
  say("!HTP:c=00ff00,d=400\r"); run(1.5);
  CHECK_EQ(LED_command[2].LEDFunction, 7);  CHECK(latched(2, 3) == held);
}

// ---- intake: an I2C command no longer wipes a serial line in flight --------------------------
SCENARIO(i2c_does_not_wipe_a_serial_line) {
  run(6); say("!HFA:i=solid,c=ff0000\r");
  while (wi < 10) pass(500);                         // half of the serial line has arrived
  i2c("R0053");                                      // a stock command lands on I2C right now
  run(0.5);
  CHECK(gUnit[0].active && gUnit[0].effect == CE_SOLID);             // the serial line survived
  CHECK_EQ(LED_command[1].LEDFunction, 5);  CHECK(latched(1) == (RGB3{0, 100, 0}));   // and the I2C command ran
}
