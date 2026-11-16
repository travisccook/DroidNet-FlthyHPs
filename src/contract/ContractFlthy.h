// Part of the DroidNet Driveable-Animation Contract — an additive layer bolted onto
// Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
// SPDX-License-Identifier: MIT
//
// src/contract/ContractFlthy.h — Flthy Holoprojectors firmware layer for the
// Driveable-Animation Contract (v1.3). LED-ONLY: the '!' path NEVER writes HP_command[*]
// and NEVER calls positionHP/twitchHP/wagHP/RCHP — no servo motion, ever. It never writes
// the operator's settings either (enableTwitchLED[], enableTwitchHP[], offcoloroverride[]):
// an armed show HOLDS a unit, and main.cpp's two auto-twitch gates ask
// contractServoTwitchHeld() and contractLedTwitchHeld(). When a hold ends, whichever way, the
// gate itself puts the twitch timer a full interval ahead before the stock test reads it, so the
// pass that handles a '!' line, ends an overlay or reaches a score section never starts a servo
// twitch. test/host/sim checks both on the real main.cpp, on every simulated pass.
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
//   DnStrip neoStrips[HPCOUNT];        (src/droidnet/dn_strip.h: Adafruit_NeoPixel, show() gated below)
//     .setPixelColor(uint16_t,uint32_t)  .show()  .setBrightness(uint8_t)  .getBrightness()  .getPixels()
//   LEDCmd  LED_command[HPCOUNT];  (.LEDFunction byte)
//   (.LEDOption1, .LEDOption2, .LEDHalt: written only to hand the LED side back, as the stock flush does)
//   const int offcolor[HPCOUNT]; boolean offcoloroverride[HPCOUNT];      (read only)
//   unsigned long twitchLEDTime[HPCOUNT]; const unsigned int LEDTwitchInterval[HPCOUNT][2];
//   void resetHPtwitch(byte);    // sets the servo twitch timer a full interval ahead; moves nothing
//   bool dnServoTwitchOverdue(byte);   // is that timer overdue? (a DroidNet one-liner in main.cpp)
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
#include "../wdp/wdp_fork.h" // wdpInFlight() (verb Q reply)

// -------------------------------------------------------------- constants ----
static const uint8_t  FLTHY_FX_BASE         = 100;   // LEDFunction = 100 + effectId (101..115)
#ifdef NEO_JEWEL_RGBW
static const uint8_t  FLTHY_PX_BYTES        = 4;     // raw bytes a pixel
#else
static const uint8_t  FLTHY_PX_BYTES        = 3;
#endif
static const uint8_t  FLTHY_SCORE_CAP       = 32;    // scored sections a unit holds (spec section 6); Q reports it as cap=

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
  uint8_t  ride        = 255;             // verb B: a fraction of s.ceil on the STRIP's brightness, as applied
  uint8_t  rideWant    = 255;             // ...and as asked for: a RAISE waits for the field gate (see _rideTry)
  bool     nativeWait  = false;           // S10: a native look is chosen and waits for its start window
  bool     nativeTold  = false;           // S10: the gate has been told the runs-as-stock look that is up
  byte     keptFn      = 255;             // S10: while nativeWait, the LED function of the layer's OWN look that stays up meanwhile
  uint8_t  level       = 0;               // last verb-L value (meter substitute)
  int      lastEnvBright = -1;            // memo for the looks that repaint only on a change
  uint32_t frameMs     = 0;               // scan/sparkle self-throttle
  uint8_t  frame       = 0;
  bool     twitchFreed  = false;          // the owner sent A199 while the unit was held (see contractOwnerServoTwitch)
  bool     servoWasHeld = false;          // the two twitch gates' view of the hold on their last pass
  bool     ledWasHeld   = false;
  // L3: a verb P with no show armed overlays the STOCK look and gives it back afterwards.
  bool     overNative  = false;
  byte     savedFn     = 255;             // the stock LEDFunction under the overlay
  uint8_t  savedPx[NEO_JEWEL_LEDS * 4];   // its frame, raw (RGB uses 21 of the 28 bytes)
  uint8_t  savedAt     = 0;               // the strip level that frame was scaled for
};

