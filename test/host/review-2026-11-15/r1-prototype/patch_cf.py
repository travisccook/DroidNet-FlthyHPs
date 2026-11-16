# Level-aware prototype of the Flthy-side R1 changes to src/contract/ContractFlthy.h.
#   patch_cf.py <file> <levels>     levels: any of A (lifecycle), G (letting go), B (native), C (L3), D (brightness), E (pulse, d=)
import sys
p, L = sys.argv[1], sys.argv[2]
s = open(p).read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (s.count(old), old[:80])
    s = s.replace(old, new)
def lines(*pairs):   # ('text', cond) -> joined text of the lines whose cond holds
    return "".join(t for t, c in pairs if c)
A, G, B, C, D, E = [x in L for x in "AGBCDE"]

if D:
    rep("static const uint8_t  FLTHY_SAFE_MAX_BRIGHT = 200;   // conservative jewel clamp (fork spec §11)\n", "")
    rep("  uint8_t        brightBase = FLTHY_SAFE_MAX_BRIGHT;   // b (pre-envelope)",
        "  uint8_t        brightBase = 255;        // b (pre-envelope): a fraction of the strip's own brightness (S6)")
    rep("  uint8_t        pulseBright  = FLTHY_SAFE_MAX_BRIGHT;", "  uint8_t        pulseBright  = 255;")
    rep("""static inline uint8_t _clampBright(int v) {
  return (uint8_t)(v < 0 ? 0 : (v > FLTHY_SAFE_MAX_BRIGHT ? FLTHY_SAFE_MAX_BRIGHT : v));
}""",
"""static inline uint8_t _clampBright(int v) {
  return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}""")
if B or C:
    rep("""  int32_t        lastAccentBeat = BEAT_NONE;
};""",
"""  int32_t        lastAccentBeat = BEAT_NONE;
""" + lines(
("""  // A native look this layer started (i=native:<n>, allow-listed). No accent may replace it.
  bool           nativeLook   = false;
  byte           nativeFn     = 255;
""", B),
("""  // L3: a verb P with no show armed overlays the STOCK look and gives it back afterwards.
  bool           overNative   = false;
  byte           savedFn      = 255;              // the stock LEDFunction under the overlay
  uint8_t        savedPx[NEO_JEWEL_LEDS * 4];     // its frame, raw (RGB uses 21 of the 28 bytes)
""", C)) + "};")
if C:
    rep("static const uint8_t  FLTHY_FX_BASE         = 100;   // LEDFunction = 100 + effectId (101..115)\n",
        "static const uint8_t  FLTHY_FX_BASE         = 100;   // LEDFunction = 100 + effectId (101..115)\n"
        "#ifdef NEO_JEWEL_RGBW\nstatic const uint8_t  FLTHY_PX_BYTES        = 4;     // raw bytes a pixel\n#else\nstatic const uint8_t  FLTHY_PX_BYTES        = 3;\n#endif\n")
