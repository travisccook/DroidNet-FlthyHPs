// src/wdp/wdp_fork.h — the FlthyHPs adapter for the shared WDP device announce core
// (wdp_announce_core.h, byte-identical across the RSeries, PSI Pro and FlthyHPs forks).
// Every 25-30 s the board tells the WCB on its serial port what it is:
//
//     @WDP1 {"type":"Flthy HP Controller","fw":"<commit>","caps":["droidnet.contract","hp.servo","hp.led"]}
//
// It goes out on Serial (UART0, D0/D1, 9600 baud), the WCB's port, which is also the Mega's USB
// serial. The line leaves in pieces no bigger than the room in Serial's TX buffer (63 bytes when
// empty, and the line is longer), so loop() never waits on it. Every other write to Serial after
// setup() calls wdpFinish() first, so no other text lands inside an announce: RCHP's "No RC Input
// Detected!" in main.cpp and the verb-Q ack in ContractFlthy.h. main.cpp's DEBUG prints do not;
// DEBUG is off (commented out) in this build.
//
// Part of the DroidNet board forks. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
#pragma once
#include "wdp_announce_core.h"

// The commit this was built from, set by scripts/wdp_fw.py (platformio.ini extra_scripts).
#ifndef WDP_FW
#define WDP_FW "unknown"
#endif

#define WDP_FORK_TYPE     "Flthy HP Controller"                 // the WCB's shared device name
#define WDP_FORK_CAPS     "droidnet.contract hp.servo hp.led"   // space-separated tags
#define WDP_FORK_SEED_PIN 3   // A3: nothing in this sketch uses it (main.cpp seeds random() from it too)

static WdpAnnouncer gWdp;

// Once, in setup() after the boot banner. The seed must differ board to board, or boards that
// power up together announce in lockstep: 16 reads of the floating A3 (about 1.8 ms), folded
// FNV-1a style over micros().
inline void wdpForkSetup() {
  uint32_t seed = 2166136261u ^ (uint32_t)micros();
  for (uint8_t i = 0; i < 16; i++)
    seed = (seed ^ (uint32_t)analogRead(WDP_FORK_SEED_PIN)) * 16777619u;
  wdpInit(gWdp, seed, (uint32_t)millis());
}

// Every loop() pass: writes what the core hands out, never more than Serial can take now.
inline void wdpForkTick() {
  const char* chunk = 0;
  uint8_t n = wdpTick(gWdp, (uint32_t)millis(), WDP_FORK_TYPE, WDP_FW, WDP_FORK_CAPS,
                      Serial.availableForWrite(), &chunk);
  if (n) Serial.write((const uint8_t*)chunk, n);
}

// Before any other write to Serial: sends the rest of a line in flight, if there is one. This can
// wait for room in the TX buffer: at most the time to send that rest, about 0.1 s at 9600 baud.
inline void wdpFinish() {
  const char* chunk = 0;
  uint8_t n = wdpRest(gWdp, &chunk);
  if (n) Serial.write((const uint8_t*)chunk, n);
}
