// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// REVIEW FINDINGS, release 2 (an independent review of 86b5ec0..63993d2, Task F3 and F4 on the R2 line).
// Every scenario in this file FAILS on 63993d2 on purpose: each one records a defect the review found,
// and passes once the layer is fixed. They are not in xfail.txt, so stage 2b reports them as FAIL.
// Nothing here edits the layer or the core. Named review_r2_f<n>_..., n as in the review's report.
//
//   f1  projector + a storm of dim overlays that begins just after a brightening step that MOVED the
//       light (the whole field's level did not rise, so the field gate's onset time did not move): the
//       step lights a pair and every return of the base after an overlay lights it again. A part of the
//       jewel 4 times in a second BY THE BANDS (the wide observers), not only by the gate's count. Known
//       limit 1 of release 2 says the bands read 3; its scenario builds the case in 116530 only, where the
//       row's colour slots sit under the bands' "lit" lines. In white and in red the bands read 4.
//   f2  the 340 ms hold after an overlay (DROIDNET_FORK.md, v1.4 reading 4, _expireOverlay()) is skipped
//       when a live A ends the overlay: _setLook() clears pulseActive itself, the projector steps at once,
//       and a storm of overlays each followed by the same A lights a part 4 times a second by the gate's
//       count all through the storm, not once at its start.
//   f3  a change of colour alone repaints the projector's slots at once, with no look at the 340 ms window
//       a brightening step needs (N9: "no pixel gets brighter except at a brightening step or a raise of
//       the level, and those share one 340 ms window"). A brighter colour of the same hue every 340 ms puts
//       frames with a LED brighter 170 ms apart, and a part 4 times a second by the gate's count.
//   f4  (low; a question on N12/C6) vu held by C:bpm=0 freezes the level the pump had at that moment, so a
//       Pause shows the meter anywhere between 255 - m and full depending on when it landed. Every other
//       pumped look shows its own level under a hold (c6_hold_shows_a_pumped_look_at_its_own_level).