if A:
    rep("static bool       gShowMode = false;\n", "")
    a = s.index("// ------------------------------------------- show-mode interlock save-state ---")
    b = s.index("// Phase-2 score (per unit, sorted asc by atBeat via contract_core scoreInsert)")
    s = s[:a] + """// ------------------------------------------------------- show-mode interlock ---
// An armed show HOLDS a unit: main.cpp's two auto-twitch gates ask contractHolds(), so no
// LED twitch and no SERVO twitch starts while it does (this fork is LED-only). The layer
// never writes enableTwitchLED[], enableTwitchHP[] or offcoloroverride[]: they stay the
// operator's, so a native 96-99 / S command sent during a show is still in force after it.
static bool    gInterlocked[HPCOUNT] = {false, false, false};

""" + s[b:]
    rep("""// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB""",
"""// Is the LED render slot one of ours (101..115)?
static inline bool _slotIsOurs(uint8_t hp) {
  byte f = LED_command[hp].LEDFunction;
  return f >= 101 && f <= (byte)(FLTHY_FX_BASE + (uint8_t)CE_TWINKLE);
}
// main.cpp's auto-twitch gates: true while this layer has the unit. LED twitch and servo
// twitch both wait.
inline bool contractHolds(uint8_t hp) {
  return gInterlocked[hp] || gUnit[hp].active;
}
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
// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB""")
    rep("""  u.pulseActive = false;
  u.lastEnvBright = -1;                                 // force first repaint

  if (eff == CE_NATIVE && nativeCode >= 0) {""",
"""  u.pulseActive = false;
  u.lastEnvBright = -1;                                 // force first repaint
  LED_command[hp].LEDHalt = -1;                         // a stock |<n> or auto-twitch timer still
                                                        // running would flush this look to black

  if (eff == CE_NATIVE && nativeCode >= 0) {""")
    a = s.index("    case CV_STOP:\n")
    b = s.index("    case CV_QUERY: {")
    s = s[:a] + """    case CV_STOP:                                      // L1: reset, black out, stay in this mode
      _unitReset(hp);
      LED_command[hp].LEDFunction = 255;               // the stock "idle, dark" value, never 0:
      LED_command[hp].LEDHalt     = -1;                // 0 is "do nothing", which also blocks the
      ledOFF(hp);                                      // stock auto twitch for good
      if (!gInterlocked[hp]) _rearmTwitch(hp);         // stay dark for a full twitch interval
      break;

    case CV_MODE:
      if (pr.mode == 's') {                            // L1: reset, then arm the show
        _unitReset(hp);
        gInterlocked[hp] = true;                       // holds both auto twitches (LED-only)
        LED_command[hp].LEDFunction = 255;             // cancel a stock look still running...
        LED_command[hp].LEDHalt     = -1;              // ...and the timer that would end it later
        ledOFF(hp);                                    // baseline dark
      } else if (pr.mode == 'i') {                     // L1: reset, then give the board back
        _idleUnit(hp);
      }
      break;

""" + s[b:]
    rep("// ------------------------------------------------------------- verb dispatch ---",
"""// ------------------------------------------------------------- show reset (L1) ---
// The Flthy half of the one show reset: everything a show can leave behind on a unit.
static inline void _unitReset(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  scoreClear(gScoreCount[hp], gScoreActive[hp]);
  u.active = false;  u.pulseActive = false;
  u.effect = CE_SOLID;  u.color = ContractRGB{0, 0, 0};  u.speed = 128;  u.brightBase = FLTHY_SAFE_MAX_BRIGHT;
  u.beatMod = 0;  u.accentMode = 0;  u.durMs = 0;  u.level = 0;
  u.sectionStart = 0;  u.sectionEnd = 0;
  u.lastAccentBeat = BEAT_NONE;
  u.lastEnvBright  = -1;
}
// M:v=idle (and, later, the watchdog and a failed verb V) end here. A unit this layer is not
// holding is left alone, so a stray idle cannot flush a look the operator started.
static inline void _idleUnit(uint8_t hp) {
  bool held = contractHolds(hp) || _slotIsOurs(hp);
  _unitReset(hp);
  gInterlocked[hp] = false;
  if (held) _nativeLedIdle(hp);                        // off colour, both twitch timers from now
}

// ------------------------------------------------------------- verb dispatch ---""")
    rep("  if (p.verb == CV_STOP && all) gBeat.running = false; // !**X also stops the clock",
"""  // L1 "stop the beat clock": this board has ONE clock for three units, so a reset stops it
  // when it addressed all of them, or when no unit is left with a show, a look or a score.
  if (p.verb == CV_STOP || (p.verb == CV_MODE && (p.params.mode == 's' || p.params.mode == 'i'))) {
    bool anyLeft = false;
    for (uint8_t hp = 0; hp < HPCOUNT; hp++)
      if (gInterlocked[hp] || gUnit[hp].active || gScoreCount[hp] > 0) anyLeft = true;
    if (all || !anyLeft) gBeat.running = false;
  }""")
