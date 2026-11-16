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
