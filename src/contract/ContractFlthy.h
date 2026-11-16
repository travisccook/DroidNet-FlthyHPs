// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// src/contract/ContractFlthy.h — Flthy Holoprojectors firmware layer for the
// Driveable-Animation Contract (v1.3). LED-ONLY: the '!' path NEVER writes HP_command[*]
// and NEVER calls positionHP/twitchHP/wagHP/RCHP — no servo motion, ever. It never writes
// the operator's settings either (enableTwitchLED[], enableTwitchHP[], offcoloroverride[]):
// an armed show HOLDS a unit, and main.cpp's two auto-twitch gates ask contractHolds() and
// contractHoldsLeds(). Letting go of a unit re-arms both twitch timers first, so the pass
// that handles a '!' line never starts a servo twitch. test/host/sim checks both on the
// real main.cpp, on every simulated pass.
//
// WHAT LIVES WHERE. Everything a show owns on one unit — armed or not, the look, the overlay,
// the score cursor, the load check, the watchdog — is the shared core's ContractUnitState
// (contract_core.h), with the core's one reset, so the three DroidNet forks cannot differ on
// safety, clock or lifecycle. This file adds only what is this board's: how a unit's base is
// put on a NeoPixel jewel through the stock LED command, the render of each effect on seven
// pixels, the strip-brightness ceiling, and handing the jewel back to the stock firmware.
//
// INCLUDE ORDER (see main.cpp): include this AFTER the board globals + effect
// primitives it references are declared, and BEFORE loop(). It relies on these
// being already declared in src/main.cpp (named, not numbered: line numbers there move):
//   Adafruit_NeoPixel neoStrips[HPCOUNT];
//     .setPixelColor(uint16_t,uint32_t)  .show()  .setBrightness(uint8_t)  .getBrightness()  .getPixels()
//   LEDCmd  LED_command[HPCOUNT];  (.LEDFunction byte)
//   (.LEDOption1, .LEDOption2, .LEDHalt: written only to hand the LED side back, as the stock flush does)
//   const int offcolor[HPCOUNT]; boolean offcoloroverride[HPCOUNT];      (read only)
//   unsigned long twitchLEDTime[HPCOUNT]; const unsigned int LEDTwitchInterval[HPCOUNT][2];
//   void resetHPtwitch(byte);    // sets twitchHPTime[hp] a full interval ahead; moves nothing
//   void ledOFF(byte);
//   void varResets(byte);
//   #define HPCOUNT 3 / #define NEO_JEWEL_LEDS 7
//   millis(), random(), Serial                         // Arduino core
//
// DESIGN: arbitrary RGB is injected straight into the NEO_GRB jewels via
// setPixelColor(i, 0xRRGGBB) (the library takes a packed uint32_t),
// bypassing the palette table entirely (basicColors/dimColorVal). Each contract
// effect writes 0xRRGGBB pre-scaled by a per-write brightness multiplier. The envelope never
// uses setBrightness(): that rescales the whole RAM buffer at once, lossily, and
// setPixelColor() already scales every write by the strip's brightness. The per-unit look
// renders through LED_command[hp].LEDFunction = 100 + effectId (cases 101..115,
// added to the render switch); an ACCENT layers a second look OVER the base and
// auto-restores on ms-expiry. A host-seeded beat-clock (verb C) drives an accent envelope,
// and the score (verb A + at=) switches sections on-beat, board-side.
//
// The accent is ONE primitive (_fireAccent) with TWO triggers:
//   * verb P                — live, the host mirrors the show beat by beat;
//   * the board's beat edge — the active scored section's ae=/ac=/ad=, fired by
//                             contractBeatTick() with the host SILENT.
// Both render through _renderLook(), so a delivered show and a live-mirrored one fire the
// same effect-swap accent on top of the same brightness pump. A score entry with no ae=
// can never arm the overlay.
#pragma once
// The core's name tables and reply text stay in flash on the AVR (contract_core.h, "name tables,
// and where they live"). test/host/run.sh stage 1g runs the core's tests with stand-ins that make
// a direct read of such a string fault.
#if defined(__AVR__)
#include <avr/pgmspace.h>
#define CONTRACT_PSTR(s)      PSTR(s)
#define CONTRACT_READ_BYTE(p) pgm_read_byte(p)
#endif
#include "contract_core.h"
#include "../wdp/wdp_fork.h" // wdpFinish() (verb Q ack line)
#include <stdio.h>          // snprintf (verb Q ack line)

// -------------------------------------------------------------- constants ----
static const uint8_t  FLTHY_FX_BASE         = 100;   // LEDFunction = 100 + effectId (101..115)
#ifdef NEO_JEWEL_RGBW
static const uint8_t  FLTHY_PX_BYTES        = 4;     // raw bytes a pixel
#else
static const uint8_t  FLTHY_PX_BYTES        = 3;
#endif
static const uint8_t  FLTHY_SCORE_CAP       = 8;     // scored sections a unit holds

