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
// added to the render switch); an ACCENT layers a second look OVER the base and
// auto-restores on ms-expiry (never ledOFF / never flushCommandArray — the native
// flush-to-black). A Studio-seeded beat-clock (verb C) drives an accent envelope,
// and the Phase-2 score (verb A + at=) switches sections on-beat, board-side.
//
// CONTRACT v1.2 — the accent is ONE primitive (_fireAccent) with TWO triggers:
//   * verb P                — live, the Pi mirrors the show beat by beat (Phase 1);
//   * the board's beat edge — the active scored section's ae=/ac=/ad=, fired by
//                             contractBeatTick() with the Pi SILENT (Phase 2).
// Both render through _renderLook(), so a delivered blueprint and a live-mirrored show
// fire the same effect-swap accent on top of the same brightness pump. A score entry
// with no ae= can never arm the overlay: it behaves exactly as it did in v1.1.
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
  ContractRGB            color{0, 0, 0};
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
  // Accent overlay — ONE primitive, TWO triggers (contract v1.2):
  //   * live  (Phase 1): verb P, carrying the overlay in i=/c=/d=;
  //   * score (Phase 2): the board's own beat edge, carrying it in the active section's
  //     ae=/ac=/ad=, so a DELIVERED blueprint fires the same effect-swap accent a
  //     MIRRORED show gets from the Pi. Both go through _fireAccent().
  // It LAYERS over the base look and auto-restores on ms-expiry (never flushes to black).
  bool           pulseActive = false;
  ContractEffect pulseFx     = CE_NONE;   // v1.2: which effect the overlay renders.
                                          // CE_NONE == the v1.1 wire shape (a verb P with no
                                          // i=) and still means a solid full-field fill.
  ContractRGB            pulseColor{255, 255, 255};
  uint8_t        pulseBright  = FLTHY_SAFE_MAX_BRIGHT;
  uint32_t       pulseStartMs = 0;
  uint32_t       pulseDurMs   = 0;
  uint32_t       pulseLastMs  = 0;        // strobe cool-down anchor
  // Once-per-beat guard for the board-side (Phase-2) accent. BEAT_NONE = "no beat accented
  // yet"; every show boundary resets it, so a new show can never inherit a stale edge.
  int32_t        lastAccentBeat = BEAT_NONE;
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
static inline uint32_t _scale(const ContractRGB& c, uint8_t bri) {
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
  ContractRGB c{0, 0, 0};
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
// Render ONE look for unit hp. The BASE look and the ACCENT OVERLAY (verb P, or the
// board-side beat edge) both render through this one switch — so the accent's effect
// vocabulary costs ZERO extra flash and can never drift from the base look's.
// Only the four inputs differ between the two callers:
//   base    : (u.effect,  u.color,      _envBright(hp)  — beat-pumped, u.startMs)
//   overlay : (u.pulseFx, u.pulseColor, u.pulseBright   — the un-pumped CEILING,
//              u.pulseStartMs — the overlay owns its own time origin)
// speed/level/frame counters always come from the unit (an overlay never owns them, which
// is exactly why the STATEFUL effects — scan/sparkle/meter — are barred from ae= in
// contract_core's accentEffectAllowed(): a 180 ms swap-and-restore would corrupt the base
// look's state machine mid-song).
static inline void _renderLook(uint8_t hp, ContractEffect eff, const ContractRGB& color,
                               uint8_t envB, uint32_t startMs) {
  FlthyUnit& u = gUnit[hp];
  uint32_t now = millis();

  switch (eff) {
    case CE_OFF:
      if (u.lastEnvBright != 0) { _fillShow(hp, 0x000000); u.lastEnvBright = 0; }
      break;

    case CE_FLASH: {                                   // strobe-capped toggle
      uint32_t half = (uint32_t)map((long)u.speed, 0, 255, 700, (long)FLTHY_STROBE_MIN_MS);
      if (half < FLTHY_STROBE_MIN_MS) half = FLTHY_STROBE_MIN_MS;
      bool on = ((now - startMs) % (2 * half)) < half;
      // envB (NOT the raw ceiling): CE_FLASH beat-pumps like every other sustained look.
      // The Logics' CE_FLASH was aligned TO this line.
      _fillShow(hp, on ? _scale(color, envB) : 0x000000);
      break;
    }

    case CE_PULSE: {                                   // triangle breathe, peak = brightest
      uint32_t period = (uint32_t)map((long)u.speed, 0, 255, 2000, 240);
      if (period < 2) period = 2;
      uint32_t ph = (now - startMs) % period;
      uint32_t tri = (ph < period / 2) ? (ph * 2 * 255 / period)
                                       : ((period - ph) * 2 * 255 / period);
      // An ODD period's midpoint raw-computes 256, not 255 (e.g. period=241, ph=120 ->
      // 121*510/241 == 256). At full brightness that is 255*256/255 == 256, and the uint8_t
      // cast truncates it to 0 — the BRIGHTEST instant of the breathe would render BLACK.
      // contract_core.h's fxTwinkleBright() computes the same triangle and already carries
      // exactly this guard; this copy of the math was missing it.
      // Today the bug is unreachable, but only by accident: every brightness store goes through
      // _clampBright(), so envB can never exceed FLTHY_SAFE_MAX_BRIGHT (200), and 200*256/255
      // truncates to 200 — the same value the clamp gives. Verified by brute force over the whole
      // (period, ph, envB<=200) space: this clamp changes ZERO rendered values, so nothing moves
      // in the JS visualizer mirror either. Raise that photosensitivity cap toward 255 without
      // this line, though, and the black flash comes back (8 samples at envB=255).
      if (tri > 255u) tri = 255u;
      uint8_t br = (uint8_t)((uint16_t)envB * (uint16_t)tri / 255);
      _fillShow(hp, _scale(color, br));
      break;
    }

    case CE_RAINBOW: {                                 // color ignored (contract §6)
      uint32_t step = (uint32_t)map((long)u.speed, 0, 255, 40, 8);
      if (step < 1) step = 1;
      uint8_t base = (uint8_t)(((now - startMs) / step) & 255);
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
          neoStrips[hp].setPixelColor(i, (i == u.frame) ? _scale(color, envB) : 0x000000);
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
          neoStrips[hp].setPixelColor(i, _scale(color, b));
        }
        neoStrips[hp].show();
      }
      break;
    }

    case CE_METER: {                                   // no VU on HP: solid @ level (fork spec §6/§8)
      uint8_t br = (uint8_t)((uint16_t)envB * (uint16_t)u.level / 255);
      if (u.lastEnvBright != (int)br) { _fillShow(hp, _scale(color, br)); u.lastEnvBright = (int)br; }
      break;
    }

    case CE_COMET: {                                   // comet trail around the 6-jewel ring, center off
      int N = NEO_JEWEL_LEDS - 1;                       // ring positions (center excluded)
      int head = fxHead(now - startMs, u.speed, N);
      neoStrips[hp].setPixelColor(0, 0x000000);         // center off
      for (int p = 0; p < N; p++) {
        uint8_t cb = fxCometBright(p, head, N);
        uint8_t v = (uint8_t)(((uint16_t)envB * cb) / 255);
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), _scale(color, v));
      }
      neoStrips[hp].show();
      break;
    }

    case CE_CHASE: {                                    // marquee chase around the ring, center off
      int N = NEO_JEWEL_LEDS - 1;                         // ring positions (center excluded)
      uint32_t el = now - startMs;
      neoStrips[hp].setPixelColor(0, 0x000000);           // center off
      for (int p = 0; p < N; p++) {
        uint32_t c = fxChaseLit(p, el, u.speed) ? _scale(color, envB) : 0x000000;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), c);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_WIPE: {                                     // ping-pong fill wipe around the ring, center off
      int N = NEO_JEWEL_LEDS - 1;                         // ring positions (center excluded)
      uint32_t el = now - startMs;
      neoStrips[hp].setPixelColor(0, 0x000000);           // center off
      for (int p = 0; p < N; p++) {
        uint32_t c = fxWipeLit(p, el, u.speed, N) ? _scale(color, envB) : 0x000000;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), c);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_GRADIENT: {                                 // hue gradient across the ring, center off
      int N = NEO_JEWEL_LEDS - 1;                         // ring positions (center excluded)
      uint32_t el = now - startMs;
      neoStrips[hp].setPixelColor(0, 0x000000);           // center off
      for (int p = 0; p < N; p++) {
        uint8_t hue = fxGradientHue(p, N, 0, el, u.speed); // base hue 0, color-independent (like rainbow)
        ContractRGB c = fxHsv2rgb(hue, 255, envB);
        uint32_t packed = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), packed);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_COLORCYCLE: {                               // whole-jewel hue rotation (color-independent)
      ContractRGB c = fxHsv2rgb(fxCycleHue(0, now - startMs, u.speed), 255, envB);
      uint32_t packed = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
      _fillShow(hp, packed);
      break;
    }

    case CE_TWINKLE: {                                  // per-px hashed triangle twinkle, whole jewel
      for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++) {
        uint8_t tb = fxTwinkleBright((int)i, now, u.speed);
        uint8_t v = (uint8_t)(((uint16_t)envB * tb) / 255);
        neoStrips[hp].setPixelColor(i, _scale(color, v));
      }
      neoStrips[hp].show();
      break;
    }

    case CE_SOLID:
    default:
      if (u.lastEnvBright != (int)envB) { _fillShow(hp, _scale(color, envB)); u.lastEnvBright = (int)envB; }
      break;
  }
}

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

  // Accent overlay: LAYER it over the base look, auto-restore on ms-expiry.
  // v1.2: it now renders a full EFFECT (pulseFx), not only a solid fill. pulseFx == CE_NONE
  // is the v1.1 wire shape (a verb P with no i=) and takes the CE_SOLID branch, which with
  // the forced repaint below is byte-for-byte the old
  //   _fillShow(hp, _scale(u.pulseColor, u.pulseBright));
  // The overlay rides the unit's brightness CEILING un-pumped (an accent must not dip with
  // the envelope it exists to punctuate) and starts its own timeline at pulseStartMs.
  if (u.pulseActive) {
    if ((uint32_t)(now - u.pulseStartMs) < u.pulseDurMs) {
      u.lastEnvBright = -1;              // the MEMOISING branches (off/meter/solid) skip a
                                         // repaint when the level is unchanged — without this
                                         // the accent would never actually be drawn over a
                                         // same-level base look
      _renderLook(hp, (u.pulseFx == CE_NONE) ? CE_SOLID : u.pulseFx,
                  u.pulseColor, u.pulseBright, u.pulseStartMs);
      return;
    }
    u.pulseActive   = false;             // accent done -> restore the base look next
    u.lastEnvBright = -1;                // force a base repaint
  }

  _renderLook(hp, u.effect, u.color, _envBright(hp), u.startMs);
}