static FlthyUnit  gUnit[HPCOUNT];
static BeatClock  gBeat;                  // ONE clock for the three units
static uint32_t   gLastLineMs = 0;        // L2: millis() of the last well-formed line for this board
// L2, board-wide: the wd= of the last watched M:v=show. A unit that LEFT that show (the operator's
// button, a failed verb V) has no watchdog of its own any more, but the host that armed it is the
// same host, and its next wildcard line can put a look or a score on that unit again. When that
// host goes silent, every unit holding anything of the contract's is idled, in a show or not.
// Cleared by the expiry, by M:v=idle once no unit is left in a show (the host ended it itself), and
// by an M:v=show with no wd= for the whole board (a show nobody watches, such as a WCB macro).
static uint8_t    gBoardWd = 0;
// Section 10: a verb-Q waits here for its reply and contractBeatTick() sends it, one reply a pass.
// One slot per unit (a newer Q to the same unit replaces its route), so F, R and T asked back to
// back are all answered, and the reply is formatted when it is sent, so it reports the state of
// that moment. The line handler never writes to Serial: a write that finds the 63-byte TX ring full
// blocks loop(), servo stepping included, for as long as the bytes take to leave at 9600 baud.
static uint8_t    gQueryRoute[HPCOUNT];   // the r= to answer on
static uint8_t    gQueryAsked = 0;        // bit hp: that unit owes a reply
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
  return (s.inShow || s.base == CB_LOOK || s.pulseActive) && !gUnit[hp].twitchFreed;
}
// "Enable servo twitch" (stock A199, or F/R/T/X/Y/Z199) is the owner's word, in a show too: given
// while the contract holds a unit, it lifts the hold on that unit's SERVO twitch until the next
// M:v=show or M:v=idle. The lights stay the show's. A show armed with no watchdog whose host has
// died would otherwise keep the holoprojectors still for good, with no button to bring them back.
// A198 puts the hold back (and the stock code has switched the twitch off anyway). main.cpp calls
// this for each unit such a command addresses; the stock handler re-arms the timer itself.
inline void contractOwnerServoTwitch(uint8_t hp, bool on) {
  const ContractUnitState& s = gUnit[hp].s;
  gUnit[hp].twitchFreed = on && (s.inShow || s.base == CB_LOOK || s.pulseActive);
}
inline bool contractHoldsLeds(uint8_t hp) {
  const ContractUnitState& s = gUnit[hp].s;
  return (s.inShow && s.base != CB_NONE) || s.base == CB_LOOK || s.pulseActive;
}
// What main.cpp's two auto-twitch gates ask, FIRST in their condition and so once every pass. This
// is the one place a hold ENDING is seen, whichever path ended it (a reset, a look replaced by a
// native one, the last overlay expiring, a score reaching a native section, a stock command taking
// the slot): in that pass the timer is put a full interval ahead BEFORE the stock test reads it.
// By then it is long overdue, and without this the stock loop twitches the unit at once, all three
// holoprojectors together when one line let them all go. resetHPtwitch() moves nothing.
// ONLY an overdue timer is re-armed. A timer that is not due yet would not fire at the release
// anyway, and pushing it a full interval ahead at the end of every short hold (a pulse every 40 s
// from a WCB macro) meant the owner's auto twitch never came due at all.
inline bool contractServoTwitchHeld(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  const bool held = contractHolds(hp);
  if (u.servoWasHeld && !held && dnServoTwitchOverdue(hp)) resetHPtwitch(hp);   // only an OVERDUE timer (see below)
  u.servoWasHeld = held;
  return held;
}
inline bool contractLedTwitchHeld(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  const bool held = contractHoldsLeds(hp);
  if (u.ledWasHeld && !held && millis() > twitchLEDTime[hp])
    twitchLEDTime[hp] = (1000L * random(LEDTwitchInterval[hp][0], LEDTwitchInterval[hp][1])) + millis();
  u.ledWasHeld = held;
  return held;
}
// Does this layer have the unit's LEDs (so a reset must hand them back)?
// (While a native look waits for its window, the look it will replace is still up. When that look
// was this layer's too, a native look the contract started, the slot is still this layer's: a
// boundary must take it down, or it would run on as stock light, ungated.)
static inline bool _keptIsUp(uint8_t hp) { return gUnit[hp].nativeWait && gUnit[hp].keptFn != 255 && LED_command[hp].LEDFunction == gUnit[hp].keptFn; }
static inline bool _heldLeds(uint8_t hp) { return contractHoldsLeds(hp) || _slotIsOurs(hp) || _nativeIsUp(hp) || _keptIsUp(hp); }

