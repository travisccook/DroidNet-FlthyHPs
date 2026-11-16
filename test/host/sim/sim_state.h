// Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
// bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// What the scenarios read of the contract layer's state. The ONLY simulation file that names the
// layer's own variables, so moving the unit state (plan Task 13: onto the core's ContractUnitState)
// edits this file and no scenario.
#pragma once
namespace sim {
inline bool armed(int hp)            { return gInterlocked[hp]; }          // M:v=show is in force
inline bool lookUp(int hp)           { return gUnit[hp].active; }          // a contract look is the base
inline int  lookEffect(int hp)       { return (int)gUnit[hp].effect; }
inline int  lookBright(int hp)       { return gUnit[hp].brightBase; }      // b=, 255 = the ceiling
inline int  lookSpeed(int hp)        { return gUnit[hp].speed; }
inline int  lookBeatMod(int hp)      { return gUnit[hp].beatMod; }
inline int  lookAm(int hp)           { return gUnit[hp].accentMode; }
inline uint32_t accentStartMs(int hp){ return gUnit[hp].pulseStartMs; }    // changes each time an overlay starts
inline uint32_t accentDurMs(int hp)  { return gUnit[hp].pulseDurMs; }
inline int  scoreLen(int hp)         { return gScoreCount[hp]; }
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