// ------------------------------------------------ the ONE accent-overlay path ---
// BOTH triggers land here: verb P (Phase-1 — the Pi mirrors the show live) and the board's
// own beat edge (Phase-2 — the board plays a delivered blueprint with the Pi silent). One
// primitive, two triggers => a mirrored show and a delivered one fire the SAME accent.
//
// LED-ONLY INVARIANT (fork README §11): this NEVER writes HP_command[*] and NEVER calls
// positionHP / twitchHP / wagHP / RCHP / flushCommandArray, so no holoprojector SERVO can
// move because of an accent. The ONLY board global it touches is LED_command[hp].LEDFunction,
// and only to keep a contract render slot selected (the same guard verb P already used).
// It also deliberately does NOT go through _applyLook() — that is the only place varResets()
// runs — so the base look's frame/frameMs counters survive the accent untouched.
static inline bool _fireAccent(uint8_t hp, ContractEffect fx, const ContractRGB& color,
                               uint8_t bright, uint32_t durMs) {
  FlthyUnit& u = gUnit[hp];
  uint32_t now = millis();
  // Strobe cool-down: >= 2x the min state between accent STARTS => <= ~2.9 accents/sec,
  // inside the <= 3 flashes/sec photosensitivity guidance, and matched to the Logics/PSIs.
  // v1.1 gated on 1x the min state here — with a Phase-2 every-beat accent above ~176 BPM
  // that would have let the jewels strobe at up to 5.9 Hz, the one board of the three that
  // could exceed the guidance.
  if (!strobeCoolDownExpired(u.pulseLastMs, now, 2 * FLTHY_STROBE_MIN_MS)) return false;
  if (durMs < FLTHY_STROBE_MIN_MS) durMs = FLTHY_STROBE_MIN_MS;   // a state shorter than the
  if (durMs > 2550u) durMs = 2550u;                               // min state IS a strobe
  // A stateful/native effect can never become an overlay: it would corrupt the base look's
  // state machine on restore (scan/sparkle/meter), or hand the frame to a renderer we do not
  // own so the expiry check never runs and the board LATCHES (native). Same gate the ae=
  // parser applies — re-asserted here because verb P reaches this straight from the wire's i=.
  u.pulseFx      = accentEffectAllowed(fx) ? fx : CE_SOLID;
  u.pulseColor   = color;
  u.pulseBright  = _clampBright(bright);
  u.pulseDurMs   = durMs;
  u.pulseStartMs = now;                              // ALWAYS retrigger (defeats a re-send no-op)
  u.pulseLastMs  = now;
  u.pulseActive  = true;
  u.lastEnvBright = -1;
  if (LED_command[hp].LEDFunction < 101 || LED_command[hp].LEDFunction > FLTHY_FX_MAX)
    LED_command[hp].LEDFunction = _fxCode(u.effect);  // ensure a contract render slot runs
  return true;
}

