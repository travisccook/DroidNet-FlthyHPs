// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Contract v1.4 (core CA2): the three new ids reach the jewel through their own render slots
// (116, 117, 118). logic draws this board's stand-in, twinkle; vu not pumped is the meter (N12).
// projector (Task F4) has its own scenarios in scenarios_r2.inc. None of them is an accent.
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
SCENARIO(v14_the_new_ids_are_no_accent) {
  run(8); line("!**M:v=show\r"); line("!H*A:i=solid,c=00ff00\r", 1.0);
  const char* p[] = {"!HFP:i=logic,c=ff0000,d=300\r", "!HRP:i=projector,c=ff0000,d=300\r", "!HTP:i=vu,c=ff0000,d=300\r"};
  for (int hp = 0; hp < 3; hp++) {
    line(p[hp], 0.1);                                // inside the 300 ms the overlay lasts
    CHECK(gUnit[hp].s.pulseActive);  CHECK_EQ(gUnit[hp].s.pulseFx, CE_SOLID);
  }
}
