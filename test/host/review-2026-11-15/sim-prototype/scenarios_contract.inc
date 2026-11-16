// The contract layer, driven through the real intake and the real loop.
static int accentFires(int hp, double seconds) {     // how many overlays started on hp in this time
  int n = 0; uint32_t last = gUnit[hp].pulseStartMs; uint64_t end = us + (uint64_t)(seconds * 1e6);
  while (us < end) { pass(); if (gUnit[hp].pulseStartMs != last) { n++; last = gUnit[hp].pulseStartMs; } }
  return n;
}
SCENARIO(contract_live_look) {
  run(6);
  line("!HFA:i=solid,c=ff0000\r");
  CHECK(gUnit[0].active);  CHECK_EQ(LED_command[0].LEDFunction, 102);
  CHECK(latched(0).r > 0 && latched(0).g == 0 && latched(0).b == 0);
  CHECK(!gUnit[1].active && !gUnit[2].active);       // unit F only
  line("!H*A:i=comet,c=00ff00,s=200\r");
  for (int hp = 0; hp < HPCOUNT; hp++) { CHECK(gUnit[hp].active); CHECK_EQ(LED_command[hp].LEDFunction, 110); }
  line("!LFA:i=solid,c=ffffff\r");                   // another class: not this board
  CHECK_EQ(LED_command[0].LEDFunction, 110);
}
SCENARIO(contract_scored_show) {
  run(6);
  line("!**M:v=show\r");
  line("!HFA:i=solid,c=ff0000,at=0\r");  line("!HFA:i=solid,c=00ff00,at=4\r");
  CHECK_EQ(gScoreCount[0], 2);  CHECK(!gUnit[0].active);   // loaded, nothing playing yet
  line("!**C:bpm=120,ph=100,bpb=4,beat=0\r", 1.0);
  CHECK(latched(0).r > 0 && latched(0).g == 0);       // beat 0..3: red
  run(2.0);
  CHECK(latched(0).g > 0 && latched(0).r == 0);       // beat 4 on: green
  line("!**X\r");  CHECK(dark(0));  CHECK_EQ(gScoreCount[0], 0);  CHECK(!gBeat.running);
}
SCENARIO(contract_longest_line_lands_whole) {
  run(6); line("!**M:v=show\r");
  line("!H*A:i=colorcycle,c=3b82f6,at=1234,am=2,m=200,ae=colorcycle,ac=ffffff,ad=250\r", 0.5);
  CHECK_EQ(gScoreCount[2], 1);  CHECK_EQ(gScore[2][0].atBeat, 1234);  CHECK_EQ(gScore[2][0].accentDur10, 25);
}
SCENARIO(contract_led_only_spaced_lines) {           // every verb, one line at a time, shortly after boot
  run(6); long m = g_servoMoves;
  const char* lines[] = {"!**M:v=show\r", "!H*A:i=solid,c=ff0000\r", "!HFA:i=native:1\r", "!HRA:i=scan,c=00ff00\r",
    "!H*P:c=ffffff,d=200\r", "!**C:bpm=120,ph=0,bpb=4,beat=0\r", "!HTA:i=comet,c=0000ff,at=2,am=2,m=200,ae=solid\r",
    "!H*B:b=120\r", "!H*L:v=190\r", "!HFQ\r", "!**X\r", "!**M:v=idle\r", "!HFX\r"};
  for (const char* l : lines) line(l, 0.4);
  CHECK_EQ(g_servoMoves - m, 0);                     // nothing was due this soon after the boot twitch
}
