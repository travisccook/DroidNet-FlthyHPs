// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT (see LICENSE-DroidNet-Contract)
// The firmware this layer drives — every LED primitive it calls — is Ryan Sondgeroth's
// work; it carries no license and is all rights reserved by him, and that MIT license
// covers our files only. See the NOTICE in README.md.
//
// src/contract/ContractFlthy.h — Flthy Holoprojectors firmware layer for the
// Driveable-Animation Contract. LED-ONLY: the '!' path NEVER writes HP_command[*]
// and NEVER calls positionHP/twitchHP/wagHP/RCHP — no servo motion, ever.
//
// INCLUDE ORDER (see main.cpp): include this AFTER the board globals + effect
// primitives it references are declared, and BEFORE loop(). It relies on these
// being already declared (all verified against src/main.cpp 2026-07-12):
//   Adafruit_NeoPixel neoStrips[HPCOUNT];             // main.cpp:667
//     .setPixelColor(uint16_t,uint32_t)               // main.cpp:1206,1212,1226
//     .show()                                          // main.cpp:1207
//     .setBrightness(uint8_t)                          // main.cpp:748
//   LEDCmd  LED_command[HPCOUNT];  (.LEDFunction byte) // main.cpp:600-608,925
//   byte    enableTwitchLED[HPCOUNT];                  // main.cpp:315
//   boolean enableTwitchHP[HPCOUNT];                   // main.cpp:335
//   const byte startEnableTwitchLED[HPCOUNT];          // main.cpp:574
//   const boolean startEnableTwitchHP[HPCOUNT];        // main.cpp:575 (DroidNet, added w/ the fork)
//   boolean offcoloroverride[HPCOUNT];                 // main.cpp:619
//   void ledOFF(byte);                                 // main.cpp:1205
//   void varResets(byte);                              // main.cpp:1460
//   #define HPCOUNT 3 / #define NEO_JEWEL_LEDS 7       // main.cpp:173,659
//   millis(), random(), Serial                         // Arduino core
//
// DESIGN: arbitrary RGB is injected straight into the NEO_GRB jewels via
// setPixelColor(i, 0xRRGGBB) (the library takes a packed uint32_t — main.cpp:1374),
// bypassing the palette table entirely (basicColors/dimColorVal). Each contract
// effect writes 0xRRGGBB pre-scaled by a per-write brightness multiplier (never
// setBrightness, which only re-scales at show() — fork spec §9). The per-unit look
// renders through LED_command[hp].LEDFunction = 100 + effectId (cases 101..115,
// added to the render switch); verb P layers a full-field pulse OVER the base and
// auto-restores on ms-expiry (never ledOFF / never flushCommandArray — the native
// flush-to-black). A Studio-seeded beat-clock (verb C) drives an accent envelope,
// and the Phase-2 score (verb A + at=) switches sections on-beat, board-side.
#pragma once
#include "contract_core.h"
#include <stdio.h>          // snprintf (verb Q ack line)

// -------------------------------------------------------------- constants ----
static const uint8_t  FLTHY_SAFE_MAX_BRIGHT = 200;   // conservative jewel clamp (fork spec §11)
static const uint32_t FLTHY_STROBE_MIN_MS   = 170;   // >=170 ms/state => <= ~3 Hz (fork spec §11)
static const uint8_t  FLTHY_FX_BASE         = 100;   // LEDFunction = 100 + effectId (101..115)
static const int      FLTHY_SCORE_CAP       = 8;     // per-unit Phase-2 sections

// contract effectId -> LEDFunction render code (CE_OFF..CE_TWINKLE map to 101..115)
static inline byte _fxCode(ContractEffect e) { return (byte)(FLTHY_FX_BASE + (uint8_t)e); }
// hp index -> the contract unit letter it answers as (Front/Rear/Top, fork spec §4)
static inline char _unitChar(uint8_t hp) { return (hp == 0) ? 'F' : ((hp == 1) ? 'R' : 'T'); }
// upper bound of the contract render-slot range (main.cpp's render switch + the
// CV_PULSE guard below both gate on this; bump it here, not with a fresh
// literal, when a later task adds another effect after CE_TWINKLE).
static const uint8_t  FLTHY_FX_MAX          = _fxCode(CE_TWINKLE);   // 115