// contract effectId -> LEDFunction render code (CE_OFF..CE_TWINKLE map to 101..115)
static inline byte _fxCode(ContractEffect e) { return (byte)(FLTHY_FX_BASE + (uint8_t)e); }
// hp index -> the contract unit letter it answers as (Front/Rear/Top)
static inline char _unitChar(uint8_t hp) { return (hp == 0) ? 'F' : ((hp == 1) ? 'R' : 'T'); }
// upper bound of the contract render-slot range (main.cpp's render switch gates on this;
// bump it here, not with a fresh literal, when a later task adds an effect after CE_TWINKLE).
static const uint8_t  FLTHY_FX_MAX          = _fxCode(CE_TWINKLE);   // 115

// ----------------------------------------------------------- per-unit state ---
struct FlthyUnit {
  ContractUnitState s;                    // everything a show owns on this unit (shared core)
  // ---- this board's own
  uint8_t  ride        = 255;             // verb B: a fraction of s.ceil on the STRIP's brightness
  uint8_t  level       = 0;               // last verb-L value (meter substitute)
  uint8_t  pulseBright = 255;             // the overlay's level, a fraction like s.bright
  int      lastEnvBright = -1;            // memo for the looks that repaint only on a change
  uint32_t frameMs     = 0;               // scan/sparkle self-throttle
  uint8_t  frame       = 0;
  // L3: a verb P with no show armed overlays the STOCK look and gives it back afterwards.
  bool     overNative  = false;
  byte     savedFn     = 255;             // the stock LEDFunction under the overlay
  uint8_t  savedPx[NEO_JEWEL_LEDS * 4];   // its frame, raw (RGB uses 21 of the 28 bytes)
};

static FlthyUnit  gUnit[HPCOUNT];
static BeatClock  gBeat;                  // ONE clock for the three units
// The score table is the fork's (sized by its cap); the cursor and the count are in s.
static ScoreEntry gScore[HPCOUNT][FLTHY_SCORE_CAP];

