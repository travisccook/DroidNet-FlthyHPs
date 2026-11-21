// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Contract v1.4 (core CA2): the three new ids reach the jewel through their own render slots
// (116, 117, 118) and draw this board's stand-ins until Task F4: logic is twinkle, projector is
// sparkle, vu is meter. None of them is an accent.
static void sparks(int hp, int& atTop, int& atBed) {   // the jewel's pixels (white) at its brightest level and at its dimmest
  const uint8_t* q = neoStrips[hp].shown; uint8_t hi = 0, lo = 255; atTop = atBed = 0;
  for (int i = 0; i < 7; i++) { if (q[i * 3] > hi) hi = q[i * 3]; if (q[i * 3] < lo) lo = q[i * 3]; }
  for (int i = 0; i < 7; i++) { if (q[i * 3] == hi) atTop++; else if (q[i * 3] == lo && lo > 0) atBed++; }
}
SCENARIO(v14_logic_draws_twinkle_and_vu_draws_meter) {
  run(8); line("!**M:v=show\r");
  say("!HFA:i=twinkle,c=ff8000,s=100\r!HRA:i=logic,c=ff8000,s=100\r"); run(1.0);
  CHECK(lookUp(0) && lookEffect(0) == CE_TWINKLE);
  CHECK(lookUp(1) && lookEffect(1) == CE_LOGIC);  CHECK_EQ(LED_command[1].LEDFunction, 116);
  int same = 0, lit = 0;
  for (int k = 0; k < 300; k++) {                    // 3 s: the two units draw the same frames
    pass(10000);
    if (!memcmp(neoStrips[0].shown, neoStrips[1].shown, 21)) same++;
    for (int i = 0; i < 21; i++) if (neoStrips[1].shown[i]) { lit++; break; }
  }
  CHECK_EQ(same, 300);  CHECK_EQ(lit, 300);
  say("!HFA:i=meter,c=00ff00\r!HRA:i=vu,c=00ff00\r!H*L:v=200\r"); run(1.0);
  CHECK(lookUp(0) && lookEffect(0) == CE_METER);
  CHECK(lookUp(1) && lookEffect(1) == CE_VU);  CHECK_EQ(LED_command[1].LEDFunction, 118);
  same = 0; lit = 0;
  for (int k = 0; k < 100; k++) {
    pass(10000);
    if (!memcmp(neoStrips[0].shown, neoStrips[1].shown, 21)) same++;
    for (int i = 0; i < 21; i++) if (neoStrips[1].shown[i]) { lit++; break; }
  }
  CHECK_EQ(same, 100);  CHECK_EQ(lit, 100);
}
SCENARIO(v14_projector_draws_sparkle) {
  run(8); line("!**M:v=show\r");
  line("!HTA:i=projector,c=ffffff,s=0\r", 1.0);       // s=0: two sparks, one moves every 477 ms
  CHECK(lookUp(2) && lookEffect(2) == CE_PROJECTOR);  CHECK_EQ(LED_command[2].LEDFunction, 117);
  int moves = 0, top = 0, bed = 0, wrong = 0; uint8_t last[21]; memcpy(last, neoStrips[2].shown, 21);
  for (int k = 0; k < 400; k++) {                    // 4 s: always two sparks, the other five at the bed
    pass(10000);
    sparks(2, top, bed);  if (top != 2 || bed != 5) wrong++;
    if (memcmp(last, neoStrips[2].shown, 21)) { moves++; memcpy(last, neoStrips[2].shown, 21); }
  }
  CHECK_EQ(wrong, 0);
  CHECK(moves >= 6 && moves <= 9);                   // 4 s at 477 ms a move
}
SCENARIO(v14_the_new_ids_are_no_accent) {
  run(8); line("!**M:v=show\r"); line("!H*A:i=solid,c=00ff00\r", 1.0);
  const char* p[] = {"!HFP:i=logic,c=ff0000,d=300\r", "!HRP:i=projector,c=ff0000,d=300\r", "!HTP:i=vu,c=ff0000,d=300\r"};
  for (int hp = 0; hp < 3; hp++) {
    line(p[hp], 0.1);                                // inside the 300 ms the overlay lasts
    CHECK(gUnit[hp].s.pulseActive);  CHECK_EQ(gUnit[hp].s.pulseFx, CE_SOLID);
  }
}