// ----------------------------------------------------------- per-unit state ---
struct FlthyUnit {
  bool           active   = false;        // the ! path is driving this jewel
  ContractEffect effect   = CE_SOLID;
  RGB            color{0, 0, 0};
  uint8_t        speed    = 128;
  uint8_t        brightBase = FLTHY_SAFE_MAX_BRIGHT;   // b (pre-envelope)
  uint8_t        beatMod  = 0;            // m (envelope depth)
  uint8_t        accentMode = 0;          // am
  uint32_t       durMs    = 0;            // d (0 = hold until changed)
  uint32_t       startMs  = 0;            // millis() at apply
  // Phase-2 section span (beats), for the am=3 "build" ramp. Equal => no span known
  // (a live, unscored cue), which reads as progress 0 — same as the Logics/PSIs.
  int32_t        sectionStart = 0, sectionEnd = 0;
  uint8_t        level    = 0;            // last verb-L value (meter substitute)
  int            lastEnvBright = -1;      // force-repaint on envelope change (fork spec §9)
  // scan/sparkle self-throttle
  uint32_t       frameMs  = 0;
  uint8_t        frame    = 0;
  // verb P overlay (layers over the base look; never flushes to black)
  bool           pulseActive = false;
  RGB            pulseColor{255, 255, 255};
  uint8_t        pulseBright  = FLTHY_SAFE_MAX_BRIGHT;
  uint32_t       pulseStartMs = 0;
  uint32_t       pulseDurMs   = 0;
  uint32_t       pulseLastMs  = 0;        // strobe cool-down anchor
};

static FlthyUnit  gUnit[HPCOUNT];
static BeatClock  gBeat;
static bool       gShowMode = false;

// ------------------------------------------- show-mode interlock save-state ---
// Show mode takes native autonomy away from a unit (it disables the native auto-twitch
// so the jewel is ours and, critically, so the HOLOPROJECTOR SERVO never moves — this
// fork is LED-ONLY, §11). Idle must put back EXACTLY what show took and NOTHING ELSE:
//   * gInterlocked[hp] gates the restore, so an idle that never followed a show cannot
//     touch the operator's settings at all (a bare !H*M:v=idle is a no-op on them).
//   * gSavedTwitchHP[hp] holds the servo-twitch flag observed at show entry, seeded from
//     the boot config startEnableTwitchHP[] (main.cpp:575). NEVER write `true` here from
//     a literal: enableTwitchHP drives a PHYSICAL actuator and the operator may have
//     turned it off in the sketch config or at runtime (native servo cmd 98, main.cpp:984).
//   * gSavedOffColor[hp] likewise holds the native "off color" override observed at show
//     entry (show forces it on so the off state is truly black); without restoring it, the
//     operator's off-color stayed suppressed after every show until a native 98/99 command.
static bool    gInterlocked[HPCOUNT]  = {false, false, false};
static boolean gSavedTwitchHP[HPCOUNT] = {startEnableTwitchHP[0],
                                          startEnableTwitchHP[1],
                                          startEnableTwitchHP[2]};
static boolean gSavedOffColor[HPCOUNT] = {false, false, false};   // main.cpp:619 default

// Phase-2 score (per unit, sorted asc by atBeat via contract_core scoreInsert)
static ScoreEntry gScore[HPCOUNT][FLTHY_SCORE_CAP];
static int        gScoreCount[HPCOUNT]  = {0, 0, 0};
static int        gScoreActive[HPCOUNT] = {-1, -1, -1};