// ---------------------------------------------------------------- helpers ----
// Is the LED render slot one of ours (101..115)?
static inline bool _slotIsOurs(uint8_t hp) {
  byte f = LED_command[hp].LEDFunction;
  return f >= 101 && f <= FLTHY_FX_MAX;
}
// Is the native look this layer started (i=native:<n>) still the one on the jewel?
static inline bool _nativeIsUp(uint8_t hp) {
  const ContractUnitState& s = gUnit[hp].s;
  return s.base == CB_NATIVE && LED_command[hp].LEDFunction == (byte)s.nativeCode;
}
// main.cpp's two auto-twitch gates. The SERVO twitch waits from the moment a show is armed
// (this fork is LED-only); the LED twitch waits once the show has begun (its first C, live A
// or P: until then the stock idle look stays) or while a contract look or overlay is up.
inline bool contractHolds(uint8_t hp) {
  const ContractUnitState& s = gUnit[hp].s;
  return s.inShow || s.base == CB_LOOK || s.pulseActive;
}
inline bool contractHoldsLeds(uint8_t hp) {
  const ContractUnitState& s = gUnit[hp].s;
  return (s.inShow && s.base != CB_NONE) || s.base == CB_LOOK || s.pulseActive;
}
// Does this layer have the unit's LEDs (so a reset must hand them back)?
static inline bool _heldLeds(uint8_t hp) { return contractHoldsLeds(hp) || _slotIsOurs(hp) || _nativeIsUp(hp); }
// Both auto-twitch timers a full interval from now. Whenever the layer lets go of a unit the
// timers are long overdue, and without this the stock loop twitches the LEDs AND THE SERVOS
// in the same pass. resetHPtwitch() only sets twitchHPTime[hp]; it moves nothing.
static inline void _rearmTwitch(uint8_t hp) {
  twitchLEDTime[hp] = (1000L * random(LEDTwitchInterval[hp][0], LEDTwitchInterval[hp][1])) + millis();
  resetHPtwitch(hp);
}
// Give the LED side back to the stock firmware: what its own flush does for the LED command
// (off colour if one is set and not overridden, else dark), then re-arm the timers.
static inline void _nativeLedIdle(uint8_t hp) {
  bool off = (offcolor[hp] >= 0 && !offcoloroverride[hp]);
  LED_command[hp].LEDFunction = off ? 100 : 255;
  LED_command[hp].LEDOption1  = off ? (byte)offcolor[hp] : 255;
  LED_command[hp].LEDOption2  = 255;
  LED_command[hp].LEDHalt     = -1;
  ledOFF(hp);
  _rearmTwitch(hp);
}
// Dark, with nothing of the stock firmware's left running: 255 is its "idle, dark" value
// (never 0: that is "do nothing", which also blocks the stock auto twitch for good), and the
// |<n> / auto-twitch timer that would flush the jewel later is cancelled.
static inline void _dark(uint8_t hp) {
  LED_command[hp].LEDFunction = 255;
  LED_command[hp].LEDHalt     = -1;
  ledOFF(hp);
}
// v1.3 S7: the native looks this layer may start are the core's nativeAllowedFlthy() list. They
// need no option bytes and no counters: 1 = Leia (fixed blue colour projector), 6 = rainbow.
// v1.3 S6: the brightness ceiling is the STRIP's own level when the show arms (BRIGHT, or what
// H<unit>B<level> last set); it is kept in s.ceil. setPixelColor() applies the strip's level to
// every write, so b= (s.bright) is used as the fraction it is and never multiplied by s.ceil.
// Verb B rides the strip below the ceiling and never outlives the show.
static inline void _restoreRide(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  if (u.ride != 255) neoStrips[hp].setBrightness(u.s.ceil);
  u.ride = 255;
}
// The native brightness command (H<unit>B<level>) calls this: the owner's new level is the
// ceiling from now on, and any verb-B ride is over.
inline void contractOwnerBrightness(uint8_t hp, uint8_t level) {
  gUnit[hp].s.ceil = level;
  gUnit[hp].ride = 255;
  gUnit[hp].lastEnvBright = -1;                         // memoised looks repaint at the new level
}
// Put the unit's base on its jewel (no overlay up).
static inline void _showBase(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  const ContractUnitState& s = u.s;
  u.lastEnvBright = -1;                                 // force the first repaint
  if (s.base == CB_LOOK) {
    LED_command[hp].LEDFunction = _fxCode(s.effect);
    LED_command[hp].LEDHalt     = -1;                   // a stock |<n> or auto-twitch timer still
  } else if (s.base == CB_NATIVE) {                     // running would flush this look to black
    LED_command[hp].LEDFunction = (byte)s.nativeCode;   // allow-listed: needs no option bytes
    LED_command[hp].LEDHalt     = -1;
  } else if (s.base == CB_DARK) {
    _dark(hp);
  } else {
    _nativeLedIdle(hp);                                 // CB_NONE: the stock firmware's again
  }
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
// self-contained rainbow wheel (mirrors native Wheel()) so a verb-P
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
// accent rides it back up on the beat. am=0 => flat s.bright.
// Level math lives in contract_core's envBright() — the SHARED envelope all three
// boards render through (b= is the ceiling, m= is the dip depth). Do not reintroduce
// a board-local floor here: it desyncs the HPs from the Logics/PSIs on every cue.
static inline uint8_t _envBright(uint8_t hp) {
  const ContractUnitState& s = gUnit[hp].s;
  if (!gBeat.running || s.beatMod == 0 || s.accentMode == 0) return s.bright;
  BeatPos bp = beatPosAt(gBeat, millis());
  // am=3 (build) ramps across the section; the span is only known while a scored
  // section is active, so a stale span from a finished show can't leak in here.
  const float prog = (s.curSection >= 0) ? beatSectionProgress(bp.beatIndex, s.sectionStart, s.sectionEnd) : 0.0f;
  return envBright(s.bright, s.beatMod, beatAccentAmount(s.accentMode, bp, s.beatMod, prog, gBeat, s.accentEvery));
}

// --------------------------------------------------- the single render fn -----
// Render ONE look for unit hp. The BASE look and the ACCENT OVERLAY (verb P, or the
// board-side beat edge) both render through this one switch — so the accent's effect
// vocabulary costs ZERO extra flash and can never drift from the base look's.
// Only the four inputs differ between the two callers:
//   base    : (s.effect,  s.color,      _envBright(hp)  — beat-pumped, s.startMs)
//   overlay : (s.pulseFx, s.pulseColor, u.pulseBright   — un-pumped,
//              s.pulseStartMs — the overlay owns its own time origin)
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
      uint32_t half = (uint32_t)map((long)u.s.speed, 0, 255, 700, (long)STROBE_MIN_STATE_MS);
      if (half < STROBE_MIN_STATE_MS) half = STROBE_MIN_STATE_MS;
      bool on = ((now - startMs) % (2 * half)) < half;
      // envB (NOT the raw ceiling): CE_FLASH beat-pumps like every other sustained look.
      // The Logics' CE_FLASH was aligned TO this line.
      _fillShow(hp, on ? _scale(color, envB) : 0x000000);
      break;
    }

    case CE_PULSE: {                                   // triangle breathe, peak = brightest
      uint32_t period = (uint32_t)map((long)u.s.speed, 0, 255, 2000, 240);
      // v1.3 S8: the pulse's full cycle is never shorter than 2 x the min state (340 ms).
      // Only s >= 242 is affected.
      if (period < 2 * STROBE_MIN_STATE_MS) period = 2 * STROBE_MIN_STATE_MS;
      uint32_t ph = (now - startMs) % period;
      uint32_t tri = (ph < period / 2) ? (ph * 2 * 255 / period)
                                       : ((period - ph) * 2 * 255 / period);
      // An ODD period's midpoint raw-computes 256, not 255 (e.g. period=241, ph=120 ->
      // 121*510/241 == 256). At full brightness that is 255*256/255 == 256, and the uint8_t
      // cast truncates it to 0 — the BRIGHTEST instant of the breathe would render BLACK.
      // contract_core.h's fxTwinkleBright() computes the same triangle and already carries
      // exactly this guard; this copy of the math was missing it.
      // While brightness was capped at 200 this was unreachable (200*256/255 truncates to 200).
      // Since v1.3 S6 the level is a fraction of the strip's own brightness and envB reaches 255,
      // so this clamp is what keeps the peak from wrapping to black.
      if (tri > 255u) tri = 255u;
      uint8_t br = (uint8_t)((uint16_t)envB * (uint16_t)tri / 255);
      _fillShow(hp, _scale(color, br));
      break;
    }

    case CE_RAINBOW: {                                 // color ignored (contract §6)
      uint32_t step = (uint32_t)map((long)u.s.speed, 0, 255, 40, 8);
      if (step < 1) step = 1;
      uint8_t base = (uint8_t)(((now - startMs) / step) & 255);
      for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++)
        neoStrips[hp].setPixelColor(i, _wheel((uint8_t)((i * 256 / NEO_JEWEL_LEDS) + base), envB));
      neoStrips[hp].show();
      break;
    }

    case CE_SCAN: {                                    // dot walks ring 1..6, center px0 off
      uint32_t step = (uint32_t)map((long)u.s.speed, 0, 255, 160, 30);
      if ((uint32_t)(now - u.frameMs) > step) {
        u.frameMs = now;
        neoStrips[hp].setPixelColor(0, 0x000000);      // center always off
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
      uint32_t step = (uint32_t)map((long)u.s.speed, 0, 255, 150, 40);
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
      int head = fxHead(now - startMs, u.s.speed, N);
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
        uint32_t c = fxChaseLit(p, el, u.s.speed) ? _scale(color, envB) : 0x000000;
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
        uint32_t c = fxWipeLit(p, el, u.s.speed, N) ? _scale(color, envB) : 0x000000;
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
        uint8_t hue = fxGradientHue(p, N, 0, el, u.s.speed); // base hue 0, color-independent (like rainbow)
        ContractRGB c = fxHsv2rgb(hue, 255, envB);
        uint32_t packed = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
        neoStrips[hp].setPixelColor((uint16_t)(p + 1), packed);
      }
      neoStrips[hp].show();
      break;
    }

    case CE_COLORCYCLE: {                               // whole-jewel hue rotation (color-independent)
      ContractRGB c = fxHsv2rgb(fxCycleHue(0, now - startMs, u.s.speed), 255, envB);
      uint32_t packed = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
      _fillShow(hp, packed);
      break;
    }

    case CE_TWINKLE: {                                  // per-px hashed triangle twinkle, whole jewel
      for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++) {
        uint8_t tb = fxTwinkleBright((int)i, now, u.s.speed);
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

// Overlay expiry. Over a contract look the base repaints; over the STOCK look (L3) the stock
// function and its frame come back exactly as they were; over a dark baseline it goes dark.
static inline void _expireOverlay(uint8_t hp, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  if (!s.pulseActive || (uint32_t)(now - s.pulseStartMs) < s.pulseDurMs) return;
  s.pulseActive   = false;
  u.lastEnvBright = -1;                                  // force a base repaint
  if (!_slotIsOurs(hp)) { u.overNative = false; return; }   // the stock firmware took the jewel meanwhile
  if (u.overNative) {
    u.overNative = false;
    LED_command[hp].LEDFunction = u.savedFn;
    memcpy(neoStrips[hp].getPixels(), u.savedPx, (size_t)NEO_JEWEL_LEDS * FLTHY_PX_BYTES);
    neoStrips[hp].show();
  } else if (s.base != CB_LOOK) {
    _showBase(hp);                                       // nothing of ours underneath: dark
  }
}

// Called from the LED render switch (cases 101..115) once per loop for unit hp.
inline void contractRenderHP(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  uint32_t now = millis();

  // Accent overlay: LAYER it over the base look, auto-restore on ms-expiry. It renders a
  // full EFFECT (pulseFx); CE_NONE is a verb P with no i= and takes the CE_SOLID branch.
  // The overlay is un-pumped (an accent must not dip with the envelope it exists to
  // punctuate) and starts its own timeline at pulseStartMs.
  _expireOverlay(hp, now);
  if (!_slotIsOurs(hp)) return;                          // the overlay just gave the jewel back
  if (s.pulseActive) {
    u.lastEnvBright = -1;                // the MEMOISING branches (off/meter/solid) skip a
                                         // repaint when the level is unchanged — without this
                                         // the accent would never actually be drawn over a
                                         // same-level base look
    _renderLook(hp, (s.pulseFx == CE_NONE) ? CE_SOLID : s.pulseFx,
                s.pulseColor, u.pulseBright, s.pulseStartMs);
    return;
  }
  if (s.base != CB_LOOK) { _showBase(hp); return; }      // a contract slot with no contract look: put the base back
  _renderLook(hp, s.effect, s.color, _envBright(hp), s.startMs);
}

// ------------------------------------------------ the ONE accent-overlay path ---
// BOTH triggers land here: verb P (the host mirrors the show live) and the board's own beat
// edge (the board plays a delivered score with the host silent). One primitive, two
// triggers => a mirrored show and a delivered one fire the SAME accent.
//
// LED-ONLY INVARIANT: this NEVER writes HP_command[*] and NEVER calls
// positionHP / twitchHP / wagHP / RCHP / flushCommandArray, so no holoprojector SERVO can
// move because of an accent. The ONLY board global it touches is LED_command[hp].LEDFunction,
// and only to keep a contract render slot selected.
//
// STROBE CAP (SAFETY — photosensitivity, enforced for BOTH triggers): an overlay may not
// RESTART more often than every 2 * STROBE_MIN_STATE_MS (340 ms => <= 2.94 a second), and it
// lasts at least STROBE_MIN_STATE_MS and at most ACCENT_DUR_MAX_MS (accentDurClamp). The gate
// anchors on s.pulseLastMs, which the core's show reset keeps: a reset is not a way around it.
static inline bool _fireAccent(uint8_t hp, ContractEffect fx, const ContractRGB& color,
                               uint8_t bright, uint32_t durMs) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  uint32_t now = millis();
  if (!strobeCoolDownExpired(s.pulseLastMs, now, 2 * STROBE_MIN_STATE_MS)) return false;
  // v1.3 section 6: no accent on a native section in R1, so an accent can never restart or
  // replace a native sequence (R2 adds ae=kick where the look can be scaled).
  if (_nativeIsUp(hp)) return false;
  // A stateful/native effect can never become an overlay: it would corrupt the base look's
  // state machine on restore (scan/sparkle/meter), or hand the frame to a renderer we do not
  // own so the expiry check never runs and the board LATCHES (native). Same gate the ae=
  // parser applies — re-asserted here because verb P reaches this straight from the wire's i=.
  s.pulseFx      = accentEffectAllowed(fx) ? fx : CE_SOLID;
  s.pulseColor   = color;
  u.pulseBright  = bright;
  s.pulseDurMs   = accentDurClamp(durMs);                // a state shorter than the min state IS a strobe
  s.pulseStartMs = now;                                  // ALWAYS retrigger (defeats a re-send no-op)
  s.pulseLastMs  = now;                                  // the cool-down anchor: set on every accent that ARMS
  s.pulseActive  = true;
  u.lastEnvBright = -1;
  if (!_slotIsOurs(hp)) {
    if (!s.inShow && !u.overNative) {                    // L3: no show armed -> overlay the STOCK look
      u.overNative = true;
      u.savedFn    = LED_command[hp].LEDFunction;
      memcpy(u.savedPx, neoStrips[hp].getPixels(), (size_t)NEO_JEWEL_LEDS * FLTHY_PX_BYTES);
    }
    LED_command[hp].LEDFunction = _fxCode(CE_OFF);       // ensure a contract render slot runs
  }
  return true;
}