// ---------------------------------------------------- apply a look to a unit ---
// Common path for Phase-1 (live A) and Phase-2 (score switch): sets cState + the
// render code. NEVER flushes to black, NEVER writes HP_command (LED-only, §11).
static inline void _applyLook(uint8_t hp, ContractEffect eff, int nativeCode,
                              const ContractRGB& color, uint8_t speed, uint8_t bright,
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
  // v1.2 accent overlay. NO ae= on the line => accentFx stays CE_NONE => the beat-edge
  // trigger bails => a v1.1 entry behaves EXACTLY as it does today (it can never arm the
  // overlay). accentFx was already allow-gated by the parser (never native, never stateful).
  if (pr.hasAccentFx) {
    e.accentFx    = pr.accentFx;
    // Resolved HERE, not at fire time: the entry deliberately carries no has-flag, so an
    // absent ac= means the accent inherits the SECTION's colour.
    e.accentColor = pr.hasAccentColor ? pr.accentColor : e.color;
    // Stored /10 ms so the entry grows by 1 byte, not 4 (AVR SRAM). ad= is clamped to <=2550
    // at parse; ad=0..9 would truncate to a zero-length accent, so it reads as the default.
    e.accentDur10 = (uint8_t)((pr.hasAccentDur ? pr.accentDurMs : 180u) / 10u);
    if (e.accentDur10 == 0) e.accentDur10 = 18;          // 180 ms
  }
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
        u.lastAccentBeat = BEAT_NONE;                  // a NEW show is loading: never inherit
                                                       // the last one's beat edge, or its first
                                                       // accent beat gets swallowed
        return;
      }
      // Phase-1: apply now. Omitting i= patches params of the current look.
      ContractEffect eff = pr.hasEffect ? pr.effect : u.effect;
      ContractRGB   col   = pr.hasColor   ? pr.color   : u.color;
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
      // Phase-1 live accent, from the Pi. v1.2: i= now selects the overlay EFFECT (an i-less
      // P is still exactly today's solid fill — no v1.1 client ever sent one). Same
      // _fireAccent() the board-side score edge uses: ONE overlay primitive, two triggers,
      // so a mirrored show and a delivered blueprint cannot look different.
      //
      // ALLOW-GATE THE WIRE'S i= HERE (parity with the Logics and the PSI, which gate at
      // their call sites too). The SCORED accent is gated at PARSE time — ae= never stores a
      // rejected effect — but verb P hands the wire's i= straight to the overlay, so this is
      // the one path a stateful/native effect can reach it by. Rejected => CE_SOLID, i.e. the
      // v1.1 solid fill, never "no accent": a P the operator sent must still punctuate.
      //   * scan/sparkle/meter are STATEFUL — they share u.frame/u.frameMs with the BASE look,
      //     so a 180 ms swap-and-restore corrupts the base look's state machine mid-song.
      //   * native:<n> hands the frame to a renderer we do not own. contractRenderHP() — and
      //     therefore the overlay's EXPIRY CHECK — only runs from LEDFunction 101..FLTHY_FX_MAX,
      //     so a native overlay could never expire and would LATCH the jewel.
      // _fireAccent() re-asserts the same gate. BOTH are load-bearing: this one keeps the bad
      // effect out of the overlay primitive, that one covers every other caller of it. Neither
      // is redundant belt-and-braces — see the CV_PULSE gate check in test/host/run.sh.
      ContractEffect fx = (pr.hasEffect && accentEffectAllowed(pr.effect)) ? pr.effect : CE_SOLID;
      _fireAccent(hp,
                  fx,
                  pr.hasColor  ? pr.color  : ContractRGB{255, 255, 255},
                  pr.hasBright ? pr.bright : u.brightBase,   // the unit's own CEILING, not a
                                                             // literal 200: an accent must land
                                                             // at the same level on all 3 boards
                  pr.hasDur    ? pr.durMs  : 120);           // clamped up to the min state inside
      break;
    }

    case CV_CLOCK:                                     // seed/re-anchor the beat-clock
      beatClockSeed(gBeat, pr, now);
      u.lastAccentBeat = BEAT_NONE;                    // the beat ORIGIN just moved (Studio
                                                       // re-anchors on every Play/seek), so a
                                                       // beat index from the OLD timeline must
                                                       // not gate the new one
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
      u.lastAccentBeat = BEAT_NONE;                    // show boundary: drop the beat edge too
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
        // v=show is the FIRST line of Studio's load burst, and Resend re-sends that burst with
        // no intervening X — so a new show starts HERE and must not inherit the last one's
        // sections (they would merge, and past FLTHY_SCORE_CAP the new ones would be dropped).
        scoreClear(gScoreCount[hp], gScoreActive[hp]);
        u.lastAccentBeat = BEAT_NONE;                  // ...nor its beat edge
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
        u.lastAccentBeat = BEAT_NONE;                  // ...and its beat edge
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

