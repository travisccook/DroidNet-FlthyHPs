// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Contract v1.4, release 2 (core CA2), Task F4: `projector` and `vu` on the jewel.
//   projector (spec v1.4 N6 to N9): the colour projector, paced. A step every 50 to 149 ms draws a
//     candidate slot of the colour's row for each pixel (the board's random()); a BRIGHTENING step
//     gives every pixel its candidate and needs 340 ms since the last one and since the unit's light
//     last went up (the core's levelMs, shared with L, B and b=); any other step CRUMBLES (a pixel
//     takes only a candidate that is no brighter, fxProjectorTake()). Its level is b=; the pump does
//     not reach it.
//   vu (N10 to N12): the meter, with the pump on its LEVEL (envBright(255, m, amount), offered to the
//     core's level hold every pass) and its brightness left at b=. Not pumped it is the meter.
// Each scenario is named for the plan's list (Task F4, "Scenarios, by name"). Everything here is a
// simulation of the real src/main.cpp; nothing of it has run on a board.

// ---- a long projector run, one pass a millisecond, measured at every pass and every latched frame ----
struct ProjRun {
  long steps = 0, brightenings = 0, crumbleBrighter = 0, framesBrighter = 0, held = 0, frames = 0;
  std::vector<uint32_t> stepAt, drawn, brightAt, riseFrameAt;   // pass times of the steps, the interval each drew, the brightening steps, the frames with a LED brighter
  long dice[10] = {0};
};
static int ledMax(const uint8_t* p, int i) { const int g = p[i * 3], r = p[i * 3 + 1], b = p[i * 3 + 2]; return std::max(r, std::max(g, b)); }
static int ledLum(const uint8_t* p, int i) { return contractLuminance(p[i * 3 + 1], p[i * 3], p[i * 3 + 2]); }
// `steps` projector steps of `look` on the front jewel at the owner's level 255 (the strip then scales
// nothing, so every pixel is a slot's colour exactly), the stock LED twitch off.
static ProjRun projectorRun(const char* look, long steps) {
  ProjRun pr;
  run(9); calmStock(); line("HFB255\r", 0.5);
  say("!HFM:v=show\r"); say(look); run(1.0);
  CHECK(lookUp(0) && lookEffect(0) == CE_PROJECTOR);  CHECK_EQ(LED_command[0].LEDFunction, 117);
  for (int k = 0; k < 10; k++) pr.dice[k] = -g_dice10[k];
  uint32_t nextWas = projNextStepMs(0), levelWas = levelWindowMs(0);
  long seen = shows(0); uint8_t prev[21]; memcpy(prev, neoStrips[0].shown, 21);
  while (pr.steps < steps) {
    const uint32_t t = millis(); const int waitedRise = projWaiting(0) == 2;
    pass(1000);
    pr.held += gLatch[0].pending;
    const bool stepped = projNextStepMs(0) != nextWas, brightened = levelWindowMs(0) != levelWas;
    nextWas = projNextStepMs(0); levelWas = levelWindowMs(0);
    if (stepped) { pr.steps++; pr.stepAt.push_back(t); pr.drawn.push_back(projNextStepMs(0) - t); }
    // a brightening step takes the window (when a frame that raised light lands in a pass, it takes the window there, and a step in that pass crumbles)
    if (stepped && levelWindowMs(0) == t && !waitedRise) { pr.brightenings++; pr.brightAt.push_back(t); }
    for (; seen < shows(0); seen++) {                      // every frame latched in this pass, in order
      const uint8_t* f = neoStrips[0].ring[seen % Adafruit_NeoPixel::RING];
      bool brighter = false;
      for (int i = 0; i < 7; i++) if (ledMax(f, i) > ledMax(prev, i) || ledLum(f, i) > ledLum(prev, i)) brighter = true;
      if (brighter) { pr.framesBrighter++; pr.riseFrameAt.push_back(t); if (!brightened) pr.crumbleBrighter++; }
      memcpy(prev, f, 21); pr.frames++;
    }
  }
  for (int k = 0; k < 10; k++) pr.dice[k] += g_dice10[k];
  return pr;
}
static uint32_t closestGap(const std::vector<uint32_t>& v) { uint32_t c = ~0u; for (size_t i = 1; i < v.size(); i++) c = std::min(c, v[i] - v[i - 1]); return c; }