// ---------------------------------------------------------------- helpers ----
static inline uint8_t _clampBright(int v) {
  return (uint8_t)(v < 0 ? 0 : (v > FLTHY_SAFE_MAX_BRIGHT ? FLTHY_SAFE_MAX_BRIGHT : v));
}
// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB
// ordering is handled inside the library; we always pass logical R,G,B).
static inline uint32_t _scale(const RGB& c, uint8_t bri) {
  uint8_t r = (uint8_t)((uint16_t)c.r * bri / 255);
  uint8_t g = (uint8_t)((uint16_t)c.g * bri / 255);
  uint8_t b = (uint8_t)((uint16_t)c.b * bri / 255);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static inline void _fillShow(uint8_t hp, uint32_t color) {
  for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++) neoStrips[hp].setPixelColor(i, color);
  neoStrips[hp].show();
}
// self-contained rainbow wheel (mirrors native Wheel(), main.cpp:1328) so a verb-P
// overlay still composites over rainbow through the single contract render path.
static inline uint32_t _wheel(uint8_t pos, uint8_t bri) {
  RGB c{0, 0, 0};
  if (pos < 85)       { c.r = (uint8_t)(pos * 3);        c.g = (uint8_t)(255 - pos * 3); c.b = 0; }
  else if (pos < 170) { pos = (uint8_t)(pos - 85);  c.r = (uint8_t)(255 - pos * 3); c.g = 0; c.b = (uint8_t)(pos * 3); }
  else                { pos = (uint8_t)(pos - 170); c.r = 0; c.g = (uint8_t)(pos * 3); c.b = (uint8_t)(255 - pos * 3); }
  return _scale(c, bri);
}

// ------------------------------------------------------------- envelope -------
// Effective per-write brightness for a unit: baseline dips by beatMod depth, the
// accent rides it back up on the beat (fork spec §9). am=0 => flat brightBase.
// Level math lives in contract_core's envBright() — the SHARED envelope all three
// boards render through (b= is the ceiling, m= is the dip depth). Do not reintroduce
// a board-local floor here: it desyncs the HPs from the Logics/PSIs on every cue.
static inline uint8_t _envBright(uint8_t hp) {
  const FlthyUnit& u = gUnit[hp];
  if (!gBeat.running || u.beatMod == 0 || u.accentMode == 0) return u.brightBase;
  BeatPos bp = beatPosAt(gBeat, millis());
  // am=3 (build) ramps across the section; the span is only known while a scored
  // section is active, so a stale span from a finished show can't leak in here.
  float prog = 0.0f;
  if (gScoreActive[hp] >= 0 && u.sectionEnd > u.sectionStart)
    prog = (float)(bp.beatIndex - u.sectionStart) / (float)(u.sectionEnd - u.sectionStart);
  uint8_t accent = beatAccentAmount(u.accentMode, bp, u.beatMod, prog);
  return envBright(u.brightBase, u.beatMod, accent);
}

