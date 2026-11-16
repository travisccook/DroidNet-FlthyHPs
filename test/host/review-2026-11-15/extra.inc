  else if (sc == "replay2") {  // hand-placed interleaving: can a beheaded "!**X" reach HP_command at all?
    say("A105|5\r"); runFor(12.0, P);                      // operator: wag all HPs for 5 s (stock command), long finished
    say("S7\r"); runFor(0.0, P);                           // (not sent: placeholder to keep the wire empty)
    g_wire.clear(); g_wi = 0; Serial.rx.clear(); inputString = ""; stringComplete = false;
    long s0 = g_servoMoves; state("before; servo twitch may tick normally");
    enableTwitchHP[0] = enableTwitchHP[1] = enableTwitchHP[2] = false;  // isolate: no native auto twitch, so any move below is the replay
    const char* a = "!**C:bpm=120\r!**"; for (const char* p = a; *p; p++) Serial.rx.push_back(*p);
    serialEvent(); g_us += 3500; g_ms = g_us / 1000; loop();      // pass 1 read the C line's CR plus 3 bytes of the next line
    const char* b = "X\r"; for (const char* p = b; *p; p++) Serial.rx.push_back(*p);
    serialEvent(); g_us += 2000; g_ms = g_us / 1000; loop();
    printf("  after the beheaded X: HP_command[0].HPFunction=%d HP_command[1].HPFunction=%d typeState=%d functionState=%d\n", HP_command[0].HPFunction, HP_command[1].HPFunction, typeState, functionState);
    runFor(10.0, P); printf("  RESULT servo moveTo() calls in the 10 s after it: %ld\n", g_servoMoves - s0);
  } else if (sc == "native3b") {
    runFor(600, P); say("!**M:v=show\r"); runFor(0.2, P); say("!H*A:i=native:3,c=0000ff\r"); runFor(0.05, P);
    for (int i = 0; i < 3; i++) printf("  hp%d native:3 -> LEDOption1=%d LEDOption2=%d\n", i, LED_command[i].LEDOption1, LED_command[i].LEDOption2);
    for (int k = 0; k < 6; k++) { runFor(0.7, P); printf("  t+%.1fs latched F=(%d,%d,%d) R=(%d,%d,%d) T=(%d,%d,%d)\n", 0.7*(k+1), neoStrips[0].shown[0],neoStrips[0].shown[1],neoStrips[0].shown[2], neoStrips[1].shown[0],neoStrips[1].shown[1],neoStrips[1].shown[2], neoStrips[2].shown[0],neoStrips[2].shown[1],neoStrips[2].shown[2]); }
  } else if (sc == "showkeepsnative") {
    while (!(LED_command[1].LEDFunction >= 1 && LED_command[1].LEDFunction <= 7 && LED_command[1].LEDHalt > 6)) onePass(P);
    say("!**M:v=show\r"); runFor(1.0, P); long c = neoStrips[1].showCount; runFor(2.0, P); state("2 s after M:v=show, nothing else sent");
    printf("  RESULT rear jewel show() calls in show mode with no contract look: %ld\n", neoStrips[1].showCount - c);
  }
  else if (sc == "driftaccent") {   // does Studio's 4 s bare re-anchor re-fire the current beat's accent?
    say("!**M:v=show\r"); runFor(0.3, P); say("!HFA:i=comet,c=ff0000,at=0,am=2,m=200,ae=solid\r"); runFor(0.3, P);
    say("!**C:bpm=60,ph=400,bpb=4,beat=0\r"); runFor(0.2, P);
    long fires = 0; uint32_t last = gUnit[0].pulseStartMs; uint64_t t0 = g_us; int sent = 0;
    // 20 s at 60 bpm => 20 beats. Every 4 s send the SAME grid again as a bare re-anchor, 600 ms into a beat.
    while (g_us < t0 + 20200000ULL) { onePass(P);
      if (gUnit[0].pulseStartMs != last) { fires++; last = gUnit[0].pulseStartMs; }
      if (arg && sent < 5) { BeatPos bp = beatPosAt(gBeat, millis());
        if (bp.beatIndex == 3 + 4 * sent && bp.phase >= 0.6f && g_wi >= g_wire.size()) { char b[64];
          snprintf(b, sizeof b, "!**C:ph=%d,beat=%d\r", (int)((1.0f - bp.phase) * 1000) - 26, (int)bp.beatIndex + 1); say(b); sent++; } } }
    printf("  RESULT %s re-anchors: %ld accents fired in ~20 beats (re-anchors sent: %d)\n", arg ? "with" : "without", fires, sent);
  } else if (sc == "scorenativeaccent") {
    say("!**M:v=show\r"); runFor(0.3, P); say("!HFA:i=native:1,c=0000ff,at=0,am=2,m=200,ae=solid\r"); runFor(0.3, P);
    say("!**C:bpm=120,ph=1000,bpb=4,beat=0\r"); runFor(0.5, P); state("scored Leia section loaded, before beat 0");
    runFor(0.7, P); state("after beat 0");   runFor(3.0, P); state("3 s later");
    printf("  RESULT LEDFunction=%d (1 = Leia)\n", LED_command[0].LEDFunction);
  }
  else if (sc == "idlejerk") {
    runFor(20, P); say("!**M:v=show\r"); runFor(240, P); long s0 = g_servoMoves; state("4 min show, about to send idle");
    say("!**X\r"); runFor(0.2, P); say("!**M:v=idle\r"); uint64_t t0 = g_us; long first = -1;
    while (g_us < t0 + 300000ULL) { onePass(P); if (first < 0 && g_servoMoves > s0) first = (long)((g_us - t0) / 1000); }
    printf("  RESULT servo moveTo() calls within 300 ms of !**M:v=idle: %ld (first one %ld ms after the line was queued; 2 calls = one HP)\n", g_servoMoves - s0, first);
  }
