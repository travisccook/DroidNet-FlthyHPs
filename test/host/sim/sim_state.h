// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// What the scenarios read of the contract layer's state. The ONLY simulation file that names the
// layer's own variables (a unit's show state is the core's ContractUnitState, gUnit[hp].s), so a
// change to where that state lives edits this file and no scenario.
#pragma once
namespace sim {
inline bool armed(int hp)            { return gUnit[hp].s.inShow; }       // M:v=show is in force
inline bool lookUp(int hp)           { return gUnit[hp].s.base == CB_LOOK; }   // a contract look is the base
inline int  lookEffect(int hp)       { return (int)gUnit[hp].s.effect; }
inline int  lookBright(int hp)       { return gUnit[hp].s.bright; }        // b=, 255 = the ceiling
inline int  lookSpeed(int hp)        { return gUnit[hp].s.speed; }
inline int  lookBeatMod(int hp)      { return gUnit[hp].s.beatMod; }
inline int  lookAm(int hp)           { return gUnit[hp].s.accentMode; }
inline uint32_t accentStartMs(int hp){ return gUnit[hp].s.pulseStartMs; }    // changes each time an overlay starts
inline uint32_t accentDurMs(int hp)  { return gUnit[hp].s.pulseDurMs; }
// Does the contract layer have this jewel's LEDs right now? Written out from the primitive state on
// purpose: the S9 property test uses it to tell the layer's relights from the stock firmware's, and
// must not ask the firmware's own gate for the answer.
inline bool simHasLeds(int hp) {
  const ContractUnitState& s = gUnit[hp].s; byte f = LED_command[hp].LEDFunction;
  return (s.inShow && s.base != CB_NONE) || s.base == CB_LOOK || s.base == CB_NATIVE || s.pulseActive || (f >= 101 && f <= 118);
}
// S10: a native look the contract started that RUNS AS STOCK is up (its own frames are the stock look's).
inline bool simStockNativeUp(int hp) {
  const ContractUnitState& s = gUnit[hp].s;
  return s.base == CB_NATIVE && !gUnit[hp].nativeWait && !nativeGatedFlthy(s.nativeCode) && LED_command[hp].LEDFunction == (byte)s.nativeCode;
}
// v1.4 (Task F4): projector's state on a jewel, and the window it shares with L, B and b=.
inline uint32_t projNextStepMs(int hp) { return gUnit[hp].frameMs; }        // when the look's next step is due
inline int  projSlot(int hp, int px)  { return gUnit[hp].projSlot[px]; }    // pixel px's slot of the row, 0..9
inline int  projWaiting(int hp)      { return gUnit[hp].projWait; }       // 0: no frame of it waits; 1: a crumbling step's; 2: one that raises light
inline uint32_t levelWindowMs(int hp) { return gUnit[hp].s.levelMs; }       // when the unit's light last went up (core: levelMs)
inline int  unitLevel(int hp)         { return gUnit[hp].s.level; }         // what a meter or vu shows (core: level)
inline int  scoreLen(int hp)         { return gUnit[hp].s.scoreLen; }
inline bool clockRunning()           { return gBeat.running; }
inline int  clockBpm()               { return gBeat.bpm; }
inline int  clockBpb()               { return gBeat.bpb; }
inline BeatPos clockPos()            { return beatPosAt(gBeat, millis()); }
}  // namespace sim
// The firmware's own "the contract holds unit i" test, as the SERVO twitch gate in main.cpp spells
// it. Used only to excuse a stock servo twitch that was due in a pass anyway.
#ifndef SIM_HELD
#define SIM_HELD(i) (contractHolds(i))
#endif