// ------------------------------------ S9, S4, S10: the core's gates, asked at the latch ---
// v1.3 S9: whatever lines arrive, in whatever order, a jewel goes from dark to lit across its
// whole field at most three times a second; S4: its look or colour changes at most once in
// 170 ms, whichever path asked. The rules are the shared core's, one definition for the three
// boards (contract_core.h: the field gate and the look gate). This board's part is WHERE they are
// asked: at the one place every frame is latched, DnStrip::show() (src/droidnet/dn_strip.h), which
// the stock firmware and this layer both go through.
//   * Two measures of the frame about to be latched, as the core defines them: the LEVEL (the mean
//     over the seven pixels of each pixel's brightest channel) and the LUMINANCE (the mean of
//     contractLuminance(r, g, b)). Both are read from the latched bytes, which already carry the
//     strip's level, so the ceiling the gate compares with (s.ceil) is the owner's strip level.
//   * A frame THIS LAYER drew (a contract render slot), a frame of a GATED native look (S10: Leia,
//     which re-rolls the whole field several times a second), and any frame in the 340 ms after
//     the layer handed the jewel back (the off colour returning after a reset) is ASKED: the look
//     gate when it is lit and its look differs from the last one shown, then the field gate. On a
//     no the frame is NOT latched. What is on the jewel already passed the gates, the frame waits
//     in the pixel buffer, and the tick offers it again every pass. A fall is never delayed. On
//     a yes the gates are TOLD what the unit now shows.
//   * A RUNS-AS-STOCK native look (S10: the rainbow) goes out unchanged, frame by frame. The gate
//     governs its edges only: _showBase() asks contractNativeMayStart() before the LEDs are handed
//     over, and the first lit frame tells the gate the level the look runs at.
//   * Any other frame is the stock firmware's own light, which runs as Ryan wrote it, strobing
//     looks included. The gates are told what it shows, so that what the contract does next keeps
//     its distance from it.
static const uint16_t FLTHY_ONSET_GAP_MS = 2 * STROBE_MIN_STATE_MS;   // 340: how long a hand-back stays this layer's
// Byte order of one pixel in Adafruit_NeoPixel's buffer for NEO_GRB and NEO_GRBW (main.cpp: HP_NEO_TYPE).
static const uint8_t FLTHY_OFF_G = 0, FLTHY_OFF_R = 1, FLTHY_OFF_B = 2;
static const uint8_t FLTHY_RECOLOUR = 48;               // of 255 in any channel of the mean colour, from the last change: a look's own change of colour
static const uint8_t FLTHY_ARRIVED  = 64;               // ...from the frame before: a contract look's colour has arrived on the field
struct FlthyLatch {
  uint32_t touchMs   = 0;                 // the last pass this layer had the jewel, or handed it back
  uint32_t frameSig  = 0;                 // the look (contractLookSig) of the frame the layer is drawing
  uint16_t peakSum   = 0;                 // while the field is lit: its brightest sum so far, in sevenths of a level
  uint16_t peakSumY  = 0;                 // ...and the same for the luminance
  uint8_t  latchedAt = 0;                 // the strip level the frame on the jewel was latched at
  uint8_t  nativeFull = 0;                // S10: the level a runs-as-stock look has at the ceiling (for verb B)
  uint8_t  nativeFullY = 0;               // ...and its luminance
  uint8_t  chroma[3]  = {0, 0, 0};        // the field's colour at its last change; under a contract look, of its last frame
  byte     pendingFn  = 255;              // the LED function that drew the frame being held back
  bool     touched   = false;             // touchMs is meaningful
  bool     pending   = false;             // a frame was held back and waits in the pixel buffer
  bool     owed      = false;             // a frame of the hand-back was held back: the stock light is asked until one is shown
  bool     extended  = false;             // ...and the 340 ms ran again from that frame (once for each hand-back)
};
static FlthyLatch gLatch[HPCOUNT];
// A frame is being held back, so what is on the jewel stays. If the owner has lowered the strip
// level since that frame was latched, it was scaled for the old level and cannot be rescaled on
// the LEDs: black it out (a fall is always allowed). The held frame follows at the new level.
static inline void _holdFrame(uint8_t hp) {
  FlthyLatch& g = gLatch[hp];
  g.pending = true;
  g.pendingFn = LED_command[hp].LEDFunction;
  if (neoStrips[hp].getBrightness() >= g.latchedAt) return;
  uint8_t keep[NEO_JEWEL_LEDS * 4]; uint8_t* raw = neoStrips[hp].getPixels();
  const size_t n = (size_t)NEO_JEWEL_LEDS * FLTHY_PX_BYTES;
  memcpy(keep, raw, n); memset(raw, 0, n);
  neoStrips[hp].Adafruit_NeoPixel::show();
  memcpy(raw, keep, n);
  contractFieldTell(gUnit[hp].s, 0, 0, millis());
  g.latchedAt = neoStrips[hp].getBrightness();
}
static inline void _touch(uint8_t hp) { FlthyLatch& g = gLatch[hp]; g.touchMs = millis(); g.touched = true; g.extended = false; }
// Has this layer the jewel's LEDs right now? (base == CB_NATIVE counts before the reconcile in the tick.)
static inline bool _hasLeds(uint8_t hp) { return _heldLeds(hp) || gUnit[hp].s.base == CB_NATIVE; }
inline void DnStrip::show() {
  const uint8_t hp = (uint8_t)(this - neoStrips);
  if (hp >= HPCOUNT) { Adafruit_NeoPixel::show(); return; }
  FlthyLatch& g = gLatch[hp];
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  const uint8_t* px = getPixels();
  uint16_t sum = 0, sumY = 0, sumC[3] = {0, 0, 0};
  for (uint8_t i = 0; i < NEO_JEWEL_LEDS; i++) {
    const uint8_t* p = px + i * FLTHY_PX_BYTES;
    uint8_t r = p[FLTHY_OFF_R], gr = p[FLTHY_OFF_G], b = p[FLTHY_OFF_B];
#ifdef NEO_JEWEL_RGBW
    { const uint8_t w = p[3]; r = (uint8_t)(r > 255 - w ? 255 : r + w); gr = (uint8_t)(gr > 255 - w ? 255 : gr + w); b = (uint8_t)(b > 255 - w ? 255 : b + w); }
#endif
    uint8_t m = r > gr ? r : gr; if (b > m) m = b;
    sum += m;
    sumY += contractLuminance(r, gr, b);
    sumC[0] += r; sumC[1] += gr; sumC[2] += b;
  }
  const uint8_t level = (uint8_t)(sum / NEO_JEWEL_LEDS), lum = (uint8_t)(sumY / NEO_JEWEL_LEDS);
  if (u.ride == 255) s.ceil = getBrightness();          // the ceiling is the owner's strip level; a verb-B ride sits below it
  const uint32_t now = millis();
  const byte fn = LED_command[hp].LEDFunction;
  const bool nativeUp = s.base == CB_NATIVE && !u.nativeWait && fn == (byte)s.nativeCode;
  // S10: a native look is chosen and waits for its window. What is up stays up: nothing lit is
  // latched meanwhile, or the look that is going would keep moving the window the new one waits for.
  if (u.nativeWait && level != 0) { _holdFrame(hp); return; }
  if (nativeUp && !nativeGatedFlthy(s.nativeCode)) {
    // S10, runs as stock: the frame goes out as the stock code drew it. The gate is told ONCE, at
    // the first lit frame, the level the look runs at (a stock look may clear the jewel first).
    if (!u.nativeTold && level != 0) {
      contractNativeStarted(s, s.nativeCode, level, now); u.nativeTold = true;
      // The core takes ONE figure for a native look and records it for both measures. A rainbow's
      // luminance is under half its level: left at that, a contract frame that follows it dimmer by
      // level and brighter by luminance is no onset to the gate. Tell it what this frame measures.
      contractFieldTell(s, level, lum, now);
      const uint16_t full = (uint16_t)(((uint32_t)level * (s.ceil + 1u)) / (getBrightness() + 1u));   // a ride may be on
      const uint16_t fullY = (uint16_t)(((uint32_t)lum * (s.ceil + 1u)) / (getBrightness() + 1u));
      g.nativeFull = (uint8_t)(full > 255u ? 255u : full);
      g.nativeFullY = (uint8_t)(fullY > 255u ? 255u : fullY);
    }
  } else {
    const bool drawn = nativeUp || (fn >= 101 && fn <= FLTHY_FX_MAX);
    // THE COLOUR ON THE JEWEL (S4), measured on every frame of 4 levels or more in its strongest
    // channel (under that, one count of rounding in a LED moves the mean colour further than any
    // test): its three channels summed over the LEDs, over the strongest.
    //   * A look that draws its OWN colours under one name: a GATED native look (Leia re-rolls the
    //     whole field by itself) and stock light (one function number, any colours). A frame far
    //     from the colour the field had at its LAST CHANGE is a change of colour like any other: it
    //     waits its 170 ms where the frame is asked, and moves the look gate's time where it is
    //     shown. "Far" is half of what an observer calls a change, and is measured from the last
    //     change, not from the frame before: every frame between two changes is then within that
    //     half of one colour, so no two of them are a change apart, whichever frames an observer
    //     happens to compare (a drift of 47 and then 50 in 141 ms was two small steps frame to
    //     frame, and one change to an eye that missed the dim frame between).
    //   * A CONTRACT look's colour is in its signature, and the look gate has timed it. But a look
    //     that comes up dim (a pulse through 3 and 7 levels) has not changed the field's colour at
    //     its first frame: it has when a frame first SHOWS the colour. That frame moves the look
    //     gate's time too (it is never held for it), as a field that lights does (the core's
    //     _fieldBrighter), so the next look keeps its 170 ms from the colour's arrival.
    bool recolour = false, coloured = false; uint8_t chroma[3] = {0, 0, 0};
    const bool ownColour = nativeUp || !drawn;
    {
      uint16_t top = sumC[0] > sumC[1] ? sumC[0] : sumC[1]; if (sumC[2] > top) top = sumC[2];
      if (top >= 4u * NEO_JEWEL_LEDS) {
        coloured = true;
        for (uint8_t k = 0; k < 3; k++) {
          chroma[k] = (uint8_t)(((uint32_t)sumC[k] * 255u) / top);
          const uint8_t d = chroma[k] > g.chroma[k] ? (uint8_t)(chroma[k] - g.chroma[k]) : (uint8_t)(g.chroma[k] - chroma[k]);
          if (d > (ownColour ? FLTHY_RECOLOUR : FLTHY_ARRIVED)) recolour = true;
        }
      }
    }
    // The hand-back stays this layer's for 340 ms; while a frame of it is held back, until one is
    // shown; and, once, for 340 ms more from that frame: the stock look's next change keeps its
    // distance from the light the hand-back put up, not from the moment the layer let go.
    if (drawn || (g.touched && ((uint32_t)(now - g.touchMs) < FLTHY_ONSET_GAP_MS || g.owed))) {
      const uint32_t sig = nativeUp ? contractLookSigNative(s.nativeCode)
                                    : (drawn ? g.frameSig : contractLookSigNative(0x4000 | fn));   // the board's own look
      if ((level != 0 && (!contractLookAllowed(s, sig, now) || (ownColour && recolour && !strobeCoolDownExpired(s.lookMs, now, STROBE_MIN_STATE_MS)))) ||
          contractFieldAsk(s, level, lum, now) != 255) {
        if (!drawn) g.owed = true;
        _holdFrame(hp);                                 // too soon: keep what is on the jewel
        return;
      }
      if (level != 0) contractLookGate(s, sig, now);    // the change of look is shown now
      if (!drawn && g.owed) { g.owed = false; if (!g.extended) { g.extended = true; g.touchMs = now; } }
    } else if (level != 0) {
      contractLookForced(s, contractLookSigNative(0x4000 | fn), now);   // stock light: recorded, never held back
    }
    if (recolour) s.lookMs = now;
    if (coloured && (recolour || !ownColour)) memcpy(g.chroma, chroma, 3);   // the colour of the last change; a contract look's, of its last frame
    contractFieldTell(s, level, lum, now);              // what the unit shows from now
    // The gate takes whole levels; seven pixels measure in sevenths. While the field is lit, a peak
    // that creeps up by less than one whole level is still "getting brighter", and by the gate's own
    // rule the onset is not over until the light stops rising. Keep that finer peak here and move
    // the gate's clocks with it, or an observer whose line sits inside that last level would time
    // the onset later than the gate did.
    bool brighter = false;
    if (!s.fieldLit) g.peakSum = 0;   else if (sum > g.peakSum)   { g.peakSum = sum;   brighter = true; }
    if (!s.fieldLitY) g.peakSumY = 0; else if (sumY > g.peakSumY) { g.peakSumY = sumY; brighter = true; }
    if (brighter) { s.fieldOnsetMs = now; s.lookMs = now; }
  }
  g.pending = false;
  g.latchedAt = getBrightness();
  Adafruit_NeoPixel::show();
}
// Each auto-twitch timer that is OVERDUE, a full interval from now. When the layer lets go of a unit
// after a show the timers are long overdue, and without this the stock loop twitches the LEDs AND
// THE SERVOS in the same pass. A timer that is not due yet is left alone (see the twitch gates).
// resetHPtwitch() only sets the servo timer; it moves nothing.
static inline void _rearmTwitch(uint8_t hp) {
  if (millis() > twitchLEDTime[hp])
    twitchLEDTime[hp] = (1000L * random(LEDTwitchInterval[hp][0], LEDTwitchInterval[hp][1])) + millis();
  if (dnServoTwitchOverdue(hp)) resetHPtwitch(hp);
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
  _touch(hp);                                           // the off colour that follows is this layer's relight (S9)
}
// Dark, with nothing of the stock firmware's left running: 255 is its "idle, dark" value
// (never 0: that is "do nothing", which also blocks the stock auto twitch for good), and the
// |<n> / auto-twitch timer that would flush the jewel later is cancelled.
static inline void _dark(uint8_t hp) {
  LED_command[hp].LEDFunction = 255;
  LED_command[hp].LEDHalt     = -1;
  ledOFF(hp);
  _touch(hp);
}
// v1.3 S7: the native looks this layer may start are the core's nativeAllowedFlthy() list. They
// need no option bytes and no counters: 1 = Leia (fixed blue colour projector), 6 = rainbow.
// v1.3 S6: the brightness ceiling is the STRIP's own level when the show arms (BRIGHT, or what
// H<unit>B<level> last set); it is kept in s.ceil. setPixelColor() applies the strip's level to
// every write, so b= (s.bright) is used as the fraction it is and never multiplied by s.ceil.
// Verb B rides the strip below the ceiling and never outlives the show.
// Verb B, the STEP is what the gate governs. A B that lowers the level applies at once (a fall is
// never delayed). A B that raises it could relight the whole field, and on an armed show that has
// not begun the light it relights is the stock firmware's, which the latch never holds back. So the
// raise itself waits until the field gate allows a rise to the new level; the tick tries again
// every pass. Stock frames between two B steps are never held.
static inline void _rideApply(uint8_t hp, uint8_t r) {
  FlthyUnit& u = gUnit[hp];
  const uint8_t is = contractLevel(u.s.ceil, r);
  u.ride = u.rideWant = r;
  neoStrips[hp].setBrightness(is);
  u.lastEnvBright = -1;                                 // every contract look repaints from its own colour
  // S10: a runs-as-stock native look is not measured frame by frame, so the gate is told its new level here.
  if (u.s.base == CB_NATIVE && u.nativeTold && !nativeGatedFlthy(u.s.nativeCode)) {
    const uint16_t lv = (uint16_t)(((uint32_t)gLatch[hp].nativeFull * (is + 1u)) / (u.s.ceil + 1u));
    const uint16_t ly = (uint16_t)(((uint32_t)gLatch[hp].nativeFullY * (is + 1u)) / (u.s.ceil + 1u));
    contractNativeLevel(u.s, (uint8_t)(lv > 255u ? 255u : lv), millis());
    contractFieldTell(u.s, (uint8_t)(lv > 255u ? 255u : lv), (uint8_t)(ly > 255u ? 255u : ly), millis());   // its luminance too
  }
  // (c) The step is this layer's light change, whoever draws the frame it shows on: stock light on
  // an armed unit is asked of the gates for 340 ms from here, like a hand-back.
  _touch(hp);
}
static inline void _rideTry(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  if (u.rideWant == u.ride) return;
  if (u.rideWant < u.ride) { _rideApply(hp, u.rideWant); return; }
  const uint8_t lv = contractLevel(u.s.ceil, u.rideWant);
  const uint8_t full = (lv == 255) ? 255 : (uint8_t)((255u * (lv + 1u)) >> 8);   // a white field at the new strip level
  if (contractFieldAsk(u.s, full, full, millis()) == 255) _rideApply(hp, u.rideWant);
}
// A boundary ends a ride: the owner's level comes back, which is a raise like any other and waits
// for the gate like any other (B never outlives a show by more than that).
static inline void _restoreRide(uint8_t hp) {
  gUnit[hp].rideWant = 255;
  _rideTry(hp);
}
// The native brightness command (H<unit>B<level>) calls this: the owner's new level is the
// ceiling from now on, and any verb-B ride is over.
inline void contractOwnerBrightness(uint8_t hp, uint8_t level) {
  // The field gate keeps its reference level in latched-byte units, and setBrightness() has just
  // rescaled every latched byte. Rescale the reference the same way, or a peak measured at the old
  // level would be compared with a step sized for the new one (found by the S9 soak: a doubled
  // level made the pump's next fall too small to count, and its next rise passed as "still rising").
  ContractUnitState& gs = gUnit[hp].s;
  const uint16_t was = (uint16_t)gs.ceil + 1u, is = (uint16_t)level + 1u;
  const uint16_t ref = (uint16_t)(((uint32_t)gs.fieldRef * is) / was);
  gs.fieldRef = (uint8_t)(ref > 255u ? 255u : ref);
  gLatch[hp].peakSum = (uint16_t)(((uint32_t)gLatch[hp].peakSum * is) / was);
  const uint16_t refY = (uint16_t)(((uint32_t)gs.fieldRefY * is) / was);
  gs.fieldRefY = (uint8_t)(refY > 255u ? 255u : refY);
  gLatch[hp].peakSumY = (uint16_t)(((uint32_t)gLatch[hp].peakSumY * is) / was);
  gUnit[hp].s.ceil = level;
  gUnit[hp].ride = gUnit[hp].rideWant = 255;            // the owner's command is the level, at once
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
    // S10: before the LEDs are handed to a stock look, ask. It may clear the jewel and light it at
    // once, so the answer needs the onset window and the look window whether the jewel is lit or
    // dark just now. On a no, what is up stays up and the tick (or the render slot) asks again.
    const uint32_t now = millis();
    if (!contractNativeMayStart(s, s.nativeCode, s.ceil, now)) { u.nativeWait = true; return; }
    u.nativeWait = false;  u.nativeTold = false;
    LED_command[hp].LEDFunction = (byte)s.nativeCode;   // allow-listed: needs no option bytes
    LED_command[hp].LEDHalt     = -1;
    // (A GATED native look's frames are asked at the latch, the first one as the change of look it
    // is: the look gate hears of it when it is on the LEDs, not here, where it is only selected.)
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
  const uint32_t prog = (s.curSection >= 0) ? beatSectionProgress16(bp.beatIndex, s.sectionStart, s.sectionEnd) : 0;
  return envBright(s.bright, s.beatMod, beatAccentAmount(s.accentMode, bp, s.beatMod, prog, gBeat, s.accentEvery));
}