// --------------------------------------------------- the single render fn -----
// Called from the LED render switch (cases 101..115) once per loop for unit hp.
inline void contractRenderHP(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  uint32_t now = millis();

  // ms-duration revert (true sub-second holds; parallels the native seconds pipe)
  if (u.active && u.durMs > 0 && (uint32_t)(now - u.startMs) >= u.durMs) {
    u.active = false;
    u.effect = CE_OFF;
    LED_command[hp].LEDFunction = _fxCode(CE_OFF);
    u.lastEnvBright = -1;
  }

  // verb-P overlay: layer a solid pulse over the base, auto-restore on expiry.
  if (u.pulseActive) {
    if ((uint32_t)(now - u.pulseStartMs) < u.pulseDurMs) {
      _fillShow(hp, _scale(u.pulseColor, u.pulseBright));
      return;
    }
    u.pulseActive  = false;         // pulse done -> restore base look next
    u.lastEnvBright = -1;           // force a base repaint
  }

  uint8_t envB = _envBright(hp);

  switch (u.effect) {
    case CE_OFF:
      if (u.lastEnvBright != 0) { _fillShow(hp, 0x000000); u.lastEnvBright = 0; }
      break;

    case CE_FLASH: {                                   // strobe-capped toggle
      uint32_t half = (uint32_t)map((long)u.speed, 0, 255, 700, (long)FLTHY_STROBE_MIN_MS);
      if (half < FLTHY_STROBE_MIN_MS) half = FLTHY_STROBE_MIN_MS;
      bool on = ((now - u.startMs) % (2 * half)) < half;
      _fillShow(hp, on ? _scale(u.color, envB) : 0x000000);
      break;
    }

    case CE_PULSE: {                                   // triangle breathe, peak = brightest
      uint32_t period = (uint32_t)map((long)u.speed, 0, 255, 2000, 240);
      if (period < 2) period = 2;
      uint32_t ph = (now - u.startMs) % period;
      uint32_t tri = (ph < period / 2) ? (ph * 2 * 255 / period)
                                       : ((period - ph) * 2 * 255 / period);
      uint8_t br = (uint8_t)((uint16_t)envB * (uint16_t)tri / 255);
      _fillShow(hp, _scale(u.color, br));
      break;
    }

    case CE_RAINBOW: {                                 // color ignored (contract §6)
      uint32_t step = (uint32_t)map((long)u.speed, 0, 255, 40, 8);
      if (step < 1) step = 1;
      uint8_t base = (uint8_t)(((now - u.startMs) / step) & 255);
      for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++)
        neoStrips[hp].setPixelColor(i, _wheel((uint8_t)((i * 256 / NEO_JEWEL_LEDS) + base), envB));
      neoStrips[hp].show();
      break;
    }

    case CE_SCAN: {                                    // dot walks ring 1..6, center px0 off
      uint32_t step = (uint32_t)map((long)u.speed, 0, 255, 160, 30);
      if ((uint32_t)(now - u.frameMs) > step) {
        u.frameMs = now;
        neoStrips[hp].setPixelColor(0, 0x000000);      // center always off (main.cpp:1285)
        if (u.frame >= NEO_JEWEL_LEDS) u.frame = 1;
        if (u.frame < 1) u.frame = 1;
        for (uint16_t i = 1; i < NEO_JEWEL_LEDS; i++)
          neoStrips[hp].setPixelColor(i, (i == u.frame) ? _scale(u.color, envB) : 0x000000);
        neoStrips[hp].show();
        u.frame++;
      }
      break;
    }

    case CE_SPARKLE: {                                 // re-roll per-px brightness/off, keep hue
      uint32_t step = (uint32_t)map((long)u.speed, 0, 255, 150, 40);
      if ((uint32_t)(now - u.frameMs) > step) {
        u.frameMs = now;
        for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++) {
          long roll = random(0, 4);                    // 1/4 dark, else varied brightness
          uint8_t b = (roll == 0) ? 0 : (uint8_t)((uint16_t)envB * (uint16_t)random(96, 256) / 255);
          neoStrips[hp].setPixelColor(i, _scale(u.color, b));
        }
        neoStrips[hp].show();
      }
      break;
    }

    case CE_METER: {                                   // no VU on HP: solid @ level (fork spec §6/§8)
      uint8_t br = (uint8_t)((uint16_t)envB * (uint16_t)u.level / 255);
      if (u.lastEnvBright != (int)br) { _fillShow(hp, _scale(u.color, br)); u.lastEnvBright = (int)br; }
      break;
    }

    case CE_COMET: {                                   // comet trail around the 6-jewel ring, center off
      int N = NEO_JEWEL_LEDS - 1;                       // ring positions (center excluded)
      int head = fxHead(now - u.startMs, u.speed, N);
      neoStrips[hp].setPixelColor(0, 0x000000);         // center off
      for (int p = 0; p < N; p++) {
        uint8_t cb = fxCometBright(p, head, N);
        uint8_t v = (uint8_t)(((uint16_t)envB * cb) / 255);
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), _scale(u.color, v));
      }
      neoStrips[hp].show();
      break;
    }

    case CE_CHASE: {                                    // marquee chase around the ring, center off
      int N = NEO_JEWEL_LEDS - 1;                         // ring positions (center excluded)
      uint32_t el = now - u.startMs;
      neoStrips[hp].setPixelColor(0, 0x000000);           // center off
      for (int p = 0; p < N; p++) {
        uint32_t c = fxChaseLit(p, el, u.speed) ? _scale(u.color, envB) : 0x000000;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), c);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_WIPE: {                                     // ping-pong fill wipe around the ring, center off
      int N = NEO_JEWEL_LEDS - 1;                         // ring positions (center excluded)
      uint32_t el = now - u.startMs;
      neoStrips[hp].setPixelColor(0, 0x000000);           // center off
      for (int p = 0; p < N; p++) {
        uint32_t c = fxWipeLit(p, el, u.speed, N) ? _scale(u.color, envB) : 0x000000;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), c);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_GRADIENT: {                                 // hue gradient across the ring, center off
      int N = NEO_JEWEL_LEDS - 1;                         // ring positions (center excluded)
      uint32_t el = now - u.startMs;
      neoStrips[hp].setPixelColor(0, 0x000000);           // center off
      for (int p = 0; p < N; p++) {
        uint8_t hue = fxGradientHue(p, N, 0, el, u.speed); // base hue 0, color-independent (like rainbow)
        RGB c = fxHsv2rgb(hue, 255, envB);
        uint32_t packed = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), packed);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_COLORCYCLE: {                               // whole-jewel hue rotation (color-independent)
      RGB c = fxHsv2rgb(fxCycleHue(0, now - u.startMs, u.speed), 255, envB);
      uint32_t packed = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
      _fillShow(hp, packed);
      break;
    }

    case CE_TWINKLE: {                                  // per-px hashed triangle twinkle, whole jewel
      for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++) {
        uint8_t tb = fxTwinkleBright((int)i, now, u.speed);
        uint8_t v = (uint8_t)(((uint16_t)envB * tb) / 255);
        neoStrips[hp].setPixelColor(i, _scale(u.color, v));
      }
      neoStrips[hp].show();
      break;
    }

    case CE_SOLID:
    default:
      if (u.lastEnvBright != (int)envB) { _fillShow(hp, _scale(u.color, envB)); u.lastEnvBright = (int)envB; }
      break;
  }
}