// ------------------------------------------------------- applying a look -------
// The counters the two stateful looks keep (and the stock ones varResets() clears, with a
// blackout) start over when such a look starts.
static inline void _freshCounters(uint8_t hp, ContractEffect eff) {
  if (eff != CE_SCAN && eff != CE_SPARKLE) return;
  gUnit[hp].frame = 1; gUnit[hp].frameMs = 0;
  varResets(hp);
}
// A live look: now, or when its S4 hold ends. NEVER writes HP_command (LED-only).
static inline void _applyPatch(uint8_t hp, ContractLookPatch lp, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  contractShowBegin(s);                                 // L1: the show's first live A (the look below replaces the dark baseline)
  lookPatchDefault(lp, s.base);                         // L5: no i= and no look yet => i=solid
  if (lp.has & LP_COLOR)   s.color = lp.color;
  if (lp.has & LP_SPEED)   s.speed = lp.speed;
  if (lp.has & LP_BRIGHT)  s.bright = lp.bright;        // a fraction of the ceiling (S6)
  if (lp.has & LP_BEATMOD) s.beatMod = lp.beatMod;
  if (lp.has & LP_AM)      s.accentMode = lp.accentMode;
  if (lp.has & LP_EVERY)   s.accentEvery = lp.every;
  s.startMs = now;
  s.pulseActive = false;
  u.overNative  = false;                                // a look replaces whatever an overlay sat on
  if (lp.has & LP_EFFECT) {
    s.effect = lp.effect; s.nativeCode = lp.nativeCode;
    s.base = (lp.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;   // the caller dropped any code off the allow-list
    _freshCounters(hp, lp.effect);
  }
  _showBase(hp);
}

// build a ScoreEntry from a parsed scored look
static inline ScoreEntry _entryFrom(const ContractParams& pr) {
  ScoreEntry e;
  e.atBeat     = pr.atBeat;
  e.effect     = pr.hasEffect ? pr.effect : CE_SOLID;
  e.color      = pr.hasColor ? pr.color : ContractRGB{0, 0, 0};
  e.speed      = pr.hasSpeed ? pr.speed : 128;
  e.bright     = pr.hasBright ? pr.bright : 255;         // absent: the ceiling
  e.beatMod    = pr.hasBeatMod ? pr.beatMod : 0;
  e.accentMode = pr.hasAm ? pr.accentMode : 0;
  e.accentEvery = accentEveryFrom(pr);
  e.nativeCode = pr.hasEffect ? pr.nativeCode : -1;      // thread the native code into the score
  // The accent the board fires ITSELF on this section's accent beats. No ae= => accentFx stays
  // CE_NONE and the beat-edge trigger never arms the overlay for it. accentFx was already
  // allow-gated by the parser (never native, never stateful).
  if (pr.hasAccentFx) {
    e.accentFx    = pr.accentFx;
    // Resolved HERE, not at fire time: the entry deliberately carries no has-flag, so an
    // absent ac= means the accent inherits the SECTION's colour.
    e.accentColor = pr.hasAccentColor ? pr.accentColor : e.color;
    // Stored /10 ms (one byte). ad= is clamped to <=2550 at parse; ad=0..9 would truncate to a
    // zero-length accent, so it reads as the default.
    uint16_t t10 = (uint16_t)((pr.hasAccentDur ? pr.accentDurMs : ACCENT_DUR_DEFAULT_MS) / 10u);
    if (t10 == 0) t10 = (uint16_t)(ACCENT_DUR_DEFAULT_MS / 10u);
    e.accentDur10 = (uint8_t)(t10 > 255u ? 255u : t10);
  }
  return e;
}

// switch a unit to a scored section (on its beat), clean-starting the look.
static inline void _enterSection(uint8_t hp, int idx, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  const ScoreEntry& e = gScore[hp][idx];
  s.effect = e.effect; s.nativeCode = e.nativeCode; s.color = e.color; s.speed = e.speed;
  s.bright = e.bright;
  s.beatMod = e.beatMod; s.accentMode = e.accentMode; s.accentEvery = e.accentEvery;
  s.curSection = (int8_t)idx; s.startMs = now; s.sectionStart = e.atBeat;
  // the last section gets a long tail so its span (the am=3 build ramp) is never zero-width
  s.sectionEnd = (idx + 1 < s.scoreLen) ? gScore[hp][idx + 1].atBeat : (e.atBeat + 9999);
  s.pulseActive = false;
  u.overNative  = false;
  s.base = (e.effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK;
  _freshCounters(hp, e.effect);
  _showBase(hp);
}

// ------------------------------------------------------------- show reset (L1) ---
// L1: this board has ONE clock for three units. A reset stops it when no unit still holds a
// score or a look.
static inline void _stopClockIfUnused() {
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) if (contractUnitNeedsClock(gUnit[hp].s)) return;
  gBeat = BeatClock();
}
// The three show boundaries: 's' M:v=show, 'x' X, 'i' M:v=idle (and, later, the watchdog and
// a failed verb V). Each runs the core's ONE reset, then puts the result on the jewel:
//   show  arms. The servo twitch waits from now. The stock idle look stays until the show's
//         first C, live A or P (contractShowBegin); a Resend over a running show goes back
//         to the stock idle look until then.
//   x     blacks out and stays in its mode. Outside a show the jewel is the stock firmware's
//         again, dark for one full twitch interval.
//   idle  gives the jewel back: its off colour, both twitch timers a full interval ahead. A
//         unit whose LEDs this layer does not have keeps its stock look, so a stray idle
//         cannot flush what the operator started; if it was armed, only the servo timer is
//         re-armed, so the overdue servo twitch cannot fire in this pass.
static inline void _boundary(uint8_t hp, char kind, uint8_t wdSec) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  const bool heldLeds = _heldLeds(hp), armed = s.inShow;
  _restoreRide(hp);                                     // B never outlives a show
  const uint8_t ceiling = neoStrips[hp].getBrightness();   // S6: the owner's level, right now
  if (kind == 's')      contractOnShow(s, ceiling, wdSec);
  else if (kind == 'x') contractOnStop(s, ceiling);
  else                  contractOnIdle(s, ceiling);
  u.overNative = false;  u.level = 0;  u.pulseBright = 255;  u.lastEnvBright = -1;
  _stopClockIfUnused();
  if (kind == 'x') {
    _dark(hp);
    if (!s.inShow) { s.base = CB_NONE; _rearmTwitch(hp); }
  } else if (heldLeds) {
    _nativeLedIdle(hp);
  } else if (kind == 'i' && armed) {
    resetHPtwitch(hp);
  }
}