// --------------------------------------------------- the single render fn -----
// Render ONE look for unit hp. The BASE look and the ACCENT OVERLAY (verb P, or the
// board-side beat edge) both render through this one switch — so the accent's effect
// vocabulary costs ZERO extra flash and can never drift from the base look's.
// Only the four inputs differ between the two callers:
//   base    : (s.effect,  s.color,      _envBright(hp)  — beat-pumped, s.startMs)
//   overlay : (s.pulseFx, s.pulseColor, s.pulseBright   — un-pumped,
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

    // flash, pulse and scan run on the core's shared timing (fxFlashOn, fxPulseBright, fxScanPos),
    // so the same s= is the same rate on the Logics, the PSIs and these jewels.
    case CE_FLASH: {                                   // 170..425 ms a state: never past the strobe cap
      // envB (NOT the raw ceiling): CE_FLASH beat-pumps like every other sustained look.
      _fillShow(hp, fxFlashOn(now - startMs, u.s.speed) ? _scale(color, envB) : 0x000000);
      break;
    }

    case CE_PULSE: {                                   // triangle breathe, a 2000..340 ms cycle (S8)
      // Under the beat pump the envelope IS the breathe, so the fill is steady at the pumped
      // level: two breathes multiplied together beat against each other. An overlay is never
      // pumped (pulseActive: this call is drawing the overlay).
      const bool pumped = !u.s.pulseActive && u.s.accentMode != 0 && u.s.beatMod != 0 && gBeat.running;
      const uint8_t br = pumped ? envB
                                : (uint8_t)(((uint16_t)envB * fxPulseBright(now - startMs, u.s.speed)) / 255u);
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

    case CE_SCAN: {                                    // one dot walks ring 1..6, centre px0 off
      const uint8_t px = (uint8_t)(1 + fxScanPos(now - startMs, u.s.speed, NEO_JEWEL_LEDS - 1, true));
      if (px != u.frame || u.lastEnvBright != (int)envB) {   // repaint on a step or a level change
        u.frame = px;  u.lastEnvBright = envB;
        neoStrips[hp].setPixelColor(0, 0x000000);      // centre always off
        for (uint16_t i = 1; i < NEO_JEWEL_LEDS; i++)
          neoStrips[hp].setPixelColor(i, (i == px) ? _scale(color, envB) : 0x000000);
        neoStrips[hp].show();
      }
      break;
    }

    case CE_SPARKLE: {                                 // a fixed number of pixels lit; ONE moves at a time
      // Redrawn for seven pixels. Re-rolling every pixel's brightness swung the whole field by more
      // than a tenth of the ceiling most frames, and the S9 gate then held over a third of them.
      // Now the count of lit pixels is constant (s picks it: 2, 3, 4 or 5) and all are at the same
      // level, so the field's level does not move at all; what moves is where the light is, which
      // is the look. s also picks how often a pixel moves (every 150 ms down to 40 ms).
      const uint8_t want = (uint8_t)(2 + (u.s.speed >> 6));
      const uint32_t step = (uint32_t)map((long)u.s.speed, 0, 255, 150, 40);
      uint8_t lit = 0;
      for (uint8_t i = 0; i < NEO_JEWEL_LEDS; i++) if (u.frame & (1u << i)) lit++;
      bool repaint = u.lastEnvBright != (int)envB;
      if (lit != want) {                               // the look has just started: place the lit pixels
        u.frame = 0;
        for (lit = 0; lit < want; ) { const uint8_t p = (uint8_t)random(0, NEO_JEWEL_LEDS); if (!(u.frame & (1u << p))) { u.frame |= (uint8_t)(1u << p); lit++; } }
        u.frameMs = now; repaint = true;
      } else if ((uint32_t)(now - u.frameMs) > step) { // move one: a lit pixel goes dark, a dark one lights
        u.frameMs = now;
        uint8_t offPick = (uint8_t)random(0, want), onPick = (uint8_t)random(0, NEO_JEWEL_LEDS - want), offIdx = 0, onIdx = 0;
        for (uint8_t i = 0; i < NEO_JEWEL_LEDS; i++) {   // the offPick-th lit pixel and the onPick-th dark one
          if (u.frame & (1u << i)) { if (offPick == 0) offIdx = i; offPick--; }
          else                     { if (onPick == 0) onIdx = i;  onPick--; }
        }
        u.frame = (uint8_t)((u.frame & ~(1u << offIdx)) | (1u << onIdx));
        repaint = true;
      }
      if (repaint) {
        u.lastEnvBright = envB;
        for (uint16_t i = 0; i < NEO_JEWEL_LEDS; i++) neoStrips[hp].setPixelColor(i, (u.frame & (1u << i)) ? _scale(color, envB) : 0x000000);
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
        uint32_t c = fxChaseLit(p, el, u.s.speed, N) ? _scale(color, envB) : 0x000000;
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
  if (!s.pulseActive || contractOverlayUp(s, now)) return;   // the core's one expiry rule; it takes the overlay down
  u.lastEnvBright = -1;                                  // force a base repaint
  if (!_slotIsOurs(hp)) { u.overNative = false; return; }   // the stock firmware took the jewel meanwhile
  if (u.overNative) {
    u.overNative = false;
    LED_command[hp].LEDFunction = u.savedFn;
    // The saved frame holds bytes already scaled for the strip level of that moment. If the owner
    // has changed the level since (H<unit>B<level>), rescale them the way setBrightness() rescaled
    // the live buffer, or a frame brighter than the new ceiling would come back.
    const uint16_t was = (uint16_t)u.savedAt + 1u, is = (uint16_t)neoStrips[hp].getBrightness() + 1u;
    uint8_t* px = neoStrips[hp].getPixels();
    for (uint8_t i = 0; i < NEO_JEWEL_LEDS * FLTHY_PX_BYTES; i++) {
      const uint16_t v = (was == is) ? u.savedPx[i] : (uint16_t)(((uint32_t)u.savedPx[i] * is) / was);
      px[i] = (uint8_t)(v > 255u ? 255u : v);
    }
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
    const ContractEffect fx = (s.pulseFx == CE_NONE) ? CE_SOLID : s.pulseFx;
    gLatch[hp].frameSig = contractLookSig(fx, s.pulseColor);   // the overlay is a look like any other (S4)
    _renderLook(hp, fx, s.pulseColor, s.pulseBright, s.pulseStartMs);
    return;
  }
  if (s.base != CB_LOOK) { _showBase(hp); return; }      // a contract slot with no contract look: put the base back
  gLatch[hp].frameSig = contractLookSig(s.effect, s.color);
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
  s.pulseBright  = bright;                               // the overlay's own level: never the base look's (S9)
  s.pulseDurMs   = accentDurClamp(durMs);                // a state shorter than the min state IS a strobe
  s.pulseStartMs = now;                                  // ALWAYS retrigger (defeats a re-send no-op)
  s.pulseLastMs  = now;                                  // the cool-down anchor: set on every accent that ARMS
  s.pulseActive  = true;
  u.lastEnvBright = -1;
  if (!_slotIsOurs(hp)) {
    if (!s.inShow && !u.overNative) {                    // L3: no show armed -> overlay the STOCK look
      u.overNative = true;
      u.savedFn    = LED_command[hp].LEDFunction;
      u.savedAt    = neoStrips[hp].getBrightness();
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
  gUnit[hp].frame = 0; gUnit[hp].frameMs = 0;         // 0: the scan's first frame is always painted
  varResets(hp);
}
// A live look: now, or when its S4 hold ends. NEVER writes HP_command (LED-only).
// A runs-as-stock native look that is up is about to be replaced: tell the gate (S10).
static inline void _nativeLeave(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  if (u.s.base == CB_NATIVE && u.nativeTold && !nativeGatedFlthy(u.s.nativeCode)) contractNativeEnded(u.s, true, millis());
  u.nativeWait = false;  u.nativeTold = false;
}
// Write a look into the unit: a live A, or a scored section. S11: the SAME look applied again
// (effect, native code, colour and speed) does not restart; only its level and pump follow.
static inline void _setLook(uint8_t hp, ContractEffect effect, int nativeCode, const ContractRGB& color, uint8_t speed, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  s.pulseActive = false;
  u.overNative  = false;                                // a look replaces whatever an overlay sat on
  if (contractLookSame(s, effect, nativeCode, color, speed) && (s.base != CB_NATIVE || _nativeIsUp(hp) || u.nativeWait)) {
    u.lastEnvBright = -1;                               // the level may have changed: repaint, no restart
    return;
  }
  const byte fnWas = _heldLeds(hp) ? LED_command[hp].LEDFunction : (byte)255;   // the layer's own look on the slot, if any
  _nativeLeave(hp);
  s.effect = effect; s.nativeCode = nativeCode; s.color = color; s.speed = speed;
  s.base = (effect == CE_NATIVE) ? CB_NATIVE : CB_LOOK; // the caller dropped any code off the allow-list
  s.startMs = now;
  contractLookRestart(s);                               // S4: the next lit frame is a change of look
  _freshCounters(hp, effect);
  _showBase(hp);
  u.keptFn = u.nativeWait ? fnWas : (byte)255;          // S10: it stays up while the new native look waits
}
// A live look: now, or when its S4 hold ends. NEVER writes HP_command (LED-only).
static inline void _applyPatch(uint8_t hp, ContractLookPatch lp, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  ContractUnitState& s = u.s;
  contractShowBegin(s);                                 // L1: the show's first live A (the look below replaces the dark baseline)
  lookPatchDefault(lp, s.base);                         // L5: no i= and no look yet => i=solid
  if (lp.has & LP_BRIGHT)  s.bright = lp.bright;        // a fraction of the ceiling (S6)
  if (lp.has & LP_BEATMOD) s.beatMod = lp.beatMod;
  if (lp.has & LP_AM)      s.accentMode = lp.accentMode;
  if (lp.has & LP_EVERY)   s.accentEvery = lp.every;
  const bool fx = (lp.has & LP_EFFECT) != 0;
  _setLook(hp, fx ? lp.effect : s.effect, fx ? lp.nativeCode : s.nativeCode,
           (lp.has & LP_COLOR) ? lp.color : s.color, (lp.has & LP_SPEED) ? lp.speed : s.speed, now);
  (void)u;
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
  ContractUnitState& s = gUnit[hp].s;
  const ScoreEntry& e = gScore[hp][idx];
  s.bright = e.bright;
  s.beatMod = e.beatMod; s.accentMode = e.accentMode; s.accentEvery = e.accentEvery;
  s.curSection = (int8_t)idx; s.sectionStart = e.atBeat;
  // the last section gets a long tail so its span (the am=3 build ramp) is never zero-width
  s.sectionEnd = (idx + 1 < s.scoreLen) ? gScore[hp][idx + 1].atBeat : (e.atBeat + 9999);
  _setLook(hp, e.effect, e.nativeCode, e.color, e.speed, now);
}

// ------------------------------------------------------------- show reset (L1) ---
// L1: this board has ONE clock for three units. A reset stops it when no unit still holds a
// score or a look.
static inline void _stopClockIfUnused() {
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) if (contractUnitNeedsClock(gUnit[hp].s)) return;
  gBeat = BeatClock();
}
// The three show boundaries: 's' M:v=show, 'x' X, 'i' M:v=idle (also the watchdog and
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
  // S6: the owner's level. While a ride is on the strip sits below it, and the raise back to it
  // may have to wait for the gate (_restoreRide), so the ceiling is the one the ride was taken from.
  const uint8_t ceiling = (u.ride != 255) ? s.ceil : neoStrips[hp].getBrightness();
  _nativeLeave(hp);
  _restoreRide(hp);                                     // B never outlives a show
  if (kind == 's')      contractOnShow(s, ceiling, wdSec);
  else if (kind == 'x') contractOnStop(s, ceiling);
  else                  contractOnIdle(s, ceiling);
  u.overNative = false;  u.level = 0;  u.lastEnvBright = -1;
  if (kind != 'x') u.twitchFreed = false;               // a new show holds again; X stays in its mode
  _stopClockIfUnused();
  if (kind == 'x') {
    _dark(hp);
    if (!s.inShow) { s.base = CB_NONE; _rearmTwitch(hp); }
  } else if (heldLeds) {
    _nativeLedIdle(hp);
  } else if (kind == 'i' && armed && dnServoTwitchOverdue(hp)) {
    resetHPtwitch(hp);                                  // overdue only, as everywhere a hold ends
  }
}

// v1.3 L6, native take-over: a button on the controller always works. main.cpp calls this for
// each unit a stock LED command or a stock sequence (S<n>) addresses, BEFORE the command runs.
// If the contract has anything on that unit (an armed show, a score, a look, held or up, an overlay), the show
// ends there exactly as M:v=idle ends it: score cleared, hold released, both twitch timers a
// full interval ahead. The other units play on. A stock SERVO command and H<unit>B<level> are
// not take-overs here: this layer never has the servos, and the brightness command moves the
// show's ceiling (S6) instead.
inline void contractNativeTakeover(uint8_t hp) {
  const ContractUnitState& s = gUnit[hp].s;
  if (s.inShow || s.scoreLen != 0 || s.base == CB_LOOK || s.base == CB_NATIVE || s.pulseActive || s.holdPending)
    _boundary(hp, 'i', 0);                              // (holdPending: a live look S4 is holding would come up after the button)
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
        // Section 7: a scored A carries its look. With no i= (or one this firmware does not know)
        // the line is dropped: an entry is a whole section, not a patch.
        if (!pr.hasEffect) return;
        // (Section 7, in the core's scoreAccept(): a unit whose load failed verb V takes no scored line
        // until the next M:v=show, and a line delivered twice changes nothing.)
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
      u.rideWant = pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : 255);
      _rideTry(hp);                                    // a fall now; a raise when the gate allows it (S9)
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
      if (pr.mode == 's')      _boundary(hp, 's', pr.hasWd ? pr.wd : 0);   // L2: wd= arms the watchdog
      else if (pr.mode == 'i') _boundary(hp, 'i', 0);
      break;

    case CV_VERIFY:                                    // section 7: is the score this unit holds the one the host sent?
      if (!pr.hasN || !pr.hasK) break;
      if (scoreVerify(s, gScore[hp], pr.n, pr.k) == 2) _boundary(hp, 'i', 0);   // no: back to the stock look,
      break;                                                                    // visibly (the reset keeps ok=2 for Q)

    case CV_QUERY: {                                   // section 10
      // Answered only when addressed to this class AND one unit AND carrying r=<route>; the reply
      // is routed (";S0!..." or ";W<n>,;S0!...") so the WCB never broadcasts it. Any other Q is
      // silent (it has already fed the watchdog: it is the host's keepalive).
      if (!contractQueryAnswerable(p)) break;
      gQueryRoute[hp] = pr.route;
      gQueryAsked |= (uint8_t)(1u << hp);
      break;                                           // contractBeatTick() sends it
    }

    default: break;                                    // CV_CLOCK is the board's, not a unit's: applyContract()
  }
}