// One brightening step of the projector (the core's levelMs moves) that does not move the field gate's
// onset time (the step moved light between pixels; the whole field did not rise), then a storm of `pl`
// every 340 ms from the very next pass. The photometer runs from the look's start, so the step's own
// onset is counted. r: the gate's count, the bands, the whole jewel; -1 when no such step came.
static void r2rBuiltStorm(const char* look, const char* pl, int seed, int r[3]) {
  r[0] = r[1] = r[2] = -1;
  int fd[2]; if (pipe(fd)) return; fflush(stdout); pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]); srand(3000 + seed); int o[3] = {-1, -1, -1};
    run(9); calmStock(); say("!HFM:v=show\r"); run(1); say(look); run(2.0 + (seed % 7) * 0.05);
    uint32_t lv = levelWindowMs(0); Photo p(0); p.strict = true;
    for (int i = 0; i < 60000 && o[0] < 0; i++) {
      pass(1000); p.sample();
      if (levelWindowMs(0) == lv) continue;
      lv = levelWindowMs(0);
      if (gUnit[0].s.fieldOnsetMs == lv || gLatch[0].pending) continue;   // a step that raised the field: the gate times it
      for (int t = 0; t < 2000; t++) { if (t % 340 == 0) say(pl); pass(1000); p.sample(); }
      o[0] = p.worstFine(); o[1] = p.worstRegion(); o[2] = p.maxPerSecond();
    }
    (void)!write(fd[1], o, sizeof o); _exit(0);
  }
  close(fd[1]); (void)!read(fd[0], r, 3 * sizeof(int)); close(fd[0]); int st; waitpid(pid, &st, 0);
}
SCENARIO(review_r2_f1_projector_dim_overlay_storm_after_a_brightening_step_lights_a_part_4_times_by_the_bands) {
  for (const char* c : {"ffffff", "ff0000", "116530"}) {
    char look[48]; snprintf(look, sizeof look, "!HFA:i=projector,c=%s\r", c);
    int built = 0, bandsOver = 0, fineOver = 0, worstB = 0, worstF = 0, worstW = 0;
    for (int seed = 0; seed < 60; seed++) {
      int r[3]; r2rBuiltStorm(look, "!HFP:c=ffffff,b=40,d=170\r", seed, r);
      if (r[0] < 0) continue;
      built++; bandsOver += r[1] > 3; fineOver += r[0] > 3;
      worstF = std::max(worstF, r[0]); worstB = std::max(worstB, r[1]); worstW = std::max(worstW, r[2]);
    }
    printf("  c=%s, P:c=ffffff,b=40,d=170 every 340 ms from the pass after the step, built %d times: whole %d; a part by the bands %d (%d builds over 3); by the gate's count %d (%d over 3)\n",
           c, built, worstW, worstB, bandsOver, worstF, fineOver);
    CHECK(built >= 50);  CHECK(worstW <= 3);
    CHECK(worstB <= 3);                                    // FAILS in white and in red (63993d2: 4, in about half the builds)
  }
}
// f2. (a) White-box: the hold _expireOverlay() gives the projector after an overlay ends. (b) What it costs.
SCENARIO(review_r2_f2_an_a_that_ends_an_overlay_skips_the_projectors_340_ms_hold) {
  {
    const int bad = trial([] {
      run(9); calmStock(); say("!HFM:v=show\r!HFA:i=projector,c=ffffff\r"); run(2.0);
      say("!HFP:c=ffffff,b=40,d=170\r"); while (!gUnit[0].s.pulseActive) pass(1000);   // an overlay that expires by itself
      while (gUnit[0].s.pulseActive) pass(1000);
      const uint32_t endedA = millis(); pass(1000);
      const long holdA = (long)(int32_t)(projNextStepMs(0) - endedA);
      run(1.0);
      say("!HFP:c=ffffff,b=40,d=170\r"); while (!gUnit[0].s.pulseActive) pass(1000);
      run(0.02); say("!HFA:i=projector,c=ffffff\r");          // the same look again: it ends the overlay (S11: no restart)
      while (gUnit[0].s.pulseActive) pass(1000);
      const uint32_t endedB = millis(); pass(1000);
      const long holdB = (long)(int32_t)(projNextStepMs(0) - endedB);
      printf("  the next step after the overlay ended: %ld ms after it when it expired, %ld ms after it when an A ended it\n", holdA, holdB);
      CHECK(holdA >= 339);
      CHECK(holdB >= 339);                                   // FAILS: the step is due at once (negative: overdue, taken when its frame is up)
    });
    CHECK_EQ(bad, 0);
  }
  for (int withA = 0; withA < 2; withA++) {
    int runs = 0, over = 0, worstF = 0, worstW = 0;
    for (int seed = 0; seed < 40; seed++) {
      int fd[2]; if (pipe(fd)) { failures++; return; } fflush(stdout); pid_t pid = fork();
      if (pid == 0) {
        close(fd[0]); srand(4000 + seed);
        run(9); calmStock(); say("!HFM:v=show\r"); run(1); say("!HFA:i=projector,c=ffffff\r"); run(1.5 + (rand() % 1000) / 1000.0);
        Photo p(0); p.strict = true;
        for (int t = 0; t < 6000; t++) { if (t % 340 == 0) say("!HFP:c=ffffff,b=40,d=170\r"); if (withA && t % 340 == 10) say("!HFA:i=projector,c=ffffff\r"); pass(1000); p.sample(); }
        int r[2] = {p.worstFine(), p.maxPerSecond()}; (void)!write(fd[1], r, sizeof r); _exit(0);
      }
      close(fd[1]); int r[2] = {-1, -1}; (void)!read(fd[0], r, sizeof r); close(fd[0]); int st; waitpid(pid, &st, 0);
      if (r[0] < 0) continue;
      runs++; over += r[0] > 3; worstF = std::max(worstF, r[0]); worstW = std::max(worstW, r[1]);
    }
    printf("  6 s of P:c=ffffff,b=40,d=170 every 340 ms%s, %d runs: whole %d; a part by the gate's count %d (%d runs over 3)\n",
           withA ? " and the same projector A 10 ms after each" : " (no A)", runs, worstW, worstF, over);
    CHECK(runs >= 38);  CHECK(worstW <= 3);
    CHECK(worstF <= 3);                                      // FAILS with the A (63993d2: 4 in about 30 of 40); passes without it
  }
}
// f3. A brighter colour of the same hue every 340 ms (the two colours by turns every 170 ms, S4's own
// spacing). At the owner's level 255, as rel2_projector_brightening_steps_are_340_ms_apart_or_more measures.
SCENARIO(review_r2_f3_a_change_of_colour_lights_pixels_outside_the_brightening_window) {
  {
    const int bad = trial([] {
      run(9); calmStock(); line("HFB255\r", 0.5); say("!HFM:v=show\r!HFA:i=projector,c=200000\r"); run(1.0);
      long seen = shows(0); uint8_t prev[21]; memcpy(prev, neoStrips[0].shown, 21); std::vector<uint32_t> rise;
      for (int t = 0; t < 20000; t++) {
        if (t % 170 == 0) say((t / 170) % 2 ? "!HFA:i=projector,c=ff0000\r" : "!HFA:i=projector,c=200000\r");
        pass(1000);
        for (; seen < shows(0); seen++) {
          const uint8_t* f = neoStrips[0].ring[seen % Adafruit_NeoPixel::RING]; bool br = false;
          for (int i = 0; i < 7; i++) if (ledMax(f, i) > ledMax(prev, i) || ledLum(f, i) > ledLum(prev, i)) br = true;
          if (br) rise.push_back(neoStrips[0].ringMs[seen % Adafruit_NeoPixel::RING]);
          memcpy(prev, f, 21);
        }
      }
      long under = 0; for (size_t i = 1; i < rise.size(); i++) under += rise[i] - rise[i - 1] < 340;
      printf("  c=200000 and c=ff0000 by turns every 170 ms, 20 s: %zu frames with a LED brighter, the closest two %u ms apart, %ld pairs under 340 ms\n",
             rise.size(), closestGap(rise), under);
      CHECK(rise.size() > 20);
      CHECK(closestGap(rise) >= 340);                        // FAILS: 170
    });
    CHECK_EQ(bad, 0);
  }
  int runs = 0, over = 0, worstF = 0, worstW = 0;
  for (int seed = 0; seed < 40; seed++) {
    int fd[2]; if (pipe(fd)) { failures++; return; } fflush(stdout); pid_t pid = fork();
    if (pid == 0) {
      close(fd[0]); srand(4000 + seed);
      run(9); calmStock(); say("!HFM:v=show\r"); run(1); say("!HFA:i=projector,c=200000\r"); run(1.5 + (rand() % 1000) / 1000.0);
      Photo p(0); p.strict = true;
      for (int t = 0; t < 6000; t++) { if (t % 170 == 0) say((t / 170) % 2 ? "!HFA:i=projector,c=ff0000\r" : "!HFA:i=projector,c=200000\r"); pass(1000); p.sample(); }
      int r[2] = {p.worstFine(), p.maxPerSecond()}; (void)!write(fd[1], r, sizeof r); _exit(0);
    }
    close(fd[1]); int r[2] = {-1, -1}; (void)!read(fd[0], r, sizeof r); close(fd[0]); int st; waitpid(pid, &st, 0);
    if (r[0] < 0) continue;
    runs++; over += r[0] > 3; worstF = std::max(worstF, r[0]); worstW = std::max(worstW, r[1]);
  }
  printf("  the same at the boot level, 40 runs of 6 s: whole %d; a part by the gate's count %d (%d runs over 3)\n", worstW, worstF, over);
  CHECK(runs >= 38);  CHECK(worstW <= 3);
  CHECK(worstF <= 3);                                        // FAILS: 4 in a few runs
}
// f4. Pause (C:bpm=0) lands once just after a beat and once between beats. A pumped solid shows its own
// level both times (c6_hold_shows_a_pumped_look_at_its_own_level); a pumped vu shows whatever the pump had.
SCENARIO(review_r2_f4_vu_held_by_pause_shows_the_level_the_pump_had_at_that_moment) {
  int level[2] = {-1, -1};
  for (int k = 0; k < 2; k++) {
    int fd[2]; if (pipe(fd)) { failures++; return; } fflush(stdout); pid_t pid = fork();
    if (pid == 0) {
      close(fd[0]);
      run(9); calmStock(); say("!HFM:v=show\r!HFA:i=vu,c=ffffff,am=2,m=200\r!**C:bpm=60,ph=0,bpb=4\r"); run(3.0);
      while (clockPos().phase16 > 3000) pass(1000);            // the top of a beat ...
      if (k == 1) while (clockPos().phase16 < 39000) pass(1000);   // ... or six tenths into it
      say("!**C:bpm=0\r"); run(2.0);
      int v = clockRunning() ? -2 : unitLevel(0); (void)!write(fd[1], &v, sizeof v); _exit(0);
    }
    close(fd[1]); (void)!read(fd[0], &level[k], sizeof level[k]); close(fd[0]); int st; waitpid(pid, &st, 0);
  }
  printf("  vu am=2,m=200 at 60 BPM, then C:bpm=0: the level held is %d when Pause lands on a beat, %d when it lands between beats\n", level[0], level[1]);
  CHECK(level[0] >= 0);  CHECK(level[1] >= 0);
  CHECK_EQ(level[0], level[1]);                              // FAILS: the meter freezes wherever the pump was
}