// ------------------------------------------------------------- verb dispatch ---
inline void applyContractToUnit(uint8_t hp, const ParsedContract& p, uint16_t lineCrc = 0) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  const ContractParams& pr = p.params;
  uint32_t now = millis();

  switch (p.verb) {
    case CV_ANIMATE: {
      // v1.3 S7: a line naming a native code off the allow-list is dropped whole, live or
      // scored (a dropped scored line is not held, so verb V reports the load bad).
      if (pr.hasEffect && pr.effect == CE_NATIVE && !nativeAllowedFlthy(pr.nativeCode)) return;
      if (pr.hasAt) {                                  // a scored look: store it, do not apply it now
        scoreAccept(s, gScore[hp], FLTHY_SCORE_CAP, _entryFrom(pr), lineCrc);   // a full table marks the load bad
        s.lastAccentBeat = accentEdgeAfterSeed(gBeat, now);   // C1 (BEAT_NONE while no clock runs)
        return;                                        // the jewel is NOT taken at load: the tick does that on the beat
      }
      // v1.3 L4: d= on A is retired; a look holds until the next look, X or idle.
      ContractLookPatch lp = lookPatchFrom(pr);
      if (liveLookOffer(s, lp, now)) _applyPatch(hp, lp, now);   // S4: else held; contractBeatTick() applies it
      break;
    }

    case CV_PULSE: {
      // The accent overlay, live. i= selects the overlay EFFECT (an i-less P is a solid fill).
      // Same _fireAccent() the board-side score edge uses: ONE overlay primitive, two triggers.
      //
      // ALLOW-GATE THE WIRE'S i= HERE (parity with the Logics and the PSI, which gate at
      // their call sites too). The SCORED accent is gated at PARSE time — ae= never stores a
      // rejected effect — but verb P hands the wire's i= straight to the overlay, so this is
      // the one path a stateful/native effect can reach it by. Rejected => CE_SOLID, never
      // "no accent": a P the operator sent must still punctuate. _fireAccent() re-asserts the
      // same gate; see the CV_PULSE gate check in test/host/run.sh.
      ContractEffect fx = (pr.hasEffect && accentEffectAllowed(pr.effect)) ? pr.effect : CE_SOLID;
      if (_nativeIsUp(hp)) break;                      // no overlay on a native look (and it does not begin a show)
      if (contractShowBegin(s)) _dark(hp);             // L1: the show's first P: the baseline goes dark first
      _fireAccent(hp,
                  fx,
                  pr.hasColor  ? pr.color  : ContractRGB{255, 255, 255},
                  pr.hasBright ? pr.bright : s.bright,       // the unit's own level: an accent must land
                                                             // at the same level on all 3 boards
                  pr.hasDur    ? pr.durMs  : 120);           // clamped to 170..2550 ms inside
      break;
    }

    case CV_BRIGHT: {                                  // master ride, inside an armed show only (S6)
      if (!contractBrightHonoured(s)) break;           // outside a show H<unit>B<level> is the control
      u.ride = pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : 255);
      // A fraction of the ceiling, never above it. setBrightness() rescales the RAM buffer at
      // once and lossily, so every look is made to repaint from its own colour.
      neoStrips[hp].setBrightness(contractLevel(s.ceil, u.ride));
      u.lastEnvBright = -1;
      break;
    }

    case CV_LEVEL:                                     // meter substitute (HP has no VU)
      u.level = pr.hasLevel ? pr.level : u.level;
      u.lastEnvBright = -1;
      break;

    case CV_STOP:
      _boundary(hp, 'x', 0);
      break;

    case CV_MODE:
      if (pr.mode == 's')      _boundary(hp, 's', 0);
      else if (pr.mode == 'i') _boundary(hp, 'i', 0);
      break;

    case CV_QUERY: {                                   // targeted ack only (contract §8)
      if (p.unit == '*') break;                        // never broadcast Q
      char buf[56];
      snprintf(buf, sizeof(buf), "!H%cq:ver=1.1,phase=2,i=%d,bpm=%u\r\n",
               _unitChar(hp), (int)s.effect, (unsigned)gBeat.bpm);
      wdpFinish();                                     // a WDP announce in flight goes out whole first
      Serial.print(buf);
      break;
    }

    default: break;                                    // CV_CLOCK is the board's, not a unit's: applyContract()
  }
}