// Fan a parsed command to the addressed jewel(s) per class/unit.
inline void applyContract(const ParsedContract& p, uint16_t lineCrc = 0) {
  if (p.cls != 'H' && p.cls != '*') return;            // not a Holoprojector board
  gLastLineMs = millis();                              // L2: any well-formed line for this board is the
                                                       // host's keepalive (any unit, any verb, a Q nobody answers)
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
  if (p.verb == CV_MODE && p.params.mode == 's') {      // the board-wide watchdog follows the show lines
    if (p.params.hasWd && p.params.wd) gBoardWd = p.params.wd;
    else if (all) gBoardWd = 0;
  }
  for (uint8_t hp = 0; hp < HPCOUNT; hp++)
    if (to[hp]) applyContractToUnit(hp, p, lineCrc);
  if (p.verb == CV_MODE && p.params.mode == 'i' &&
      !gUnit[0].s.inShow && !gUnit[1].s.inShow && !gUnit[2].s.inShow) gBoardWd = 0;   // the host ended the show itself
}

// ------------------------------ beat tick + score switch + board-side accent ---
// Call once per loop(), before the per-hp render block. It runs every pass, clock or no clock.
inline void contractBeatTick() {
  const uint32_t now = millis();
  // One waiting Q reply a pass: whole, in one write that cannot wait (the TX ring has room for the
  // longest reply), and only when no WDP announce is partly sent, so neither line lands inside the other.
  if (gQueryAsked && !wdpInFlight() && Serial.availableForWrite() >= (int)CONTRACT_REPLY_MAX) {
    uint8_t hp = 0;
    while (!(gQueryAsked & (1u << hp))) hp++;
    gQueryAsked &= (uint8_t)~(1u << hp);
    char buf[CONTRACT_REPLY_MAX];
    const uint8_t n = contractFormatQuery(buf, sizeof(buf), gQueryRoute[hp], 'H', _unitChar(hp), FLTHY_SCORE_CAP,
                                          gUnit[hp].s.scoreLen, gUnit[hp].s.loadOk, gBeat.running ? gBeat.bpm : 0);
    if (!wdpInFlight()) Serial.write((const uint8_t*)buf, n);
  }
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    FlthyUnit& u = gUnit[hp];
    ContractUnitState& s = u.s;
    // S9: this pass the layer has the jewel; and a frame the field gate held back is offered to
    // it again every pass (a look that paints once would otherwise stay dark).
    if (_hasLeds(hp)) _touch(hp);
    if (u.nativeWait) { if (s.base == CB_NATIVE) _showBase(hp); else u.nativeWait = false; }   // S10: ask again
    // A held frame is offered again only as what it was. When the LED function has changed since
    // (an overlay took the slot, a stock look started), the frame in the buffer is not the new
    // owner's, and offering it under the new owner's look would show it as that look.
    if (gLatch[hp].pending && LED_command[hp].LEDFunction != gLatch[hp].pendingFn) gLatch[hp].pending = false;
    if (gLatch[hp].pending && !u.nativeWait) neoStrips[hp].show();
    if (u.rideWant != u.ride) _rideTry(hp);              // a verb-B raise the gate is holding
    // L2: the host armed a watchdog (M:v=show,wd=<s>) and has been silent that long: the unit
    // idles itself, exactly as M:v=idle would. X does not disarm it; idle and a new show do.
    if (contractWatchdogExpired(s, gLastLineMs, now)) _boundary(hp, 'i', 0);
    _expireOverlay(hp, now);
    // A stock command, or its |<n> timer, took the render slot: the jewel is the stock
    // firmware's again. Stop holding the unit (outside a show its auto twitch would never
    // come back) and re-arm the timers so letting go moves nothing in this pass.
    if ((s.base == CB_LOOK || s.pulseActive) && !_slotIsOurs(hp)) {
      s.pulseActive = false;  u.overNative = false;
      if (s.base == CB_LOOK) s.base = s.inShow ? CB_DARK : CB_NONE;
      if (!s.inShow) _rearmTwitch(hp);
    }
    if (s.base == CB_NATIVE && !u.nativeWait && !_nativeIsUp(hp)) { s.base = s.inShow ? CB_DARK : CB_NONE; u.nativeTold = false; }
    ContractLookPatch lp;
    if (liveLookDue(s, now, lp)) _applyPatch(hp, lp, now);   // S4: a held live look's window ended
  }
  // L2, board-wide (see gBoardWd): the host that armed a watched show has gone silent. Every unit
  // with anything of the contract's on it is idled, except one in a show armed with no watchdog.
  if (gBoardWd && (uint32_t)(now - gLastLineMs) >= (uint32_t)gBoardWd * 1000UL) {
    gBoardWd = 0;
    for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
      const ContractUnitState& s = gUnit[hp].s;
      if (s.inShow && s.wdSec == 0) continue;
      if (s.inShow || s.scoreLen != 0 || s.base != CB_NONE || s.pulseActive || s.holdPending || _slotIsOurs(hp))
        _boundary(hp, 'i', 0);
    }
  }
  if (!gBeat.running) return;                          // no clock (or held, C6): the score does not move
  const BeatPos bp = beatPosAt(gBeat, now);
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    ContractUnitState& s = gUnit[hp].s;
    if (s.scoreLen == 0) continue;
    int idx = scoreActiveIndex(gScore[hp], s.scoreLen, bp.beatIndex);
    idx = scoreCursor(s.curSection, idx, s.pinForward);     // C2
    if (idx < 0 || idx == s.curSection) continue;
    if (!contractSectionMayApply(s, now)) continue;        // S4 for sections too (a C seek can flip between two every few ms);
    _enterSection(hp, idx, now);                           // a later tick takes it if the beat is still in it
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
  if (!contractParse(afterBang, p)) return;
  // A scored line is held with the CRC of its own text: verb V compares the XOR of them (section 7).
  applyContract(p, (p.verb == CV_ANIMATE && p.params.hasAt) ? contractCrc16(afterBang) : 0);
}