if G:
    rep("  return gInterlocked[hp] || gUnit[hp].active;\n", "  return gInterlocked[hp] || gUnit[hp].active || gUnit[hp].pulseActive;\n")
    rep("""// Called from the LED render switch (cases 101..115) once per loop for unit hp.
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
""",
"""// ms-duration revert (true sub-second holds; parallels the native seconds pipe). In a show the
// unit goes dark; outside one the jewel goes back to the stock firmware.
static inline void _expireDur(uint8_t hp, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  if (!(u.active && u.durMs > 0 && (uint32_t)(now - u.startMs) >= u.durMs)) return;
  u.active = false;
  u.durMs  = 0;                                          // a deadline never outlives its look
  u.effect = CE_OFF;
  u.lastEnvBright = -1;
  if (gInterlocked[hp]) LED_command[hp].LEDFunction = _fxCode(CE_OFF);
  else                  _nativeLedIdle(hp);
}

// Called from the LED render switch (cases 101..115) once per loop for unit hp.
inline void contractRenderHP(uint8_t hp) {
  FlthyUnit& u = gUnit[hp];
  uint32_t now = millis();

  _expireDur(hp, now);
  if (!_slotIsOurs(hp)) return;                          // the deadline just gave the jewel back
""")
    rep("""inline void contractBeatTick() {
  if (!gBeat.running) return;
  BeatPos bp = beatPosAt(gBeat, millis());""",
"""inline void contractBeatTick() {
  uint32_t now = millis();
  for (uint8_t hp = 0; hp < HPCOUNT; hp++) {
    FlthyUnit& u = gUnit[hp];
    _expireDur(hp, now);
    // A stock command, or its |<n> timer, took the render slot: the jewel is the stock
    // firmware's again. Stop holding the unit (outside a show its auto twitch would never
    // come back) and re-arm the timers so letting go moves nothing in this pass.
    if ((u.active || u.pulseActive) && !_slotIsOurs(hp)) {
      u.active = false;  u.pulseActive = false;
      if (!gInterlocked[hp]) _rearmTwitch(hp);
    }
  }
  if (!gBeat.running) return;
  BeatPos bp = beatPosAt(gBeat, now);""")
if B:
    rep("// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB",
"""// v1.3 S7, R1: the native looks this layer may start. They need no option bytes and no
// counters: 1 = Leia (fixed blue colour projector), 6 = rainbow.
static inline bool flthyNativeAllowed(int code) { return code == 1 || code == 6; }
// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB""")
    rep("""  if (!strobeCoolDownExpired(u.pulseLastMs, now, 2 * FLTHY_STROBE_MIN_MS)) return false;
  if (durMs < FLTHY_STROBE_MIN_MS) durMs = FLTHY_STROBE_MIN_MS;   // a state shorter than the""",
"""  if (!strobeCoolDownExpired(u.pulseLastMs, now, 2 * FLTHY_STROBE_MIN_MS)) return false;
  // v1.3 S7 / section 8: an accent never restarts or replaces a native sequence. In R1 a
  // native section simply has no accent (R2 adds ae=kick where the look can be scaled).
  if (u.nativeLook && LED_command[hp].LEDFunction == u.nativeFn) return false;
  if (durMs < FLTHY_STROBE_MIN_MS) durMs = FLTHY_STROBE_MIN_MS;   // a state shorter than the""")
    rep("""  if (eff == CE_NATIVE && nativeCode >= 0) {
    // escape hatch: hand off to a native LED function (0..7); NOT a contract render
    u.active = false;
    LED_command[hp].LEDFunction = (byte)nativeCode;
    return;
  }
""",
"""  u.nativeLook = false;
  if (eff == CE_NATIVE) {
    // escape hatch: hand off to an allow-listed native LED function; NOT a contract render.
    // Both callers have already refused any other code (flthyNativeAllowed).
    u.active     = false;
    u.nativeLook = true;
    u.nativeFn   = (byte)nativeCode;
    LED_command[hp].LEDFunction = (byte)nativeCode;
    return;
  }
""")
    rep("""      if (pr.hasAt) {                                  // Phase-2: schedule, don't apply now
        ScoreEntry e = _entryFrom(pr);""",
"""      // v1.3 S7: a native code off the allow-list is ignored, live or scored (a refused
      // scored line is not "accepted", so verb V reports the load bad).
      if (pr.hasEffect && pr.effect == CE_NATIVE && !flthyNativeAllowed(pr.nativeCode)) return;
      if (pr.hasAt) {                                  // Phase-2: schedule, don't apply now
        ScoreEntry e = _entryFrom(pr);""")
    rep("  u.active = false;  u.pulseActive = false;\n  u.effect = CE_SOLID;", "  u.active = false;  u.pulseActive = false;  u.nativeLook = false;\n  u.effect = CE_SOLID;")
    rep("  bool held = contractHolds(hp) || _slotIsOurs(hp);",
        "  FlthyUnit& u = gUnit[hp];\n  bool held = contractHolds(hp) || _slotIsOurs(hp) ||\n              (u.nativeLook && LED_command[hp].LEDFunction == u.nativeFn);")
    rep("""      if (!gInterlocked[hp]) _rearmTwitch(hp);
    }
  }
  if (!gBeat.running) return;""",
"""      if (!gInterlocked[hp]) _rearmTwitch(hp);
    }
    if (u.nativeLook && LED_command[hp].LEDFunction != u.nativeFn) u.nativeLook = false;
  }
  if (!gBeat.running) return;""")