// Fan a parsed command to the addressed jewel(s) per class/unit.
inline void applyContract(const ParsedContract& p, uint16_t lineCrc = 0) {
  if (p.cls != 'H' && p.cls != '*') return;            // not a Holoprojector board
  const bool all = (p.unit == '*');
  const bool to[HPCOUNT] = {all || p.unit == 'F', all || p.unit == 'R', all || p.unit == 'T'};
  if (p.verb == CV_CLOCK) {                            // ONE shared clock: seed it once, then every unit follows
    const uint32_t now = millis();
    const ContractSeed k = beatClockSeed(gBeat, p.params, now);
    if (k == CS_IGNORED || k == CS_HOLD) return;       // S1: ignored whole. C6: held — the cursors freeze
                                                       // because a stopped clock is not ticked
    for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
      ContractUnitState& s = gUnit[hp].s;
      s.lastAccentBeat = accentEdgeAfterSeed(gBeat, now);   // C1: mid-beat, that beat counts as accented
      s.pinForward = (k == CS_NUDGE);                       // C2: a nudge never moves a section cursor back
      if (k == CS_SEEK && to[hp] && contractShowBegin(s)) _dark(hp);   // L1: the show's first C
    }
    return;
  }
  for (uint8_t hp = 0; hp < HPCOUNT; hp++)
    if (to[hp]) applyContractToUnit(hp, p, lineCrc);
}

