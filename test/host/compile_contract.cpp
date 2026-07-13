// Host check of the firmware layer ContractFlthy.h against mock_flthy.h.
// Mostly a TYPE-CHECK — it proves the firmware C++ compiles with the verified board
// API signatures (catches typos / wrong arity / type errors before the bench). Every
// fork entry point is exercised so the compiler instantiates and type-checks it, and
// the main.cpp '!' dispatch body is replicated verbatim. Plus a focused behavioral
// guard that _entryFrom threads a scored NATIVE section's native code into the
// ScoreEntry (the drift the 2026-07-12 fork audit fixed — nativeCode stayed -1).
#include "mock_flthy.h"
#include "../../src/contract/ContractFlthy.h"
#include <cstdio>
#include <cstring>

int main() {
  // parse + fan-out
  ParsedContract p;
  contractParse("HFA:i=solid,c=0080FF,d=0,b=255,m=180", p);
  applyContract(p);
  applyContractToUnit(0, p);
  applyContractToUnit(1, p);
  applyContractToUnit(2, p);

  // exercise every verb
  { ParsedContract q; if (contractParse("H*A:i=rainbow", q))            applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=pulse,c=FFFFFF", q))     applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=scan,c=00FF00", q))      applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=sparkle,c=FF00FF", q))   applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=meter,c=00FF00", q))     applyContract(q); }
  { ParsedContract q; if (contractParse("HFA:i=native:6", q))           applyContract(q); }
  { ParsedContract q; if (contractParse("H*A:i=comet,c=3B82F6,s=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*A:i=chase,c=3B82F6,s=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*A:i=wipe,c=3B82F6,s=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*A:i=gradient,s=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*A:i=colorcycle,s=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*A:i=twinkle,c=3B82F6,s=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("HTA:i=solid,c=112233,at=44,am=1", q)) applyContract(q); }  // Phase-2 schedule
  { ParsedContract q; if (contractParse("HFP:c=FFFFFF,d=120,b=200", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("**C:bpm=128,ph=40,bpb=4,beat=44", q)) applyContract(q); }
  { ParsedContract q; if (contractParse("H*B:v=180", q))                applyContract(q); }
  { ParsedContract q; if (contractParse("H*L:v=190,band=mid", q))       applyContract(q); }
  { ParsedContract q; if (contractParse("HFQ", q))                      applyContract(q); }
  { ParsedContract q; if (contractParse("HFM:v=show", q))               applyContract(q); }
  { ParsedContract q; if (contractParse("HFM:v=idle", q))               applyContract(q); }
  { ParsedContract q; if (contractParse("**X", q))                      applyContract(q); }

  // beat tick + per-hp render (cases 101..115 route here)
  contractBeatTick();
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) contractRenderHP(hp);

  // replicate the main.cpp '!' branch body verbatim to type-check it
  {
    char inputBuffer[80] = "!HFA:i=flash,c=FF0000,s=200";
    if (inputBuffer[0] == '!') contractHandle(&inputBuffer[1]);
  }

  // behavioral guard: _entryFrom must thread the parsed native code into the
  // ScoreEntry (regression guard for the nativeCode-stayed-(-1) drift the audit fixed;
  // without it a scored native section fell through to an unhandled LEDFunction=109).
  { ParsedContract q;
    if (!contractParse("HFA:i=native:3,at=44", q)) { printf("FAIL: Flthy parse\n"); return 1; }
    ScoreEntry e = _entryFrom(q.params);
    if (e.nativeCode != 3 || e.effect != CE_NATIVE) {
      printf("FAIL: Flthy _entryFrom did not thread nativeCode (got %d)\n", e.nativeCode);
      return 1;
    }
  }

  // I4 guard (PHYSICAL): the contract must NEVER force-enable the native HP SERVO twitch.
  // enableTwitchHP[] moves a holoprojector servo; this fork is LED-ONLY (§11) and the idle
  // branch used to write a literal `true`, silently re-enabling servo motion the operator
  // had turned off (in the sketch config or via native servo cmd 98) until the next reboot.
  // Contract: show may take the flag away, idle may only put back EXACTLY what show took,
  // and an idle that never followed a show must not touch it at all.
  {
    ParsedContract q;
    enableTwitchHP[0] = false;                    // operator has the FRONT servo twitch OFF
    if (contractParse("HFM:v=idle", q)) applyContract(q);
    if (enableTwitchHP[0]) { printf("FAIL: Flthy idle force-enabled the HP servo twitch (no prior show)\n"); return 1; }

    if (contractParse("HFM:v=show", q)) applyContract(q);
    if (enableTwitchHP[0]) { printf("FAIL: Flthy show must disable the HP servo twitch (LED-only)\n"); return 1; }
    if (contractParse("HFM:v=idle", q)) applyContract(q);
    if (enableTwitchHP[0]) { printf("FAIL: Flthy idle re-enabled a servo twitch the operator disabled\n"); return 1; }

    enableTwitchHP[1] = true;                     // operator has the REAR servo twitch ON
    if (contractParse("HRM:v=show", q)) applyContract(q);
    if (enableTwitchHP[1]) { printf("FAIL: Flthy show must disable the HP servo twitch (LED-only)\n"); return 1; }
    if (contractParse("HRM:v=idle", q)) applyContract(q);
    if (!enableTwitchHP[1]) { printf("FAIL: Flthy idle did not restore the operator's HP servo twitch\n"); return 1; }
  }

  // Minor guard: show forces offcoloroverride[] on (so the off state is truly black); idle
  // must hand the native off-color behavior back. It used to stay overridden after every
  // show until the operator sent a native 98/99 command.
  {
    ParsedContract q;
    offcoloroverride[2] = false;                  // native off-color ACTIVE on the top HP
    if (contractParse("HTM:v=show", q)) applyContract(q);
    if (!offcoloroverride[2]) { printf("FAIL: Flthy show must override the off color (truly black)\n"); return 1; }
    if (contractParse("HTM:v=idle", q)) applyContract(q);
    if (offcoloroverride[2]) { printf("FAIL: Flthy idle left the native off color overridden\n"); return 1; }

    offcoloroverride[2] = true;                   // operator had it overridden already (native 96/97)
    if (contractParse("HTM:v=show", q)) applyContract(q);
    if (contractParse("HTM:v=idle", q)) applyContract(q);
    if (!offcoloroverride[2]) { printf("FAIL: Flthy idle clobbered an operator-set off-color override\n"); return 1; }
    offcoloroverride[2] = false;
  }

  // I3 guard: ending a show must CLEAR the Phase-2 score. Without scoreClear(),
  // gScoreCount/gScoreActive survived verb X — and because a later at= insert resets
  // gScoreActive[hp] to -1, the very next contractBeatTick() re-applied the OLD table's
  // section and RESURRECTED the previous show on a jewel that had just been blacked out.
  // Semantics: !HFX clears that HP; !**X clears all three units plus the beat clock;
  // M:v=idle (the other way a show ends) clears too.
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);                            // clean slate
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);  // clock at beat 0 (millis()==0)
    if (contractParse("HFA:i=flash,c=FF0000,at=0", q)) applyContract(q);      // show 1, section at beat 0
    contractBeatTick();
    if (!gUnit[0].active || gUnit[0].effect != CE_FLASH) { printf("FAIL: Flthy scored section never applied\n"); return 1; }

    if (contractParse("HFX", q)) applyContract(q);                            // per-unit STOP
    if (gUnit[0].active)   { printf("FAIL: Flthy STOP left the unit active\n"); return 1; }
    if (gScoreCount[0] != 0 || gScoreActive[0] != -1) { printf("FAIL: Flthy STOP left the Phase-2 score armed\n"); return 1; }

    // show 2 arrives: its at= insert resets the active cursor, so a table that was never
    // cleared re-applies show 1's beat-0 section on the next tick (the resurrection path).
    if (contractParse("HFA:i=solid,c=00FF00,at=8", q)) applyContract(q);      // show 2 starts at beat 8
    contractBeatTick();                                                        // still beat 0 => nothing to play
    if (gUnit[0].active) { printf("FAIL: Flthy beat tick resurrected the stopped show from a stale score\n"); return 1; }

    // !**X: every unit's score cleared, plus the beat clock
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("H*A:i=solid,c=00FF00,at=0", q)) applyContract(q);
    contractBeatTick();
    if (contractParse("**X", q)) applyContract(q);
    for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
      if (gScoreCount[hp] != 0 || gScoreActive[hp] != -1 || gUnit[hp].active) {
        printf("FAIL: Flthy !**X did not clear HP %u\n", (unsigned)hp); return 1;
      }
    }
    if (gBeat.running) { printf("FAIL: Flthy !**X did not stop the beat clock\n"); return 1; }

    // M:v=idle also ends a show: its sections must not survive it
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HRA:i=flash,c=FF0000,at=0", q)) applyContract(q);
    contractBeatTick();
    if (!gUnit[1].active) { printf("FAIL: Flthy scored section never applied (idle case)\n"); return 1; }
    if (contractParse("HRM:v=idle", q)) applyContract(q);
    if (gScoreCount[1] != 0 || gScoreActive[1] != -1) { printf("FAIL: Flthy idle left the Phase-2 score armed\n"); return 1; }
    if (contractParse("HRA:i=solid,c=00FF00,at=8", q)) applyContract(q);
    contractBeatTick();
    if (gUnit[1].active) { printf("FAIL: Flthy beat tick resurrected a show after idle\n"); return 1; }
    if (contractParse("**X", q)) applyContract(q);                            // leave a clean slate
  }

  // I5 guard (BEHAVIORAL): an am=3 ("build") section must actually RAMP across its span.
  // _envBright() passed a hardcoded sectionProgress = 1.0f to beatAccentAmount(), so every
  // build frame returned FULL accent: the jewel sat pinned at the b= ceiling for the whole
  // section instead of swelling into the next one. The span now comes from the active score
  // entry (gUnit[hp].sectionStart/.sectionEnd, set in contractBeatTick()).
  // Score two 16-beat sections at 120 BPM (500 ms/beat) and read the LATCHED pixel a hair
  // after a downbeat 1/4 and 3/4 of the way through the first one: same barPos, same phase,
  // so section progress is the ONLY thing that differs. With c=0000FF the blue channel of
  // the shown pixel IS the envelope brightness (_scale: 255*envB/255).
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);                       // clean slate
    uint32_t t0 = _mock_millis;                                          // clock anchors here
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HFA:i=solid,c=0000FF,at=0,am=3,m=255", q))  applyContract(q);
    if (contractParse("HFA:i=solid,c=00FF00,at=16,am=3,m=255", q)) applyContract(q);

    auto renderedBright = [&](int beat) -> uint8_t {
      _mock_millis = t0 + (uint32_t)beat * 500u + 10u;                   // just past the downbeat
      contractBeatTick();                                                // section switch + span
      contractRenderHP(0);
      return (uint8_t)(neoStrips[0].shown[0] & 0xFFu);                   // blue == envelope level
    };
    uint8_t early = renderedBright(4);    // beat  4 of [0,16) -> section progress 0.25
    uint8_t late  = renderedBright(12);   // beat 12 of [0,16) -> section progress 0.75
    uint8_t ceil_ = gUnit[0].brightBase;  // b= ceiling: full accent renders exactly here

    if (early >= ceil_ && late >= ceil_) {
      printf("FAIL: am=3 BUILD section is pinned at FULL accent for its whole span "
             "(early=%u late=%u, b=%u) — sectionProgress is not being derived, so the "
             "build never ramps\n", (unsigned)early, (unsigned)late, (unsigned)ceil_);
      return 1;
    }
    if (early == 0 && late == 0) {
      printf("FAIL: am=3 BUILD section is parked at the envelope floor (early=%u late=%u) "
             "— sectionProgress is stuck at 0\n", (unsigned)early, (unsigned)late);
      return 1;
    }
    if (late < early + 24) {
      printf("FAIL: am=3 BUILD does not ramp across the section (early=%u late=%u) — late "
             "must be meaningfully brighter than early\n", (unsigned)early, (unsigned)late);
      return 1;
    }
    _mock_millis = t0;
    if (contractParse("**X", q)) applyContract(q);                       // leave a clean slate
  }

  // Minor guard: the verb-Q ack must echo the unit that answered. It hardcoded 'f', so
  // !HRQ and !HTQ both replied "!Hfq:..." and a host could not tell which HP responded.
  {
    ParsedContract q;
    const char* want[3] = {"!HFq:", "!HRq:", "!HTq:"};
    const char* cmd[3]  = {"HFQ", "HRQ", "HTQ"};
    for (int i = 0; i < 3; i++) {
      Serial.clear();
      if (contractParse(cmd[i], q)) applyContract(q);
      if (strncmp(Serial.last, want[i], 5) != 0) {
        printf("FAIL: Flthy %s ack did not echo its unit (wanted \"%s...\", got \"%s\")\n",
               cmd[i], want[i], Serial.last);
        return 1;
      }
    }
    Serial.clear();                                    // broadcast Q must stay silent (§8)
    if (contractParse("H*Q", q)) applyContract(q);
    if (Serial.last[0] != 0) { printf("FAIL: Flthy answered a broadcast Q (\"%s\")\n", Serial.last); return 1; }
  }

  printf("ContractFlthy.h type-check + score-native / servo-twitch / score-clear / "
         "build-ramp / Q-unit guards OK\n");
  return 0;
}