if C:
    rep("""// Called from the LED render switch (cases 101..115) once per loop for unit hp.""",
"""// Overlay expiry. Over a contract look the base repaints; over the STOCK look (L3) the stock
// function and its frame come back exactly as they were.
static inline void _expireOverlay(uint8_t hp, uint32_t now) {
  FlthyUnit& u = gUnit[hp];
  if (!u.pulseActive || (uint32_t)(now - u.pulseStartMs) < u.pulseDurMs) return;
  u.pulseActive   = false;
  u.lastEnvBright = -1;                                  // force a base repaint
  if (u.overNative) {
    u.overNative = false;
    if (_slotIsOurs(hp)) {                               // still ours: nothing native took it meanwhile
      LED_command[hp].LEDFunction = u.savedFn;
      memcpy(neoStrips[hp].getPixels(), u.savedPx, (size_t)NEO_JEWEL_LEDS * FLTHY_PX_BYTES);
      neoStrips[hp].show();
    }
  }
}

// Called from the LED render switch (cases 101..115) once per loop for unit hp.""")
    rep("""  if (u.pulseActive) {
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
""",
"""  _expireOverlay(hp, now);
  if (!_slotIsOurs(hp)) return;                          // the overlay just gave the stock look back
  if (u.pulseActive) {
    u.lastEnvBright = -1;                // the MEMOISING branches (off/meter/solid) skip a
                                         // repaint when the level is unchanged — without this
                                         // the accent would never actually be drawn over a
                                         // same-level base look
    _renderLook(hp, (u.pulseFx == CE_NONE) ? CE_SOLID : u.pulseFx,
                u.pulseColor, u.pulseBright, u.pulseStartMs);
    return;
  }
""")
    rep("""  if (LED_command[hp].LEDFunction < 101 || LED_command[hp].LEDFunction > FLTHY_FX_MAX)
    LED_command[hp].LEDFunction = _fxCode(u.effect);  // ensure a contract render slot runs
  return true;""",
"""  if (!_slotIsOurs(hp)) {
    if (!gInterlocked[hp] && !u.overNative) {          // L3: no show armed -> overlay the STOCK look
      u.overNative = true;
      u.savedFn    = LED_command[hp].LEDFunction;
      memcpy(u.savedPx, neoStrips[hp].getPixels(), (size_t)NEO_JEWEL_LEDS * FLTHY_PX_BYTES);
    }
    u.effect = CE_OFF;                                  // nothing of ours was running underneath
    LED_command[hp].LEDFunction = _fxCode(CE_OFF);      // ensure a contract render slot runs
  }
  return true;""")
    rep("""  u.pulseActive = false;
  u.lastEnvBright = -1;                                 // force first repaint
  LED_command[hp].LEDHalt = -1; """,
"""  u.pulseActive = false;
  u.overNative  = false;                                // a look replaces whatever an overlay sat on
  u.lastEnvBright = -1;                                 // force first repaint
  LED_command[hp].LEDHalt = -1; """)
    rep("    _expireDur(hp, now);\n    // A stock command,", "    _expireDur(hp, now);\n    _expireOverlay(hp, now);\n    // A stock command,")
    rep("      u.active = false;  u.pulseActive = false;\n      if (!gInterlocked[hp]) _rearmTwitch(hp);",
        "      u.active = false;  u.pulseActive = false;  u.overNative = false;\n      if (!gInterlocked[hp]) _rearmTwitch(hp);")
    rep("  u.effect = CE_SOLID;  u.color = ContractRGB{0, 0, 0};", "  u.overNative = false;\n  u.effect = CE_SOLID;  u.color = ContractRGB{0, 0, 0};")