// ------------------------------ beat tick + score switch + board-side accent ---
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

    // ---- v1.2: BOARD-SIDE ACCENT SELF-FIRE (Phase 2, the Pi is silent) ----------------
    // The gap this closes: before v1.2 a DELIVERED blueprint's only beat expression was the
    // brightness pump (envBright), so it could not fire the effect-swap accent that a LIVE,
    // Pi-mirrored show got from verb P. Now the board fires the SAME overlay itself, from
    // the active section's ae=/ac=/ad=. The section switch above runs FIRST on purpose: a
    // section that starts on a downbeat gets its accent on that very beat.
    //
    // ORDER MATTERS. beatEdge() advances the guard UNCONDITIONALLY, so it must be consumed
    // before any bail-out below — a beat that does NOT accent still has to consume its edge,
    // or the next tick (still inside the same beat) re-tests and re-fires on every frame,
    // turning a 180 ms accent into a permanent latch.
    if (!beatEdge(gUnit[hp].lastAccentBeat, bp.beatIndex)) continue;   // same beat: done
    int a = gScoreActive[hp];
    if (a < 0) continue;                                     // no scored section is playing:
                                                             // a LIVE look never self-accents
                                                             // (the Pi's verb P owns Phase 1)
    const ScoreEntry& e = gScore[hp][a];
    if (e.accentFx == CE_NONE) continue;                     // v1.1 entry: NEVER accents
    // The SAME predicate the brightness pump gates on (contract_core's beatAccentFires, which
    // beatAccentAmount() also calls) — so the pump and the effect accent can NEVER disagree
    // about which beats carry an accent.
    if (!beatAccentFires(e.accentMode, bp.barPos)) continue;
    _fireAccent(hp, e.accentFx, e.accentColor, gUnit[hp].brightBase,
                (uint32_t)e.accentDur10 * 10u);
  }
}

// The single fork command entry point. main.cpp passes the bytes AFTER '!'.
inline void contractHandle(const char* afterBang) {
  ParsedContract p;
  if (contractParse(afterBang, p)) applyContract(p);
}