SCENARIO(rel2_projector_slot_histogram_is_flat_over_20000_steps) {
  const ProjRun pr = projectorRun("!HFA:i=projector,c=116530\r", 20000);
  long total = 0; for (long d : pr.dice) total += d;
  CHECK_EQ(total, 7L * 20000);                             // seven candidates a step, from random(0, 10)
  const double mean = total / 10.0; double worst = 0;
  printf("  candidates drawn over %ld steps, slot 0..9:", pr.steps);
  for (long d : pr.dice) { printf(" %ld", d); worst = std::max(worst, fabs(d - mean) / mean); }
  printf("\n  the farthest slot from the mean: %.2f %% (limit 3 %%)\n", worst * 100);
  CHECK(worst <= 0.03);
}
// The interval each step draws is the board's dice (random(), 50 to 149 ms). The interval between two
// steps is that, unless a frame of the look waited at the latch meanwhile (no step is taken until it is
// up): in white the row has one colour and nothing waits; in 116530 a step that moves the field's
// colour waits for S4's 170 ms, as Leia's frames do, and the steps are further apart.
SCENARIO(rel2_projector_step_intervals_are_50_to_149_ms_with_a_mean_of_99_5) {
  for (const char* c : {"ffffff", "116530"}) {
    char l[64]; snprintf(l, sizeof l, "!HFA:i=projector,c=%s\r", c);
    const int bad = trial([l, c] {
      const ProjRun pr = projectorRun(l, 20000);
      uint32_t lo = ~0u, hi = 0, dlo = ~0u, dhi = 0; double sum = 0, dsum = 0;
      for (size_t i = 1; i < pr.stepAt.size(); i++) { const uint32_t d = pr.stepAt[i] - pr.stepAt[i - 1]; lo = std::min(lo, d); hi = std::max(hi, d); sum += d; }
      for (uint32_t d : pr.drawn) { dlo = std::min(dlo, d); dhi = std::max(dhi, d); dsum += d; }
      const double mean = sum / (pr.stepAt.size() - 1), dmean = dsum / pr.drawn.size();
      printf("  c=%s, %ld steps: drawn %u to %u ms, mean %.2f; between steps %u to %u ms, mean %.2f; passes with a frame waiting: %ld\n",
             c, pr.steps, dlo, dhi, dmean, lo, hi, mean, pr.held);
      CHECK(dlo >= 50);  CHECK(dhi <= 149);  CHECK(fabs(dmean - 99.5) <= 2.0);  CHECK(lo >= 50);
      if (!strcmp(c, "ffffff")) { CHECK(hi <= 149);  CHECK(fabs(mean - 99.5) <= 2.0);  CHECK_EQ(pr.held, 0); }
    });
    CHECK_EQ(bad, 0);
  }
}
SCENARIO(rel2_projector_no_led_gets_brighter_in_a_crumbling_step) {
  for (const char* c : {"116530", "ff0000", "3b82f6"}) {
    char l[64]; snprintf(l, sizeof l, "!HFA:i=projector,c=%s\r", c);
    const int bad = trial([l, c] {
      const ProjRun pr = projectorRun(l, 6000);
      printf("  c=%s: %ld steps, %ld brightening, %ld frames latched, %ld with a LED brighter, %ld of them not at a brightening step\n",
             c, pr.steps, pr.brightenings, pr.frames, pr.framesBrighter, pr.crumbleBrighter);
      CHECK_EQ(pr.crumbleBrighter, 0);  CHECK(pr.brightenings > 6000 / 5);   // it does brighten: about one step in four
    });
    CHECK_EQ(bad, 0);
  }
}
SCENARIO(rel2_projector_brightening_steps_are_340_ms_apart_or_more) {
  const ProjRun pr = projectorRun("!HFA:i=projector,c=116530\r", 20000);
  const uint32_t steps = closestGap(pr.brightAt), lit = closestGap(pr.riseFrameAt);
  double mean = 0; for (size_t i = 1; i < pr.brightAt.size(); i++) mean += pr.brightAt[i] - pr.brightAt[i - 1]; mean /= (pr.brightAt.size() - 1);
  printf("  %ld steps, %ld brightening: the closest two %u ms apart, %.0f ms on average; frames with a LED brighter: the closest two %u ms apart\n",
         pr.steps, pr.brightenings, steps, mean, lit);
  CHECK(steps >= 340);  CHECK(lit >= 340);
}
// The rows of the vector file (core CA2's contract_vectors.json, group fxProjectorSlot, slots 0..9), copied
// by hand: what the jewel shows at the owner's level 255 and b=255 must be exactly these colours.
static const struct { uint32_t c; uint32_t row[10]; } R2_ROWS[] = {
  {0x000000, {0x0000ff, 0x0000ff, 0x0000ff, 0xffffff, 0x5555ff, 0x8080ff, 0xaaaaff, 0, 0, 0}},   // no colour: Leia's blue
  {0xff0000, {0xff0000, 0xff0000, 0xff0000, 0xffffff, 0xff5555, 0xff8080, 0xffaaaa, 0, 0, 0}},
  {0x00ff00, {0x00ff00, 0x00ff00, 0x00ff00, 0xffffff, 0x55ff55, 0x80ff80, 0xaaffaa, 0, 0, 0}},
  {0x116530, {0x116530, 0x116530, 0x116530, 0xffffff, 0x609875, 0x88b297, 0xafcbba, 0, 0, 0}},
  {0x3b82f6, {0x3b82f6, 0x3b82f6, 0x3b82f6, 0xffffff, 0x7cabf9, 0x9dc0fa, 0xbdd5fc, 0, 0, 0}},
  {0x808080, {0x808080, 0x808080, 0x808080, 0xffffff, 0xaaaaaa, 0xbfbfbf, 0xd4d4d4, 0, 0, 0}},
};
SCENARIO(rel2_projector_ten_row_colours_for_six_c_values_match_the_vector_file) {
  for (const auto& row : R2_ROWS) {
    const int bad = trial([&row] {
      char l[64]; if (row.c) snprintf(l, sizeof l, "!HFA:i=projector,c=%06x\r", (unsigned)row.c); else snprintf(l, sizeof l, "!HFA:i=projector\r");
      run(9); calmStock(); line("HFB255\r", 0.5); say("!HFM:v=show\r"); say(l); run(1.0);
      std::set<uint32_t> want(row.row, row.row + 10), seen; long wrong = 0, slotWrong = 0;
      for (int k = 0; k < 60000; k++) {                    // 60 s
        pass(1000);
        for (int i = 0; i < 7; i++) {
          const RGB3 p = latched(0, i); const uint32_t v = ((uint32_t)p.r << 16) | ((uint32_t)p.g << 8) | (uint32_t)p.b;
          if (!want.count(v)) wrong++; else seen.insert(v);
          if (!gLatch[0].pending && v != row.row[projSlot(0, i)]) slotWrong++;   // the pixel shows its slot
        }
      }
      printf("  c=%06x: %zu of the row's %zu colours seen, %ld pixel-frames not in the row, %ld not their slot's\n", (unsigned)row.c, seen.size(), want.size(), wrong, slotWrong);
      CHECK_EQ(wrong, 0);  CHECK_EQ(slotWrong, 0);  CHECK(seen == want);
    });
    CHECK_EQ(bad, 0);
  }
}
// ---- the gate's limits on a part of the jewel, measured the way the S12 scenarios measure ----
struct R2Count { int whole, bands, fine; uint64_t closestUs; long lit; char region[24]; };
static R2Count r2Count(const std::function<void()>& setup, const std::function<void(int)>& everyMs, int periodMs, double secs = 20.0) {
  R2Count c = {99, 99, 99, 0, 0, ""}; int fd[2]; if (pipe(fd)) return c;
  fflush(stdout); pid_t pid = fork();
  if (pid == 0) {
    close(fd[0]); failures = 0; run(9); calmStock(); say("!HFM:v=show\r"); run(1); setup(); run(1.0);
    Photo p(0); p.strict = true; long lit = 0;
    for (int t = 0; t < (int)(secs * 1000); t++) { everyMs(t % periodMs); pass(1000); p.sample(); if (!dark(0)) lit++; }
    std::string w; c.bands = p.worstRegion(&w); snprintf(c.region, sizeof c.region, "%s", w.c_str());
    c.fine = p.worstFine(); c.whole = p.maxPerSecond(); c.closestUs = p.closestOnsetsUs(); c.lit = lit;
    (void)!write(fd[1], &c, sizeof c); _exit(0);
  }
  close(fd[1]); (void)!read(fd[0], &c, sizeof c); close(fd[0]); int st; waitpid(pid, &st, 0);
  return c;
}
static void r2Report(const char* what, const R2Count& c, double secs = 20.0) {
  printf("  %-58s whole %d, a part by the bands %d (%s), by the gate's count %d; closest onsets %.0f ms; lit %.0f %%\n", what, c.whole, c.bands,
         c.bands ? c.region : "-", c.fine, c.closestUs > 1e12 ? -1.0 : c.closestUs / 1000.0, c.lit / (secs * 10.0));
  CHECK(c.whole <= 3);  CHECK(c.bands <= 3);  CHECK(c.fine <= 3);  CHECK(c.closestUs >= 334000ULL);
}
SCENARIO(rel2_projector_b_and_B_raised_every_50_ms_light_no_part_more_than_three_times_a_second) {
  for (const char* c : {"ffffff", "116530"}) {
    char a[64], lo[48], hi[48]; snprintf(a, sizeof a, "!HFA:i=projector,c=%s\r", c);
    std::string look = a;
    r2Report((std::string("alone, c=") + c).c_str(), r2Count([look] { say(look.c_str()); }, [](int) {}, 1000));
    snprintf(lo, sizeof lo, "!HFA:b=0\r"); snprintf(hi, sizeof hi, "!HFA:b=255\r");
    r2Report((std::string("b=0 / b=255 every 50 ms, c=") + c).c_str(),
             r2Count([look] { say(look.c_str()); }, [](int t) { if (t == 0) say("!HFA:b=0\r"); if (t == 50) say("!HFA:b=255\r"); }, 100));
    r2Report((std::string("B:b=0 / B:b=255 every 50 ms, c=") + c).c_str(),
             r2Count([look] { say(look.c_str()); }, [](int t) { if (t == 0) say("!HFB:b=0\r"); if (t == 50) say("!HFB:b=255\r"); }, 100));
    r2Report((std::string("b=40 / b=255 every 50 ms, c=") + c).c_str(),
             r2Count([look] { say(look.c_str()); }, [](int t) { if (t == 0) say("!HFA:b=40\r"); if (t == 50) say("!HFA:b=255\r"); }, 100));
    r2Report((std::string("b= and B by turns, raised every 50 ms, c=") + c).c_str(),
             r2Count([look] { say(look.c_str()); }, [](int t) { if (t == 0) say("!HFB:b=0\r"); if (t == 50) say("!HFB:b=255\r"); if (t == 25) say("!HFA:b=0\r"); if (t == 75) say("!HFA:b=255\r"); }, 100));
  }
}
SCENARIO(rel2_projector_an_overlay_returns_to_the_same_slots) {
  // (a) the slots: an overlay every 340 ms for 20 s; at each overlay's end the base comes back with the
  // slots it had when the overlay began, and the first base frame latched shows them.
  // (Under a wipe of 300 ms the base is up for 40 ms between overlays, and its frame waits at the gate
  // and is replaced by the next overlay: there the slots are checked, and no base frame is expected.)
  for (const char* p : {"!HFP:c=ffffff,d=170\r", "!HFP:c=ffffff,b=0,d=170\r", "!HFP:c=ffffff,b=80,d=250\r", "!HFP:i=wipe,c=ff0000,d=300\r"}) {
    std::string pl = p; const bool shown = pl.find("wipe") == std::string::npos;
    const int bad = trial([pl, shown] {
      run(9); calmStock(); line("HFB255\r", 0.5); say("!HFM:v=show\r!HFA:i=projector,c=116530\r"); run(2.0);
      int slots[7] = {0}; bool wasUp = false, watching = false; long overlays = 0, same = 0, differ = 0, framesOk = 0, framesBad = 0; long seen = shows(0);
      for (int t = 0; t < 20000; t++) {
        if (t % 340 == 0) say(pl.c_str());
        pass(1000);
        const bool up = gUnit[0].s.pulseActive;
        if (up && !wasUp) { overlays++; for (int i = 0; i < 7; i++) slots[i] = projSlot(0, i); }
        if (!up && wasUp) { bool eq = true; for (int i = 0; i < 7; i++) if (projSlot(0, i) != slots[i]) eq = false; (eq ? same : differ)++;
          watching = true; seen = gLatch[0].pending ? shows(0) : shows(0) - 1; }   // the base frame: latched last in this pass, or the next one
        if (watching && !up && shows(0) > seen) {              // the first base frame latched after the overlay
          const uint8_t* f = neoStrips[0].ring[seen % Adafruit_NeoPixel::RING]; bool ok = true;
          for (int i = 0; i < 7; i++) { const uint32_t v = ((uint32_t)f[i * 3 + 1] << 16) | ((uint32_t)f[i * 3] << 8) | f[i * 3 + 2];
            if (v != R2_ROWS[3].row[slots[i]]) ok = false; }
          (ok ? framesOk : framesBad)++; watching = false;
        }
        wasUp = up;
      }
      printf("  %-36s %ld overlays: the same slots after %ld, others after %ld; the first base frame showed them %ld times, not %ld\n",
             pl.substr(0, pl.size() - 1).c_str(), overlays, same, differ, framesOk, framesBad);
      CHECK(overlays >= 50);  CHECK_EQ(differ, 0);  CHECK_EQ(framesBad, 0);  if (shown) CHECK(framesOk >= overlays - 2);
    });
    CHECK_EQ(bad, 0);
  }
  // (b) the limit (spec v1.4 section 9): an overlay every 340 ms lights no part of the jewel more than three times a second
  for (const char* p : {"!HFP:c=ffffff,d=170\r", "!HFP:c=ffffff,b=0,d=170\r", "!HFP:c=ffffff,b=40,d=170\r", "!HFP:i=wipe,c=ff0000,d=300\r"}) {
    std::string pl = p;
    r2Report(("projector under " + pl.substr(1, pl.size() - 2) + " every 340 ms").c_str(),
             r2Count([] { say("!HFA:i=projector,c=116530\r"); }, [pl](int t) { if (t == 0) say(pl.c_str()); }, 340));
  }
}
SCENARIO(rel2_projector_am_and_m_do_not_move_the_level) {
  // At the owner's level 255 and b=200 every lit pixel is a slot of the row at exactly 200/255, at every
  // tempo of the pump, with the beat clock running: the pump would put it lower.
  for (int bpm : {60, 120, 300}) {
    const int bad = trial([bpm] {
      run(9); calmStock(); line("HFB255\r", 0.5); say("!HFM:v=show\r!HFA:i=projector,c=116530,b=200,am=2,m=255\r"); run(0.5);
      char c[48]; snprintf(c, sizeof c, "!**C:bpm=%d,ph=0,bpb=4\r", bpm); say(c); run(1.0);
      CHECK(clockRunning());  CHECK_EQ(lookAm(0), 2);  CHECK_EQ(lookBeatMod(0), 255);   // kept (N8) ...
      std::set<uint32_t> want; for (uint32_t v : R2_ROWS[3].row) { const uint32_t r = ((v >> 16) & 255) * 200 / 255, g = ((v >> 8) & 255) * 200 / 255, b = (v & 255) * 200 / 255; want.insert((r << 16) | (g << 8) | b); }
      long wrong = 0, lit = 0;
      for (int k = 0; k < 20000; k++) { pass(1000);
        for (int i = 0; i < 7; i++) { const RGB3 p = latched(0, i); const uint32_t v = ((uint32_t)p.r << 16) | ((uint32_t)p.g << 8) | (uint32_t)p.b; if (!want.count(v)) wrong++; if (v) lit++; } }
      printf("  %3d BPM, am=2, m=255: %ld pixel-frames lit, %ld of them not at the look's level\n", bpm, lit, wrong);
      CHECK_EQ(wrong, 0);  CHECK(lit > 20000);                // ...and ignored
    });
    CHECK_EQ(bad, 0);
  }
}
SCENARIO(rel2_projector_and_vu_the_led_only_invariant_holds) {
  // All three jewels on the two looks for 90 s with the stock auto twitches on, a live line every 50 to
  // 400 ms. The pass checks (LED-only, the hold) run on every pass; here, no servo moves at all while held.
  run(9); line("!**M:v=show\r");
  const long m0[3] = {hpMoves(0), hpMoves(1), hpMoves(2)}; long n = 0;
  const char* L[] = {"!HFA:i=projector,c=ff0000,b=200\r", "!HRA:i=vu,c=00ff00,am=2,m=200\r", "!HTA:i=projector\r", "!**C:bpm=120,ph=0,bpb=4\r",
                     "!HFA:b=40\r", "!HFA:b=255\r", "!HRL:v=200\r", "!H*B:b=0\r", "!H*B:b=255\r", "!HTP:c=ffffff,d=200\r", "!HFA:i=projector,c=0000ff\r",
                     "!HRA:i=vu,c=ffffff,am=1,m=255\r", "!**C:bpm=300\r", "!HTA:i=vu,c=ffffff,am=2,m=255\r", "!HFA:i=projector,c=116530,s=7\r"};
  for (uint64_t end = us + 90000000ULL; us < end; ) { say(L[rand() % (sizeof L / sizeof *L)]); runRand(0.05 + (rand() % 350) / 1000.0, 1000, 4000); n++; }
  for (int hp = 0; hp < 3; hp++) { CHECK(armed(hp));  CHECK_EQ(hpMoves(hp), m0[hp]); }
  printf("  %ld lines over 90 s; servo moves: %ld %ld %ld\n", n, hpMoves(0) - m0[0], hpMoves(1) - m0[1], hpMoves(2) - m0[2]);
}
// ---- vu: the meter with the pump on its level ----
SCENARIO(rel2_vu_pumped_at_five_tempos) {
  // am=2, m=200: between beats the level is 255 - m = 55 and each beat the pump fires on takes it up (N10),
  // never more often than the level hold's 340 ms (N11); the brightness stays at b= (here the ceiling).
  for (int bpm : {30, 120, 176, 177, 300}) {
    char c[48]; snprintf(c, sizeof c, "!**C:bpm=%d,ph=0,bpb=4\r", bpm);
    std::string cl = c;
    const int bad = trial([bpm, cl] {
      run(9); calmStock(); say("!HFM:v=show\r!HFA:i=vu,c=ffffff,am=2,m=200\r"); say(cl.c_str()); run(2.0);
      int lo = 255, hi = 0, wasLevel = unitLevel(0); long rises = 0; uint32_t lastRise = 0, closest = ~0u;
      Photo p(0); p.strict = true;
      for (int k = 0; k < 20000; k++) { pass(1000); p.sample();
        const int lv = unitLevel(0); lo = std::min(lo, lv); hi = std::max(hi, lv);
        if (lv > wasLevel) { rises++; if (lastRise) closest = std::min(closest, (uint32_t)(millis() - lastRise)); lastRise = millis(); }
        wasLevel = lv; }
      std::string w; const int bands = p.worstRegion(&w), fine = p.worstFine(), whole = p.maxPerSecond();
      const double want = std::min(bpm / 60.0, 1000.0 / 340.0);   // a beat a second, at most one rise in 340 ms
      printf("  %3d BPM: the level %d..%d, %ld rises in 20 s (the closest %u ms apart); onsets a second: whole %d, a part %d by the bands, %d by the gate's count; brightness %d\n",
             bpm, lo, hi, rises, closest, whole, bands, fine, lookBright(0));
      CHECK_EQ(lo, 55);  CHECK(hi >= 200);  CHECK(closest >= 340);  CHECK(rises >= (long)(20 * want * 0.4));
      CHECK(whole <= 3);  CHECK(bands <= 3);  CHECK(fine <= 3);  CHECK(p.closestOnsetsUs() >= 334000ULL);  CHECK_EQ(lookBright(0), 255);
    });
    CHECK_EQ(bad, 0);
  }
}
SCENARIO(rel2_vu_pumped_under_a_seek_every_50_ms) {
  for (int bpm : {120, 300}) {
    char c[48]; snprintf(c, sizeof c, "!**C:bpm=%d,ph=0,bpb=4,beat=0\r", bpm); std::string cl = c;
    r2Report((std::string("vu am=2 m=255, a C seek every 50 ms at ") + std::to_string(bpm) + " BPM").c_str(),
             r2Count([cl] { say("!HFA:i=vu,c=ffffff,am=2,m=255\r"); say(cl.c_str()); },
                     [bpm](int t) { if (t % 50 == 0) { char l[48]; snprintf(l, sizeof l, "!**C:bpm=%d,beat=%d\r", bpm, (t / 50) % 4); say(l); } }, 200));
    r2Report((std::string("vu am=2 m=255, B 0 / 200 every 50 ms at ") + std::to_string(bpm) + " BPM").c_str(),
             r2Count([cl] { say("!HFA:i=vu,c=ffffff,am=2,m=255\r"); say(cl.c_str()); },
                     [](int t) { if (t == 0) say("!HFB:b=0\r"); if (t == 50) say("!HFB:b=200\r"); }, 100));
  }
}
SCENARIO(rel2_vu_an_l_during_the_pump_changes_nothing) {
  // Two jewels on the same pumped vu, one clock: L to the front only, every 25 ms. While the pump runs
  // the two latch the same frames (N12: an L that arrives while the pump runs is not shown). With the
  // clock held the pump stops, vu is the meter, and the front takes its L.
  run(9); calmStock(); say("!**M:v=show\r!HFA:i=vu,c=ffffff,am=2,m=200\r!HRA:i=vu,c=ffffff,am=2,m=200\r!**C:bpm=120,ph=0,bpb=4\r"); run(2.0);
  long same = 0, n = 0;
  for (int t = 0; t < 10000; t++) {
    if (t % 25 == 0) say((t / 25) % 2 ? "!HFL:v=0\r" : "!HFL:v=255\r");
    pass(1000); n++; if (!memcmp(neoStrips[0].shown, neoStrips[1].shown, 21) && unitLevel(0) == unitLevel(1)) same++;
  }
  printf("  pumped: the two jewels latched the same frame at %ld of %ld passes\n", same, n);
  CHECK_EQ(same, n);
  say("!**C:bpm=0\r"); run(1.0); say("!HFL:v=128\r!HRL:v=20\r"); run(1.0);
  CHECK(!clockRunning());  CHECK_EQ(unitLevel(0), 128);  CHECK_EQ(unitLevel(1), 20);   // not pumped: the meter, L shown
  CHECK(memcmp(neoStrips[0].shown, neoStrips[1].shown, 21) != 0);
}
// A KNOWN LIMIT (DROIDNET_FORK.md, Known limits, v1.4): two looks by turns on one part of the jewel. A
// brightening step lights a pair of pixels, a crumbling step darkens it, and the first overlay of a storm
// (a dim one, under a step of the whole field, so the field gate passes it) lights the pair again before
// 340 ms have passed; the storm's next overlays light it every 340 ms. By the gate's count of a part that
// is 4 in a second, once, at the storm's start (during a storm the jewel holds still, see _expireOverlay()).
// Here the case is built on purpose 300 times: the whole jewel and the bands are asserted; the gate's
// count is reported, and held to the 4 it is.
SCENARIO(rel2_projector_a_dim_overlay_storm_right_after_a_crumbling_step) {
  int worstF = 0, worstB = 0, worstW = 0, built = 0, fours = 0;
  for (int seed = 0; seed < 300; seed++) {
    int fd[2]; if (pipe(fd)) { failures++; return; } fflush(stdout); pid_t pid = fork();
    if (pid == 0) { close(fd[0]); srand(1000 + seed);
      run(9); calmStock(); say("!HFM:v=show\r"); run(1); say("!HFA:i=projector,c=116530\r"); run(2.0 + (seed % 7) * 0.05);
      uint32_t lv = levelWindowMs(0); int k = -1; uint32_t tb = 0; int r[3] = {-1, -1, -1}; Photo p(0); p.strict = true;
      for (int i = 0; i < 60000 && r[0] < 0; i++) { pass(1000); p.sample();
        if (levelWindowMs(0) != lv) { lv = levelWindowMs(0); tb = millis(); k = -1;           // a brightening step: which neighbours did it light?
          for (int q = 1; q <= 6; q++) if (projSlot(0, q) < 7 && projSlot(0, q % 6 + 1) < 7) { k = q; break; } }
        else if (k > 0 && millis() - tb < 200 && projSlot(0, k) >= 7 && projSlot(0, k % 6 + 1) >= 7) {   // ...and a crumbling step darkened both
          for (int t = 0; t < 2000; t++) { if (t % 340 == 0) say("!HFP:c=ffffff,b=40,d=170\r"); pass(1000); p.sample(); }
          r[0] = p.worstFine(); r[1] = p.worstRegion(); r[2] = p.maxPerSecond(); } }
      (void)!write(fd[1], r, sizeof r); _exit(0); }
    close(fd[1]); int r[3] = {-1, -1, -1}; (void)!read(fd[0], r, sizeof r); close(fd[0]); int st; waitpid(pid, &st, 0);
    if (r[0] < 0) continue;
    built++; fours += r[0] > 3; worstF = std::max(worstF, r[0]); worstB = std::max(worstB, r[1]); worstW = std::max(worstW, r[2]);
  }
  printf("  built %d times: the whole jewel at most %d onsets in a second, a part by the bands %d, by the gate's count %d (%d of the %d at 4)\n",
         built, worstW, worstB, worstF, fours, built);
  CHECK(built >= 250);  CHECK(worstW <= 3);  CHECK(worstB <= 3);  CHECK(worstF <= 4);
}