// ------------------------------ beat tick + score switch + board-side accent ---
// Call once per loop(), before the per-hp render block. It runs every pass, clock or no clock.
inline void contractBeatTick() {
  const uint32_t now = millis();
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    FlthyUnit& u = gUnit[hp];
    ContractUnitState& s = u.s;
    _expireOverlay(hp, now);
    // A stock command, or its |<n> timer, took the render slot: the jewel is the stock
    // firmware's again. Stop holding the unit (outside a show its auto twitch would never
    // come back) and re-arm the timers so letting go moves nothing in this pass.
    if ((s.base == CB_LOOK || s.pulseActive) && !_slotIsOurs(hp)) {
      s.pulseActive = false;  u.overNative = false;
      if (s.base == CB_LOOK) s.base = s.inShow ? CB_DARK : CB_NONE;
      if (!s.inShow) _rearmTwitch(hp);
    }
    if (s.base == CB_NATIVE && !_nativeIsUp(hp)) s.base = s.inShow ? CB_DARK : CB_NONE;
    ContractLookPatch lp;
    if (liveLookDue(s, now, lp)) _applyPatch(hp, lp, now);   // S4: a held live look's window ended
  }
  if (!gBeat.running) return;                          // no clock (or held, C6): the score does not move
  const BeatPos bp = beatPosAt(gBeat, now);
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    ContractUnitState& s = gUnit[hp].s;
    if (s.scoreLen == 0) continue;
    int idx = scoreActiveIndex(gScore[hp], s.scoreLen, bp.beatIndex);
    idx = scoreCursor(s.curSection, idx, s.pinForward);     // C2
    if (idx < 0 || idx == s.curSection) continue;
    _enterSection(hp, idx, now);
  }

  // The board fires its OWN effect-swap accent, once per accent beat: this is what makes a
  // DELIVERED show look like a LIVE-mirrored one. Runs AFTER the section loop so a section
  // starting on a downbeat accents on that very beat. Gated to the SCORE path only — a live
  // verb-A never self-accents (the host's verb P owns that), so a live show cannot double-fire.
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    ContractUnitState& s = gUnit[hp].s;
    // Consume the edge FIRST and UNCONDITIONALLY: a beat that does not accent must still
    // retire its edge, or the next tick (same beat) re-tests and re-fires every frame.
    if (!beatEdge(s.lastAccentBeat, bp.beatIndex)) continue;
    if (s.scoreLen == 0 || s.curSection < 0) continue;       // no active section
    const ScoreEntry& e = gScore[hp][s.curSection];
    if (e.accentFx == CE_NONE) continue;                     // no ae=: never accents
    // The SAME predicate the brightness pump gates on, so the pump and the effect accent can
    // NEVER disagree about which beats carry an accent; both are under the S2 rate floor.
    if (!beatAccentFires(e.accentMode, bp, gBeat, e.accentEvery)) continue;
    _fireAccent(hp, e.accentFx, e.accentColor, s.bright, (uint32_t)e.accentDur10 * 10u);
  }
}

// The single fork command entry point. main.cpp passes the bytes AFTER '!'.
inline void contractHandle(const char* afterBang) {
  ParsedContract p;
  if (contractParse(afterBang, p)) applyContract(p);
}
