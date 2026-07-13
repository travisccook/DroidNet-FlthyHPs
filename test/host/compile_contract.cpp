// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT (see LICENSE-DroidNet-Contract)
// The firmware this harness type-checks against is Ryan Sondgeroth's work; it carries
// no license and is all rights reserved by him, and that MIT license covers our files
// only. See the NOTICE in README.md.
//
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
    char inputBuffer[INPUTBUFFERLEN] = "!HFA:i=flash,c=FF0000,s=200";
    if (inputBuffer[0] == '!') contractHandle(&inputBuffer[1]);
  }

  // WIRE BUDGET: main.cpp's line buffer TRUNCATES SILENTLY (inputString.toCharArray into
  // inputBuffer[INPUTBUFFERLEN], main.cpp:801) — a line one byte too long is not an error, it
  // just loses its tail and the board plays a mangled cue. contract v1.2 adds the accent
  // overlay keys (ae=/ac=/ad=) to an already-long scored line, so pin the worst case Studio
  // can emit against the buffer that has to survive it.
  {
    const char* worst = "!H*A:i=colorcycle,c=3b82f6,at=1234,am=2,m=200,ae=colorcycle,ac=ffffff,ad=250";
    if ((int)strlen(worst) + 1 > INPUTBUFFERLEN) {
      printf("FAIL: the longest v1.2 scored line (%d chars) does not fit INPUTBUFFERLEN=%d — "
             "main.cpp would truncate it SILENTLY\n", (int)strlen(worst), INPUTBUFFERLEN);
      return 1;
    }
    ParsedContract q;                                  // ...and it must still parse in full
    if (!contractParse(worst + 1, q) || !q.params.hasAccentFx ||
        q.params.accentFx != CE_COLORCYCLE || !q.params.hasAt || q.params.atBeat != 1234) {
      printf("FAIL: the longest v1.2 scored line did not parse intact\n");
      return 1;
    }
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

  // ======================================================================================
  // A1 guard (BEHAVIORAL, contract v1.2): the BOARD-SIDE ACCENT SELF-FIRE.
  // Before v1.2 a DELIVERED blueprint's only beat expression was the brightness envelope —
  // it could not fire the effect-swap accent that a LIVE, Pi-mirrored show got from verb P,
  // so the two paths did not look the same. Now contractBeatTick() fires the same overlay
  // itself, off the active score entry's ae=/ac=/ad=, with the Pi silent.
  //
  // Three units, one beat clock (120 BPM => 500 ms/beat, 4/4), driven beat by beat:
  //   HP0  am=1 + ae=flash + ac=FF0000  -> accents DOWNBEATS ONLY, in red over a blue base
  //   HP1  am=2 + ae=pulse (no ac=)     -> accents EVERY beat, inheriting the section colour
  //   HP2  am=2, NO ae= (a v1.1 entry)  -> must NEVER arm the overlay, on any beat
  // The mid-beat re-tick is the load-bearing one: it lands 390 ms after the accent, i.e. PAST
  // the 340 ms strobe cool-down, so only the once-per-beat EDGE can stop a re-fire there.
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);
    _mock_servoTouches = 0;                       // A2 (below) audits this whole run
    boolean twitchBefore[HPCOUNT] = {enableTwitchHP[0], enableTwitchHP[1], enableTwitchHP[2]};

    _mock_millis = 100000;                        // non-zero origin: pulseLastMs == 0 must keep
    uint32_t t0 = _mock_millis;                   // meaning "this unit has never accented"
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HFA:i=solid,c=0000FF,at=0,am=1,m=0,ae=flash,ac=FF0000,ad=200", q)) applyContract(q);
    if (contractParse("HRA:i=solid,c=00FF00,at=0,am=2,m=0,ae=pulse", q)) applyContract(q);
    if (contractParse("HTA:i=solid,c=00FF00,at=0,am=2,m=200", q)) applyContract(q);

    uint32_t startBefore[HPCOUNT]; bool activeBefore[HPCOUNT];
    auto tick = [&](uint32_t ms) {
      _mock_millis = ms;
      for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
        startBefore[hp]  = gUnit[hp].pulseStartMs;
        activeBefore[hp] = gUnit[hp].pulseActive;
      }
      contractBeatTick();
    };
    // an accent FIRED on this tick == the overlay is armed AND its start time is new
    auto fired = [&](uint8_t hp) {
      return gUnit[hp].pulseActive &&
             (!activeBefore[hp] || gUnit[hp].pulseStartMs != startBefore[hp]);
    };

    tick(t0 + 10);                                                     // beat 0, barPos 0
    if (!fired(0)) { printf("FAIL: a scored ae= did not fire an accent on the downbeat — a "
                            "delivered blueprint has NO effect accent at all\n"); return 1; }
    if (!fired(1)) { printf("FAIL: an am=2 scored accent did not fire on beat 0\n"); return 1; }
    if (fired(2))  { printf("FAIL: a v1.1 score entry (no ae=) armed the accent overlay — the "
                            "v1.2 layer is NOT backward compatible\n"); return 1; }
    if (gUnit[0].pulseFx != CE_FLASH || gUnit[0].pulseColor.r != 255 ||
        gUnit[0].pulseColor.g != 0   || gUnit[0].pulseColor.b != 0 || gUnit[0].pulseDurMs != 200) {
      printf("FAIL: the scored accent did not carry ae=flash / ac=FF0000 / ad=200 "
             "(fx=%d rgb=%u,%u,%u dur=%lu)\n", (int)gUnit[0].pulseFx,
             (unsigned)gUnit[0].pulseColor.r, (unsigned)gUnit[0].pulseColor.g,
             (unsigned)gUnit[0].pulseColor.b, (unsigned long)gUnit[0].pulseDurMs);
      return 1;
    }
    // no ac= => the accent inherits the SECTION's colour (resolved at insert); no ad= => 180 ms
    if (gUnit[1].pulseFx != CE_PULSE || gUnit[1].pulseColor.g != 255 ||
        gUnit[1].pulseColor.r != 0 || gUnit[1].pulseDurMs != 180) {
      printf("FAIL: an ac=-less accent did not inherit the section colour / the 180 ms default "
             "(fx=%d rgb=%u,%u,%u dur=%lu)\n", (int)gUnit[1].pulseFx,
             (unsigned)gUnit[1].pulseColor.r, (unsigned)gUnit[1].pulseColor.g,
             (unsigned)gUnit[1].pulseColor.b, (unsigned long)gUnit[1].pulseDurMs);
      return 1;
    }
    // ...and it must actually be LAYERED onto the jewel: the accent's red over the blue base
    contractRenderHP(0);
    uint32_t px = neoStrips[0].shown[0];
    if (((px >> 16) & 0xFF) == 0 || (px & 0xFF) != 0) {
      printf("FAIL: the accent overlay is not on the jewel (want the ae= RED over the blue "
             "base, got 0x%06lX)\n", (unsigned long)px);
      return 1;
    }

    tick(t0 + 400);                              // STILL beat 0, and 390 ms on => PAST the
                                                 // 340 ms cool-down: only the beat EDGE can
                                                 // stop a re-fire here
    if (fired(0) || fired(1)) {
      printf("FAIL: the accent re-fired INSIDE the same beat — the once-per-beat edge is not "
             "being consumed, so a 180 ms accent becomes a permanent latch\n");
      return 1;
    }
    contractRenderHP(0);                         // the 200 ms accent has expired -> base back
    px = neoStrips[0].shown[0];
    if ((px & 0xFF) == 0 || ((px >> 16) & 0xFF) != 0) {
      printf("FAIL: the accent did not auto-restore the base look (want the blue base back, "
             "got 0x%06lX)\n", (unsigned long)px);
      return 1;
    }

    for (int beat = 1; beat <= 3; beat++) {      // barPos 1..3
      tick(t0 + (uint32_t)beat * 500u + 10u);
      if (fired(0)) {
        printf("FAIL: an am=1 (downbeat) accent fired on OFF-BEAT %d — the board-side trigger "
               "is not using the same fire predicate as the brightness pump\n", beat);
        return 1;
      }
      if (!fired(1)) {
        printf("FAIL: an am=2 (every-beat) accent did not fire on beat %d\n", beat);
        return 1;
      }
      if (fired(2)) { printf("FAIL: a v1.1 score entry accented on beat %d\n", beat); return 1; }
    }

    tick(t0 + 2010);                             // beat 4 == the next DOWNBEAT
    if (!fired(0)) {
      printf("FAIL: an am=1 accent did not fire on the next downbeat (beat 4)\n"); return 1;
    }
    if (gUnit[2].pulseActive || gUnit[2].pulseStartMs != 0) {
      printf("FAIL: a v1.1 score entry armed the overlay somewhere across the run\n"); return 1;
    }

    // ---------------------------------------------------------------------------------
    // A2 guard (PHYSICAL — the reason this fork exists as an LED-ONLY fork): the accent path
    // must NEVER move a holoprojector SERVO. main.cpp reaches the servos only through
    // HP_command[] and positionHP/twitchHP/wagHP/RCHP (+ flushCommandArray(hp,1)), and every
    // one of those is a tripwire in mock_flthy.h. The entire Phase-2 delivery and autonomous
    // playback above — score load, section switch, beat edges, accent fires, renders — must
    // not have tripped a single one, nor changed the operator's servo-twitch flags.
    if (_mock_servoTouches != 0) {
      printf("FAIL: the accent path touched the HP SERVO command path %d time(s) — this fork "
             "promises LED-EFFECTS-ONLY and must never move a holoprojector\n", _mock_servoTouches);
      return 1;
    }
    for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
      if (enableTwitchHP[hp] != twitchBefore[hp]) {
        printf("FAIL: the accent path changed enableTwitchHP[%u] (a PHYSICAL servo flag)\n",
               (unsigned)hp);
        return 1;
      }
      if ((byte)HP_command[hp].HPFunction != 0 || (byte)HP_command[hp].HPOption1 != 0) {
        printf("FAIL: the accent path left an HP SERVO command staged on HP %u\n", (unsigned)hp);
        return 1;
      }
    }
    if (contractParse("**X", q)) applyContract(q);
  }

  // A2b guard: the overlay must render the ae= EFFECT — not merely a solid fill in the accent
  // colour. That distinction IS the feature: Phase 1's verb-P accent was ALREADY a solid
  // 180 ms fill, and what a delivered blueprint was missing is the EFFECT SWAP.
  // Pinned with a look whose render differs from solid: at s=128 an ae=flash strobes with a
  // 434 ms ON half, so 500 ms into a 900 ms accent it must be DARK — where a solid overlay
  // would still be lit red, and the base look would be lit blue.
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);
    _mock_millis = 400000;
    uint32_t t0 = _mock_millis;
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HTA:i=solid,c=0000FF,s=128,at=0,am=1,ae=flash,ac=FF0000,ad=900", q)) applyContract(q);

    _mock_millis = t0 + 10;  contractBeatTick(); contractRenderHP(2);   // the accent fires here
    uint32_t lit = neoStrips[2].shown[0];
    if (((lit >> 16) & 0xFF) == 0) {
      printf("FAIL: the ae=flash accent is not lit in its ON half (got 0x%06lX) — this guard "
             "is measuring nothing\n", (unsigned long)lit);
      return 1;
    }
    _mock_millis = t0 + 510;               // 500 ms into the 900 ms accent => the flash's OFF
    contractRenderHP(2);                   // half. NB: render only — no tick, no new accent.
    uint32_t dark = neoStrips[2].shown[0];
    if (dark != 0x000000) {
      printf("FAIL: the accent overlay is NOT rendering its ae= EFFECT (got 0x%06lX 500 ms into "
             "a 900 ms ae=flash, where the strobe must be DARK). Red means it collapsed to a "
             "solid fill; blue means the base look is showing through\n", (unsigned long)dark);
      return 1;
    }
    if (!gUnit[2].pulseActive) { printf("FAIL: the 900 ms accent expired early\n"); return 1; }
    if (contractParse("**X", q)) applyContract(q);
  }

  // A3 guard: a show boundary must DROP the beat edge. If a stale lastAccentBeat survives, the
  // next show's first accent beat is silently swallowed (beatEdge() sees no change in index).
  // All five boundaries, pinned individually, then the end-to-end trap.
  {
    ParsedContract q;
    struct { const char* cmd; const char* what; } bounds[] = {
      {"HFX",                           "verb X (stop)"},
      {"HFM:v=show",                    "verb M v=show (a new show is loading)"},
      {"HFM:v=idle",                    "verb M v=idle (the show ended)"},
      {"**C:bpm=120,ph=0,bpb=4,beat=0", "verb C (the beat ORIGIN moved: a Play/seek re-anchor)"},
      {"HFA:i=solid,c=0000FF,at=0,am=2,ae=flash", "a scored A (a new show's sections)"},
    };
    for (auto& b : bounds) {
      gUnit[0].lastAccentBeat = 7;                       // pretend beat 7 accented, last show
      if (contractParse(b.cmd, q)) applyContract(q);
      if (gUnit[0].lastAccentBeat != BEAT_NONE) {
        printf("FAIL: %s did not reset the accent beat edge — a stale edge swallows the next "
               "show's first accent\n", b.what);
        return 1;
      }
    }
    if (contractParse("**X", q)) applyContract(q);

    // end-to-end: show 1 accents beat 0; STOP; show 2 is re-anchored so it ALSO starts at
    // beat 0 — its very first accent must still fire.
    _mock_millis = 200000;
    uint32_t t1 = _mock_millis;
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HFA:i=solid,c=0000FF,at=0,am=2,ae=flash", q)) applyContract(q);
    _mock_millis = t1 + 10; contractBeatTick();
    if (!gUnit[0].pulseActive) { printf("FAIL: show 1's first accent never fired\n"); return 1; }

    if (contractParse("HFX", q)) applyContract(q);                  // show 1 ends
    _mock_millis = t1 + 5000;                                       // show 2 arrives, re-anchors
    uint32_t t2 = _mock_millis;
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HFA:i=solid,c=00FF00,at=0,am=2,ae=flash", q)) applyContract(q);
    _mock_millis = t2 + 10; contractBeatTick();
    if (!gUnit[0].pulseActive || gUnit[0].pulseStartMs != t2 + 10) {
      printf("FAIL: the new show's first accent was SWALLOWED by a stale beat edge from the "
             "previous show (both start at beat 0)\n");
      return 1;
    }
    if (contractParse("**X", q)) applyContract(q);
  }

  // A4 guard (SAFETY / photosensitivity): accent STARTS must be at least 2x the min state
  // apart (2 * 170 = 340 ms => <= ~2.9 accents/sec, inside the <= 3 flashes/sec guidance).
  // v1.1 gated on 1x the min state here — the loosest cool-down of the three boards — so a
  // Phase-2 every-beat accent above ~176 BPM could have strobed the jewels at up to 5.9 Hz.
  // The cap must not be over-tightened either: a 2.5 Hz accent has to pass, or a Studio show
  // silently stutters on alternate beats and just looks broken.
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);
    _mock_millis = 500000;
    uint32_t t = _mock_millis;
    if (contractParse("HFP:c=FFFFFF,d=200", q)) applyContract(q);
    uint32_t first = gUnit[0].pulseStartMs;
    if (!gUnit[0].pulseActive || first != t) { printf("FAIL: verb P did not fire an accent\n"); return 1; }

    _mock_millis = t + 200;                        // 200 ms after the last one == 5 Hz: DROP
    if (contractParse("HFP:c=FFFFFF,d=200", q)) applyContract(q);
    if (gUnit[0].pulseStartMs != first) {
      printf("FAIL: an accent only 200 ms after the previous one was accepted — 5 flashes/sec "
             "exceeds the <= 3 flashes/sec photosensitivity cap\n");
      return 1;
    }
    _mock_millis = t + 400;                        // 400 ms == 2.5 Hz: must PASS
    if (contractParse("HFP:c=FFFFFF,d=200", q)) applyContract(q);
    if (gUnit[0].pulseStartMs != t + 400) {
      printf("FAIL: the strobe cool-down is dropping accents it should pass (2.5 Hz) — a "
             "Studio-authored show would stutter on alternate beats\n");
      return 1;
    }
    if (contractParse("**X", q)) applyContract(q);
  }

  // ======================================================================================
  // A6 guard (contract v1.2 — the LATCH / STATE-CORRUPTION hazard on verb P).
  // The SCORED accent is allow-gated at PARSE time: ae= simply refuses to store a rejected
  // effect, so a ScoreEntry can never carry one. Verb P is the OTHER door — it hands the
  // wire's i= straight to the overlay — and nothing here used to send a rejected i= on a P,
  // so the gate that stops it was covered by NO test at all.
  // Two effects classes must never become an overlay (contract_core's accentEffectAllowed):
  //   * STATEFUL (scan/sparkle/meter): they drive u.frame/u.frameMs, which they SHARE with the
  //     BASE look — a ~200 ms swap-and-restore corrupts the base look's state machine mid-song.
  //   * NATIVE (native:<n>): contractRenderHP() — and therefore the overlay's EXPIRY CHECK —
  //     only ever runs from LEDFunction 101..FLTHY_FX_MAX. Hand the render slot to a renderer
  //     the contract does not own and the overlay can never expire: the jewel LATCHES.
  // Rejected => CE_SOLID (the v1.1 solid fill), never "no accent": a P the operator sent must
  // still punctuate. Pinned three ways, because the gates are deliberately redundant:
  //   (a)+(b) BLACK BOX through verb P — the contract-level promise;
  //   (c)     WHITE BOX straight into _fireAccent(), which BYPASSES the CV_PULSE call site and
  //           is therefore the only thing that can see the gate INSIDE _fireAccent go missing.
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);
    _mock_millis = 600000;
    uint32_t t = _mock_millis;
    _mock_servoTouches = 0;                       // LED-only audit across this whole guard
    boolean twitchBefore[HPCOUNT] = {enableTwitchHP[0], enableTwitchHP[1], enableTwitchHP[2]};

    // a plain contract base look: blue, no beat pump, d=0 so it never ms-reverts under us
    if (contractParse("HFA:i=solid,c=0000FF,b=200,m=0,d=0", q)) applyContract(q);
    byte baseSlot = LED_command[0].LEDFunction;
    if (baseSlot < 101 || baseSlot > FLTHY_FX_MAX) {
      printf("FAIL: the base look is not in a contract render slot (LEDFunction=%u) — this "
             "guard is measuring nothing\n", (unsigned)baseSlot);
      return 1;
    }

    // ---- (a) verb P carrying i=native:3 — the LATCH hazard --------------------------------
    if (contractParse("HFP:i=native:3,c=FF0000,d=200", q)) applyContract(q);
    if (!gUnit[0].pulseActive || gUnit[0].pulseStartMs != t) {
      printf("FAIL: verb P did not fire an accent at all — this guard is measuring nothing\n");
      return 1;
    }
    if (gUnit[0].pulseFx != CE_SOLID) {
      printf("FAIL: a verb P with i=native:3 ARMED A NATIVE OVERLAY (pulseFx=%d, want CE_SOLID=%d). "
             "The native renderer is not in contractRenderHP's dispatch, so the overlay's expiry "
             "check would never run again and the jewel LATCHES\n",
             (int)gUnit[0].pulseFx, (int)CE_SOLID);
      return 1;
    }
    if (LED_command[0].LEDFunction != baseSlot) {
      printf("FAIL: a verb P with i=native:3 handed the render slot to the NATIVE path "
             "(LEDFunction=%u, want the contract slot %u) — contractRenderHP stops running and "
             "the accent can never expire\n",
             (unsigned)LED_command[0].LEDFunction, (unsigned)baseSlot);
      return 1;
    }
    contractRenderHP(0);                          // it must fall back to the SOLID fill: accent
    uint32_t px = neoStrips[0].shown[0];          // red over the blue base, centre pixel included
    if (((px >> 16) & 0xFF) == 0 || (px & 0xFF) != 0) {
      printf("FAIL: the native-rejected accent did not fall back to the solid fill (want the "
             "accent RED, got 0x%06lX)\n", (unsigned long)px);
      return 1;
    }
    _mock_millis = t + 400;                       // ...and, above all, it must EXPIRE
    contractRenderHP(0);
    if (gUnit[0].pulseActive) {
      printf("FAIL: the accent overlay NEVER EXPIRED 400 ms into a 200 ms accent — the board is "
             "LATCHED\n");
      return 1;
    }
    px = neoStrips[0].shown[0];
    if ((px & 0xFF) == 0 || ((px >> 16) & 0xFF) != 0) {
      printf("FAIL: the expired accent did not restore the blue base look (got 0x%06lX)\n",
             (unsigned long)px);
      return 1;
    }

    // ---- (b) verb P carrying i=scan — the STATE-CORRUPTION hazard -------------------------
    // CE_SCAN walks a dot around ring pixels 1..6 and leaves the jewel's CENTRE pixel (px0)
    // DARK, off frame counters it shares with the base look. The solid fallback lights px0.
    // So px0 tells the two renders apart, and frame/frameMs tell us the base look survived.
    uint8_t  frameBefore   = gUnit[0].frame;
    uint32_t frameMsBefore = gUnit[0].frameMs;
    _mock_millis = t + 1000;                      // well clear of the 340 ms strobe cool-down
    if (contractParse("HFP:i=scan,c=FF0000,d=200", q)) applyContract(q);
    if (!gUnit[0].pulseActive || gUnit[0].pulseStartMs != t + 1000) {
      printf("FAIL: verb P (i=scan) did not fire an accent at all — this guard is measuring "
             "nothing\n");
      return 1;
    }
    if (gUnit[0].pulseFx != CE_SOLID) {
      printf("FAIL: a verb P with i=scan ARMED A STATEFUL OVERLAY (pulseFx=%d, want CE_SOLID=%d) "
             "— CE_SCAN drives the frame counters it SHARES with the base look, so the swap-and-"
             "restore corrupts the base look's state machine mid-song\n",
             (int)gUnit[0].pulseFx, (int)CE_SOLID);
      return 1;
    }
    contractRenderHP(0);
    px = neoStrips[0].shown[0];                   // the CENTRE pixel
    if (px == 0) {
      printf("FAIL: the scan-rejected accent is rendering CE_SCAN — the jewel's CENTRE pixel is "
             "dark, where the solid fallback must light it\n");
      return 1;
    }
    if (((px >> 16) & 0xFF) == 0 || (px & 0xFF) != 0) {
      printf("FAIL: the scan-rejected accent did not fall back to the solid fill (want the accent "
             "RED, got 0x%06lX)\n", (unsigned long)px);
      return 1;
    }
    if (gUnit[0].frame != frameBefore || gUnit[0].frameMs != frameMsBefore) {
      printf("FAIL: the accent overlay advanced the BASE look's shared frame counters "
             "(frame %u->%u, frameMs %lu->%lu) — a stateful effect got in\n",
             (unsigned)frameBefore, (unsigned)gUnit[0].frame,
             (unsigned long)frameMsBefore, (unsigned long)gUnit[0].frameMs);
      return 1;
    }
    _mock_millis = t + 1400;                      // and it EXPIRES: no latch here either
    contractRenderHP(0);
    if (gUnit[0].pulseActive) {
      printf("FAIL: the scan-rejected accent never EXPIRED — the board is LATCHED\n");
      return 1;
    }

    // ---- (c) WHITE BOX: straight into _fireAccent(), bypassing the CV_PULSE call site ------
    // (a) and (b) are satisfied by EITHER gate, so on their own they cannot see the one inside
    // _fireAccent() disappear. This can: it is the primitive's own contract, and it also covers
    // every future caller of it.
    const ContractEffect rejected[] = {CE_SCAN, CE_SPARKLE, CE_METER, CE_NATIVE};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
      _mock_millis = t + 2000 + (uint32_t)i * 400u;        // each fire clear of the cool-down
      if (!_fireAccent(0, rejected[i], RGB{255, 0, 0}, 200, 200)) {
        printf("FAIL: _fireAccent refused to fire at all (effect %d) — this guard is measuring "
               "nothing\n", (int)rejected[i]);
        return 1;
      }
      if (gUnit[0].pulseFx != CE_SOLID) {
        printf("FAIL: _fireAccent ARMED a stateful/native overlay (effect %d -> pulseFx=%d, want "
               "CE_SOLID=%d) when handed one DIRECTLY. The allow-gate inside _fireAccent is gone, "
               "so the CV_PULSE call site is the only thing left between a native i= and a "
               "permanently LATCHED jewel\n",
               (int)rejected[i], (int)gUnit[0].pulseFx, (int)CE_SOLID);
        return 1;
      }
      if (LED_command[0].LEDFunction < 101 || LED_command[0].LEDFunction > FLTHY_FX_MAX) {
        printf("FAIL: _fireAccent left the render slot outside the contract range "
               "(LEDFunction=%u) — contractRenderHP stops running and the accent can never "
               "expire\n", (unsigned)LED_command[0].LEDFunction);
        return 1;
      }
    }

    // LED-ONLY INVARIANT (§11): none of the above may have gone near a holoprojector SERVO.
    if (_mock_servoTouches != 0) {
      printf("FAIL: the rejected-accent path touched the HP SERVO command path %d time(s) — this "
             "fork promises LED-EFFECTS-ONLY\n", _mock_servoTouches);
      return 1;
    }
    for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
      if (enableTwitchHP[hp] != twitchBefore[hp]) {
        printf("FAIL: the rejected-accent path changed enableTwitchHP[%u] (a PHYSICAL servo "
               "flag)\n", (unsigned)hp);
        return 1;
      }
      if ((byte)HP_command[hp].HPFunction != 0 || (byte)HP_command[hp].HPOption1 != 0) {
        printf("FAIL: the rejected-accent path left an HP SERVO command staged on HP %u\n",
               (unsigned)hp);
        return 1;
      }
    }
    if (contractParse("**X", q)) applyContract(q);
  }

  // A5 guard: Flthy's CE_FLASH BEAT-PUMPS — it renders at the envelope, not at the raw b=
  // ceiling. The Logics' CE_FLASH is being aligned TO this line, so pin it here or the
  // alignment target can drift out from under the other two boards.
  // Also pins that a LIVE (unscored) look NEVER self-accents: only the score path fires the
  // board-side overlay, so a board driven live cannot double-fire (the Pi's verb P AND its
  // own beat edge on the same beat).
  // 120 BPM (500 ms/beat), s=128 => the flash's ON half is 434 ms. Sample two moments that are
  // both inside an ON half but at opposite ends of a beat: phase ~0.02 (full accent) vs phase
  // ~0.92 (the envelope has decayed to its floor).
  {
    ParsedContract q;
    if (contractParse("**X", q)) applyContract(q);
    _mock_millis = 300000;
    uint32_t T0 = _mock_millis;
    if (contractParse("**C:bpm=120,ph=0,bpb=4,beat=0", q)) applyContract(q);
    if (contractParse("HFA:i=flash,c=0000FF,s=128,b=200,m=200,am=2", q)) applyContract(q);  // LIVE: no at=

    auto litBlue = [&](uint32_t ms) -> uint8_t {
      _mock_millis = ms;
      contractBeatTick();
      contractRenderHP(0);
      return (uint8_t)(neoStrips[0].shown[0] & 0xFFu);      // c=0000FF => blue == the level
    };
    uint8_t onBeat  = litBlue(T0 + 10);    // beat 0 phase 0.02, flash elapsed  10 ms -> ON
    uint8_t offBeat = litBlue(T0 + 960);   // beat 1 phase 0.92, flash elapsed 960 ms -> ON (960 % 868 = 92)

    if (gUnit[0].pulseActive) {
      printf("FAIL: a LIVE (unscored) look self-accented — only a scored section may fire the "
             "board-side overlay, or a live-driven board double-fires with the Pi's verb P\n");
      return 1;
    }
    if (onBeat == 0 || offBeat == 0) {
      printf("FAIL: the CE_FLASH pump guard sampled the OFF half of the strobe (on=%u off=%u) — "
             "it is measuring nothing\n", (unsigned)onBeat, (unsigned)offBeat);
      return 1;
    }
    if (onBeat < offBeat + 24) {
      printf("FAIL: CE_FLASH does not BEAT-PUMP — its lit level is the same on the beat as it is "
             "at the end of one (on=%u off=%u); it is rendering at the raw b= ceiling instead of "
             "the envelope\n", (unsigned)onBeat, (unsigned)offBeat);
      return 1;
    }
    if (contractParse("**X", q)) applyContract(q);
    _mock_millis = 0;
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

  printf("ContractFlthy.h type-check + score-native / servo-twitch / score-clear / build-ramp / "
         "Q-unit / v1.2 accent-self-fire / servo-interlock / beat-edge / strobe-cap / "
         "accent-allow-gate (no stateful or native overlay, ever) / flash-pump guards OK\n");
  return 0;
}