if D:
    rep("static bool    gInterlocked[HPCOUNT] = {false, false, false};\n",
"""static bool    gInterlocked[HPCOUNT] = {false, false, false};
// v1.3 S6: the brightness ceiling is the strip's own level when the show arms (BRIGHT, or
// what H<unit>B<level> last set). Verb B rides below it and never outlives the show.
static uint8_t gCeil[HPCOUNT] = {0, 0, 0};
static uint8_t gRide[HPCOUNT] = {255, 255, 255};
""")
    rep("// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB",
"""// The native brightness command (H<unit>B<level>) calls this: the owner's new level is the
// ceiling from now on, and any verb-B ride is over.
inline void contractOwnerBrightness(uint8_t hp, uint8_t level) {
  gCeil[hp] = level;
  gRide[hp] = 255;
  gUnit[hp].lastEnvBright = -1;                         // memoised looks repaint at the new level
}
// pack an RGB scaled by a 0..255 brightness multiplier into 0xRRGGBB (NEO_GRB""")
    rep("""    case CV_BRIGHT: {                                  // master volatile ride (never EEPROM)
      uint8_t b = _clampBright(pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : u.brightBase));
      neoStrips[hp].setBrightness(b);                  // runtime, re-scales at next show() (main.cpp:748)
      break;
    }""",
"""    case CV_BRIGHT: {                                  // master ride, inside an armed show only (S6)
      if (!gInterlocked[hp]) break;                    // outside a show H<unit>B<level> is the control
      gRide[hp] = pr.hasBright ? pr.bright : (pr.hasLevel ? pr.level : 255);
      // A fraction of the ceiling, never above it. setBrightness() rescales the RAM buffer at
      // once and lossily, so every look is made to repaint from its own colour.
      neoStrips[hp].setBrightness((uint8_t)(((uint16_t)gCeil[hp] * gRide[hp]) / 255));
      u.lastEnvBright = -1;
      break;
    }""")
    rep("        gInterlocked[hp] = true;                       // holds both auto twitches (LED-only)\n",
        "        gInterlocked[hp] = true;                       // holds both auto twitches (LED-only)\n"
        "        gCeil[hp] = neoStrips[hp].getBrightness();     // S6: the owner's level is the ceiling\n")
    rep("  scoreClear(gScoreCount[hp], gScoreActive[hp]);\n  u.active = false;",
        "  scoreClear(gScoreCount[hp], gScoreActive[hp]);\n"
        "  if (gInterlocked[hp] && gRide[hp] != 255) neoStrips[hp].setBrightness(gCeil[hp]);   // B never outlives a show\n"
        "  gRide[hp] = 255;\n  u.active = false;")
    rep("u.brightBase = FLTHY_SAFE_MAX_BRIGHT;\n", "u.brightBase = 255;\n")
if E:
    rep("""      uint32_t period = (uint32_t)map((long)u.speed, 0, 255, 2000, 240);
      if (period < 2) period = 2;""",
"""      uint32_t period = (uint32_t)map((long)u.speed, 0, 255, 2000, 240);
      // v1.3 S2: a full dark-bright-dark cycle is a beat-rate change like any other, so it
      // may not recur faster than 2 x the min state (340 ms). Only s >= 242 is affected.
      if (period < 2 * FLTHY_STROBE_MIN_MS) period = 2 * FLTHY_STROBE_MIN_MS;""")
    rep("      uint32_t d  = pr.hasDur     ? pr.durMs   : u.durMs;",
        "      // A line with i= starts a NEW look, which holds unless it brings its own d=. Only an\n"
        "      // i-less patch keeps the running look's deadline.\n"
        "      uint32_t d  = pr.hasDur     ? pr.durMs   : (pr.hasEffect ? 0 : u.durMs);")
open(p, 'w').write(s)
print("ContractFlthy.h patched, levels", L)