// ---------------------------------------------------- apply a look to a unit ---
// Common path for Phase-1 (live A) and Phase-2 (score switch): sets cState + the
// render code. NEVER flushes to black, NEVER writes HP_command (LED-only, §11).
static inline void _applyLook(uint8_t hp, ContractEffect eff, int nativeCode,
                              const RGB& color, uint8_t speed, uint8_t bright,
                              uint8_t beatMod, uint8_t am, uint32_t durMs,
                              bool freshEffect) {
  FlthyUnit& u = gUnit[hp];
  u.color       = color;
  u.speed       = speed;
  u.brightBase  = _clampBright(bright);
  u.beatMod     = beatMod;
  u.accentMode  = am;
  u.durMs       = durMs;
  u.startMs     = millis();
  u.pulseActive = false;
  u.lastEnvBright = -1;                                 // force first repaint

  if (eff == CE_NATIVE && nativeCode >= 0) {
    // escape hatch: hand off to a native LED function (0..7); NOT a contract render
    u.active = false;
    LED_command[hp].LEDFunction = (byte)nativeCode;
    return;
  }

  u.active = true;
  u.effect = eff;
  if (freshEffect && (eff == CE_SCAN || eff == CE_SPARKLE)) {
    u.frame = 1; u.frameMs = 0;
    varResets(hp);                                      // reset Frame/SC counters once at switch (main.cpp:1460)
  }
  LED_command[hp].LEDFunction = _fxCode(eff);
}

// build a ScoreEntry from a parsed look (Phase-2 insert)
static inline ScoreEntry _entryFrom(const ContractParams& pr) {
  ScoreEntry e;
  e.atBeat     = pr.atBeat;
  e.effect     = pr.hasEffect ? pr.effect : CE_SOLID;
  e.color      = pr.color;
  e.speed      = pr.hasSpeed ? pr.speed : 128;
  e.beatMod    = pr.beatMod;
  e.accentMode = pr.accentMode;
  e.nativeCode = pr.hasEffect ? pr.nativeCode : -1;      // thread native code into the score (parity w/ RSeries)
  return e;
}

