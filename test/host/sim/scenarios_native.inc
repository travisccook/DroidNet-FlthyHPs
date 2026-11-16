// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// Stock behaviour that must never change (v1.3 section 11: every native command still works).
SCENARIO(native_boot_auto_twitch) {
  run(10);                                           // the stock LED and servo twitches start about 4 s after boot
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK(LED_command[hp].LEDFunction >= 1 && LED_command[hp].LEDFunction <= 7);
  CHECK(g_servoMoves >= 12);                         // setup() centres 3 HPs (6 moveTo), the boot twitch moves them again
  run(600);
  CHECK(twitches() >= 9);                            // and they keep coming, 45-180 s apart
  CHECK(g_servoMoves >= 24);
}
SCENARIO(native_led_commands) {
  run(1);
  line("F0055\r");  CHECK_EQ(LED_command[0].LEDFunction, 5);  CHECK(latched(0) == (RGB3{0, 0, 100}));   // blue at BRIGHT 100
  line("A006|2\r"); for (int hp = 0; hp < HPCOUNT; hp++) CHECK_EQ(LED_command[hp].LEDFunction, 6);
  run(2.5);                                          // the |2 timer ends it: back to each HP's off colour {4,9,5}
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK_EQ(LED_command[hp].LEDFunction, 100);
  CHECK(latched(0) == (RGB3{0, 100, 100}));  CHECK(latched(1) == (RGB3{100, 100, 100}));  CHECK(latched(2) == (RGB3{0, 0, 100}));
  line("A096\r");   for (int hp = 0; hp < HPCOUNT; hp++) { CHECK_EQ(enableTwitchLED[hp], 0); CHECK(offcoloroverride[hp]); }
  line("R0053\n");  CHECK(latched(1) == (RGB3{0, 100, 0}));                                           // \n ends a line too
  line("T0051\r\n"); CHECK(latched(2) == (RGB3{100, 0, 0}));
}
SCENARIO(native_servo_commands) {
  run(1); long m = g_servoMoves;
  line("F1013\r");  CHECK_EQ(g_servoMoves - m, 2);   // front HP to position 3: its two servos
  m = g_servoMoves; line("A105|2\r"); run(3);        // wag all three for 2 s
  CHECK(g_servoMoves - m >= 12);
  for (int hp = 0; hp < HPCOUNT; hp++) CHECK_EQ(HP_command[hp].HPFunction, 255);
  line("A198\r");   for (int hp = 0; hp < HPCOUNT; hp++) CHECK(!enableTwitchHP[hp]);
  m = g_servoMoves; run(400); CHECK_EQ(g_servoMoves - m, 0);
  line("A199\r");   for (int hp = 0; hp < HPCOUNT; hp++) CHECK(enableTwitchHP[hp]);
}
SCENARIO(native_i2c_command) {
  run(1); i2c("F0055"); run(0.1);
  CHECK_EQ(LED_command[0].LEDFunction, 5);  CHECK(latched(0) == (RGB3{0, 0, 100}));
  long m = g_servoMoves; i2c("R1012"); run(0.1); CHECK_EQ(g_servoMoves - m, 2);
}
SCENARIO(native_brightness_command) {
  run(1); line("F0059\r"); CHECK(latched(0) == (RGB3{100, 100, 100}));
  line("HFB200\r"); CHECK_EQ(neoStrips[0].getBrightness(), 200); CHECK_EQ(neoStrips[1].getBrightness(), 100);
  run(0.1); CHECK(latched(0) == (RGB3{200, 200, 200}));
  line("HFB0\r");   CHECK_EQ(neoStrips[0].getBrightness(), 200);                                      // 0 is refused
}