// switch a unit to a scored section (Phase-2 onBeat), clean-starting the look.
static inline void _applyScore(uint8_t hp, const ScoreEntry& e) {
  gUnit[hp].pulseActive = false;
  _applyLook(hp, e.effect, e.nativeCode, e.color, e.speed, gUnit[hp].brightBase,
             e.beatMod, e.accentMode, 0, /*freshEffect=*/true);
}

// ------------------------------------------------------------- verb dispatch ---
inline void applyContractToUnit(uint8_t hp, const ParsedContract& p) {
  FlthyUnit& u = gUnit[hp];
  const ContractParams& pr = p.params;
  uint32_t now = millis();

  switch (p.verb) {
    case CV_ANIMATE: {
      if (pr.hasAt) {                                  // Phase-2: schedule, don't apply now
        ScoreEntry e = _entryFrom(pr);
        gScoreCount[hp] = scoreInsert(gScore[hp], gScoreCount[hp], FLTHY_SCORE_CAP, e);
        gScoreActive[hp] = -1;                         // re-evaluate on next tick
        return;
      }
      // Phase-1: apply now. Omitting i= patches params of the current look.
      ContractEffect eff = pr.hasEffect ? pr.effect : u.effect;
      RGB   col   = pr.hasColor   ? pr.color   : u.color;
      uint8_t spd = pr.hasSpeed   ? pr.speed   : u.speed;
      uint8_t bri = pr.hasBright  ? pr.bright  : u.brightBase;
      uint8_t bm  = pr.hasBeatMod ? pr.beatMod : u.beatMod;
      uint8_t am  = pr.hasAm      ? pr.accentMode : u.accentMode;
      uint32_t d  = pr.hasDur     ? pr.durMs   : u.durMs;
      _applyLook(hp, eff, pr.nativeCode, col, spd, bri, bm, am, d,
                 /*freshEffect=*/pr.hasEffect);
      break;
    }

    case CV_PULSE: {
      // strobe cool-down: drop pulses arriving faster than ~3 Hz (fork spec §11)
      if (u.pulseLastMs != 0 && (uint32_t)(now - u.pulseLastMs) < FLTHY_STROBE_MIN_MS) return;
      u.pulseColor  = pr.hasColor  ? pr.color  : RGB{255, 255, 255};
      u.pulseBright = _clampBright(pr.hasBright ? pr.bright : FLTHY_SAFE_MAX_BRIGHT);
      u.pulseDurMs  = pr.hasDur    ? pr.durMs  : 120;
      u.pulseStartMs = now;                            // ALWAYS retrigger (defeats re-send no-op)
      u.pulseLastMs  = now;
      u.pulseActive  = true;
      if (LED_command[hp].LEDFunction < 101 || LED_command[hp].LEDFunction > FLTHY_FX_MAX)
        LED_command[hp].LEDFunction = _fxCode(u.effect);   // ensure a contract render slot runs
      break;
    }

    case CV_CLOCK:                                     // seed/re-anchor the beat-clock
      beatClockSeed(gBeat, pr, now);
      break;

    case CV_BRIGHT: {                                  // master volatile ride (never EEPROM)
      uint8_t b = _clampBright(pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : u.brightBase));
      neoStrips[hp].setBrightness(b);                  // runtime, re-scales at next show() (main.cpp:748)
      break;
    }

    case CV_LEVEL:                                     // meter substitute (HP has no VU)
      u.level = pr.hasLevel ? pr.level : u.level;
      u.lastEnvBright = -1;
      break;

    case CV_STOP:
      u.active = false; u.pulseActive = false;
      scoreClear(gScoreCount[hp], gScoreActive[hp]);   // forget the show (contract_core)
      ledOFF(hp);                                      // blackout (main.cpp:1205)
      LED_command[hp].LEDFunction = 0;                 // do-nothing
      u.lastEnvBright = -1;
      break;

    case CV_MODE:
      if (pr.mode == 's') {                            // show: LED-only interlock
        if (!gInterlocked[hp]) {                       // snapshot what we are about to take (once)
          gSavedTwitchHP[hp] = enableTwitchHP[hp];
          gSavedOffColor[hp] = offcoloroverride[hp];
          gInterlocked[hp]   = true;
        }
        gShowMode = true;
        enableTwitchLED[hp] = 0;                       // no auto LED twitch (main.cpp:968)
        enableTwitchHP[hp]  = false;                   // no auto SERVO twitch (main.cpp:1001) -> LED-only
        offcoloroverride[hp] = true;                   // off state truly black
        u.active = false; u.pulseActive = false;
        ledOFF(hp);
      } else if (pr.mode == 'i') {                     // idle: restore native autonomy
        gShowMode = false;
        enableTwitchLED[hp] = startEnableTwitchLED[hp];// (main.cpp:574)
        if (gInterlocked[hp]) {                        // ONLY undo what show actually took
          enableTwitchHP[hp]   = gSavedTwitchHP[hp];   // NEVER a literal true: this moves a servo
          offcoloroverride[hp] = gSavedOffColor[hp];   // give the native off-color back
          gInterlocked[hp]     = false;
        }
        u.active = false; u.pulseActive = false;
        scoreClear(gScoreCount[hp], gScoreActive[hp]); // the show is over: forget its sections
        LED_command[hp].LEDFunction = 0;
      }
      break;

    case CV_QUERY: {                                   // targeted ack only (contract §8)
      if (p.unit == '*') break;                        // never broadcast Q
      // echo the unit that actually answered — this used to hardcode 'f', so !HRQ and
      // !HTQ both replied "!Hfq:..." and a host could not tell the HPs apart.
      char buf[56];
      snprintf(buf, sizeof(buf), "!H%cq:ver=1.1,phase=2,i=%d,bpm=%u\r\n",
               _unitChar(hp), (int)u.effect, (unsigned)gBeat.bpm);
      Serial.print(buf);
      break;
    }

    default: break;
  }
}

// Fan a parsed command to the addressed jewel(s) per class/unit (fork spec §4).
inline void applyContract(const ParsedContract& p) {
  if (p.cls != 'H' && p.cls != '*') return;            // not a Holoprojector board
  bool all = (p.unit == '*');
  if (all || p.unit == 'F') applyContractToUnit(0, p);
  if (all || p.unit == 'R') applyContractToUnit(1, p);
  if (all || p.unit == 'T') applyContractToUnit(2, p);
  if (p.verb == CV_STOP && all) gBeat.running = false; // !**X also stops the clock
}

// -------------------------------------------------- beat tick + score switch ---
// Call once per loop(), before the per-hp render block (main.cpp ~916).
inline void contractBeatTick() {
  if (!gBeat.running) return;
  BeatPos bp = beatPosAt(gBeat, millis());
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    int idx = scoreActiveIndex(gScore[hp], gScoreCount[hp], bp.beatIndex);
    if (idx >= 0 && idx != gScoreActive[hp]) {
      gScoreActive[hp] = idx;
      // span of this section, for the am=3 build ramp (mirrors ContractLogics.h:308-309);
      // the last section gets a long tail so its span is never zero-width.
      gUnit[hp].sectionStart = gScore[hp][idx].atBeat;
      gUnit[hp].sectionEnd   = (idx + 1 < gScoreCount[hp]) ? gScore[hp][idx + 1].atBeat
                                                           : (gScore[hp][idx].atBeat + 9999);
      _applyScore(hp, gScore[hp][idx]);
    }
  }
}

// The single fork command entry point. main.cpp passes the bytes AFTER '!'.
inline void contractHandle(const char* afterBang) {
  ParsedContract p;
  if (contractParse(afterBang, p)) applyContract(p);
}
