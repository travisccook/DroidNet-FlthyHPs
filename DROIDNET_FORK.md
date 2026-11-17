# DroidNet fork notes — FlthyHPs

Working notes for the fork itself. The user-facing documentation, attribution, and the
"never run on hardware" warning are in [README.md](README.md) — read that first.

This is an **additive** fork of FlthyHPs by Ryan Sondgeroth
(<https://github.com/ryan-sondgeroth/FlthyHPs>). His firmware is the repository; our layer
is `src/contract/*`, `src/droidnet/*`, `src/wdp/*`, `test/host/*`, and a handful of hooks in
`src/main.cpp`.

## What the fork does

Implements the DroidNet **Driveable-Animation Contract** on the `!` command prefix, so
Cantina Studio can drive the three holoprojector jewels (F/R/T) with arbitrary RGB,
beat-locked accents, a host-seeded beat clock, and autonomous board-side section playback.

**LED effects only.** The `!` path never writes `HP_command[]` and never calls
`positionHP` / `twitchHP` / `wagHP` / `RCHP`. An armed show HOLDS a unit: `main.cpp`'s two auto-twitch
gates ask `contractServoTwitchHeld(i)` and `contractLedTwitchHeld(i)`, first in their tests. The layer
never writes `enableTwitchHP[]`, `enableTwitchLED[]` or `offcoloroverride[]`. When a hold ends,
whichever way, the gate itself puts an overdue twitch timer a full interval ahead before the stock
test reads it (one that is not due yet is left alone, or short holds would starve the twitch), so no pass that handles a `!` line, ends an overlay or reaches a score section starts a
servo twitch. A stock line too short to carry a type or function has none (`255`), so a bare `X` or
`A` left over from a corrupted contract line cannot replay the last stock servo command.
No servo motion, ever. `test/host/sim` checks all of it on the real `src/main.cpp`, on every
simulated pass.

Every native command (`DT##` LED, `DT1##` servo, `S#` sequences, the auto-twitch system, the
I2C intake) still behaves exactly as it did before.

It also adds **one native command**, `H<unit>B<level>` (library id `flthy.led.brightness`), which
sets jewel brightness 1-255 in RAM. See "Native brightness" below.

And it **announces itself to the WCB** (WDP device announce) every 25-30 s. See "WDP device
announce" below.

## Status

- **Contract v1.3, release 1: built.** The shared core at revision CA1.5, and this board's layer on
  the core's `ContractUnitState`: show lifecycle (arm, begin, stop, idle), the watchdog, a 32-section
  score with a load check (verb `V`), the routed query reply (verb `Q:r=`), brightness as a fraction
  of the strip's level, the native allow-list (1 and 6), native take-over, and `flash`, `pulse` and
  `scan` on the core's shared timing. See "Contract v1.3, release 1" below.
- **Six parametric effects: built.** comet, chase, wipe, gradient, colorcycle, twinkle —
  rendered around the jewel ring.
- **Host verification: green.** `bash test/host/run.sh` → 139,791 parser checks, 2,874,222 v1.3 core
  checks (run twice: plain, and with stand-ins for the AVR flash-string hooks), a type-check of
  `ContractFlthy.h` against `test/host/mock_flthy.h`, 1,617,274 contract fuzz checks, 77/77 native
  brightness parser checks and 2,965,122 native brightness fuzz checks, 37,506 WDP announce core
  checks and 19 adapter checks (run twice).
- **Whole-firmware simulation: 91 scenarios pass.** Stage 2b of `run.sh`
  (`test/host/sim`) runs the real `src/main.cpp` on a fake Arduino core: the 9600-baud wire with its
  receive ring (bytes are lost when it is full) and its transmit ring (a write blocks when it is
  full), `pulseIn()` blocking as it does in RC mode, and loop passes of random length. Origin is
  what the host sent: a corrupted `!` line stays contract traffic for every byte.
  `test/host/sim/xfail.txt` names scenarios that record a known defect and must fail; it is empty.
- **Independent review of release 1 (2026-11-16): seven findings, all closed.** A bare `X`/`A`
  replaying a servo command; a hold ending without re-arming the twitch; S9; the watchdog lost by a
  unit that left the show; four spec sentences in the shared core (closed by CA1.2); the blocking `Q`
  reply; `A199` in a show. `test/host/sim/scenarios_review.inc` holds a scenario for each, the
  single-fault sweep (45,786 corruptions) and the S9 property test (150 hostile streams, the latched
  frames measured by three observers).
- **Second round of that review: three findings closed here, a fourth in the core.** Verb `B` flashing
  stock light on an armed unit (S9); the hold-end re-arm starving the owner's auto twitch; one `Q`
  reply slot for three units. The fourth, swings under a quarter of the ceiling, is closed by
  core revision CA1.4: the gate's step is a tenth of the ceiling. The property test's oracle
  has a strict mode since: every relight counts, whoever drew the pixels, apart from a stock
  command's own for 400 ms.
- **CA1.5 round.** The gate's step is a tenth of the ceiling in two measures, S4 holds every change
  of look or colour, native looks are of two kinds (S10), the same look again does not restart (S11),
  and a verb-`B` raise waits instead of the frames. The property test measures luminance and colour
  as well, with observers a step apart, and the held-frames table below is a scenario.
- **The soak round.** The six streams of seed base 800000 that broke after the CA1.5 round were
  replayed (`S9_REPLAY=1`: the core's two gates asked afresh about every frame a jewel latched, at
  the board's own clock). Three were the oracle's: the photometer looked once a loop pass and missed
  a frame that was up for under a pass; it now measures every latched frame. Three were the layer's,
  and the soaks that followed named four more faults of the layer; each has its smallest case as a
  scenario and is fixed (the hand-back window, verb `B` over stock light, the stock light's own
  colours, a colour that arrives dim, the rainbow's luminance, Leia's drifting colour). Sparkle is
  redrawn. Soak after that, 4,000 streams at each of three seed bases (800000, 1200000, 1600000):
  none of the 12,000 broke. Three fresh seed bases then broke on 6 of 12,000, all one more fault of
  the layer, older than this round: a native look the contract had started stayed up while another
  native look waited for its window, and a boundary in that wait left it running as stock light
  (`s10_idle_while_a_native_look_waits_takes_down_the_look_that_stayed_up`). With that fixed: 4,000
  streams at each of nine seed bases (the six above and 3200000, 3600000, 4000000, which nothing
  was written against), and none of the 36,000 breaks the property. That is a simulation result,
  on an oracle whose colour test starts at 4 levels; it says nothing of a board.
- **CA1.6 round.** Core `17bb425`: one rise is one onset (coloured fades are no longer held), verb `L`
  held in the core, native looks told with two figures, the look gate by the frame's brightest LED,
  a comet's lap of 680 ms. S12, a part of the field: the property test watches twenty-two regions of
  each jewel; sparkle, twinkle, chase and comet are drawn on a bed, Leia's brightening frames are
  340 ms apart, and scan and sparkle no longer black the jewel out at their start. The case the
  logics fork records as a known failure (a comet and a full look by turns) is closed on the jewel
  by the comet's bed. Soak: no stream of 12,000 breaks S9 or S4; 63 light one part 4 times in a
  second (see "A part of the jewel" below).
- **Build:** flash 44,372 B of 253,952; SRAM 4,673 B of 8,192 (the score tables are 2,112 B).
  `stack_report.py`: worst-case stack 440 B of the 3,519 B left, 3,079 B spare. The heap (Arduino
  `String`) shares that space. (The tool takes any two bytes of `.data` equal to a function's address
  for a function pointer; on one build of the fix round two letters of "Flthy HP Controller" matched
  `RCHP()` and it called the bound "not sound" over an interrupt path that does not exist.)
- **Prebuilt image: older than this code.** `firmware/` holds the image built from `9d28c52`. No
  image has been built from contract v1.3; `firmware/build-image.sh` does that at release.
- **Hardware verification: NONE.** This has never been flashed to a board. Not once. Nothing
  about flash fit, loop stability, LED timing, or how any of it actually looks has been
  confirmed on real hardware. Bench-compile and bench-test before this goes anywhere near a
  droid.

## Fork facts

- **Fork head:** `main` in this repository. Baseline import is commit `5385a7c`; every commit
  after it is the DroidNet layer.
- **Seeded from:** the vendored copy of FlthyHPs (v1.8 / sketch header v1.81) in the owner's
  private working collection of firmware for the C2B5 droid, 2026-07-12, git-tracked files
  only. That collection is private; upstream is Ryan's repo, linked above.
- **MCU/toolchain:** ATmega2560 (Arduino Mega ADK), Arduino/AVR via PlatformIO. 8 KB SRAM /
  256 KB flash — the roomiest of the three boards this contract targets.
- **Role:** one of three board forks in the Cantina dome light-show program (RSeries Logics,
  PSI Pro, FlthyHPs).
- **GitHub remote:** <https://github.com/travisccook/DroidNet-FlthyHPs> (public).

## Shared core

`src/contract/contract_core.h` is **byte-identical** across the three DroidNet forks
(RSeries / PSI / Flthy). It is owned by DroidNet-RSeriesLogics and never edited here:
`src/contract/CORE_PIN` names the RSeries commit the shared files were copied from (`cea4921`, core
revision CA1.5), the list of shared files is that commit's `test/host/core-files.txt`, and `run.sh`
stage 0 compares every one of them with the pinned commit when an RSeries checkout is at hand (CI
always has one). A change to the core is made there and re-copied here with a new pin. It is pure
C++ with no Arduino dependency, which is why it can be unit-tested on a host.

The shared files: `src/contract/contract_core.h`, `test/host/test_contract_core.cpp`,
`test/host/test_contract_v13.cpp`, `test/host/fuzz_parsers.cpp`, `src/wdp/wdp_announce_core.h` and
`test/host/test_wdp_announce_core.cpp`.

On the AVR, `ContractFlthy.h` defines the core's two flash-string hooks (`CONTRACT_PSTR`,
`CONTRACT_READ_BYTE`) before including it, so the parser's name tables and the query reply text stay
in flash. `run.sh` stage 1g runs the core's v1.3 tests with stand-ins that fault on a direct read.

## Board-specific notes

- The strand is the jewel: 7 NeoPixels per HP, `NEO_JEWEL_LEDS`. The spatial effects treat the
  6-LED ring as the 1-D strand and handle the center pixel per effect.
- Arbitrary RGB is written straight into the jewel as a packed `0xRRGGBB` via
  `setPixelColor()`, bypassing the native `basicColors` palette. The contract's envelope
  brightness is pre-scaled per write. Note that `setBrightness()` does **not** wait for `show()`:
  in Adafruit NeoPixel 1.15.x it rescales the RAM pixel buffer immediately (and lossily), and
  `show()` only transmits.
- The per-unit look renders through `LED_command[hp].LEDFunction = 100 + effectId`, i.e. the
  `case 101..115` block added to the native render switch.
- `INPUTBUFFERLEN` was grown 10 → 128 (the longest v1.3 line is 102 characters), and
  `serialEvent()` drops trailing bytes past the buffer instead of overrunning it. It also stops at a
  line's terminator, so a second line cannot be appended to one `loop()` has not handled yet: one
  line is handled per pass.
- `-DSERIAL_RX_BUFFER_SIZE=256` in `platformio.ini` makes the UART receive ring 256 bytes, which is
  what lets a 400-byte burst with no gaps arrive whole (spec section 11) while one line is handled a
  pass. An Arduino IDE build keeps the core's 64 bytes. How long a pass really takes is not measured
  yet: `pio run -e FlthyHPs_probe` is the build that reports it.
- The I2C receive handler copies into its own buffer (`dnI2cBuffer`), never into the serial line's
  `String`; `loop()` takes an I2C line first, with interrupts off for the copy.

## Contract v1.3, release 1

What `ContractFlthy.h` does on top of the shared core. The spec's rule numbers are in brackets.

- **Arm and begin (L1).** `M:v=show` arms: the unit is held (no servo twitch) and the stock LED look
  stays. The show begins at its first `C`, live `A` or `P`: the baseline goes dark and the LED twitch
  waits too. A Resend over a running show goes back to the stock idle look until then.
- **Stop and idle (L1).** `X` blacks out and keeps the mode; outside a show the jewel is the stock
  firmware's again, dark for one twitch interval. `M:v=idle` hands the jewel back (off colour, or
  dark with the override set) with both twitch timers a full interval ahead. An idle on a unit whose
  LEDs the layer does not have leaves the stock look alone.
- **Watchdog (L2).** `M:v=show,wd=<s>`; any well-formed line for the board feeds it; expiry is the
  idle path. `X` leaves it armed. Board-wide as well: the board keeps the `wd=` of the last watched
  show, and when it expires every unit holding anything of the contract's is idled, in a show or not
  (a unit that left the show by L6 or a failed `V` and was lit again by the same host). A unit in a
  show armed with no `wd=` is left alone. The memory is cleared by the expiry, by `M:v=idle` once no
  unit is in a show, and by an `M:v=show` with no `wd=` for the whole board.
- **`A199` in a show.** "Enable servo twitch" sent while the contract holds a unit lifts the hold on
  that unit's servo twitch until the next `M:v=show` or `M:v=idle` (`contractOwnerServoTwitch()`).
  `A198` puts it back. Without this a show armed with no `wd=` whose host died held the servos for
  good.
- **Pulse with no show (L3).** The overlay saves the stock function and its frame, and puts both
  back when it ends, the frame rescaled if the owner changed the strip level meanwhile.
- **`d=` on `A` (L4)** is ignored. **First live `A` with no `i=` (L5)** is a solid fill.
- **Native take-over (L6).** A stock LED command (type 0, any designator) or an `S<n>` sequence ends
  the show on each unit it addresses, as idle would, then runs (`contractNativeTakeover()`); a live
  look that S4 is still holding back is dropped with it. Two
  differences from L6's wording, by decision: a stock servo command (type 1) is not a take-over (the
  layer never has the servos; the command runs and the lights stay), and `H<unit>B<level>` moves the
  show's ceiling instead of ending the show.
- **Limits (S1, S2, S4, S5, S8).** From the core: 30 to 300 BPM, the pump's rate floor, live looks
  170 ms apart (newest wins), saturating parameters, pulse never faster than a 340 ms cycle.
- **Brightness (S6).** The ceiling is the strip's own level when the show arms. `b=` and the
  envelope are fractions applied per pixel write; verb `B` rides the strip below the ceiling in show
  mode only, and every reset puts the strip back.
- **Native looks (S7).** `nativeAllowedFlthy()`: 1 (Leia) and 6 (rainbow). No accent on a native
  section.
- **One limit for every path (S9), and S4 for every path.** The rules are the core's field gate and
  look gate; this board asks them where every frame is latched. The jewels are `DnStrip`
  (`src/droidnet/dn_strip.h`), whose `show()` (in `ContractFlthy.h`) measures the frame's level (the
  mean of each pixel's brightest channel) and its luminance (the core's integer Rec.709, read from
  the strip's G,R,B bytes), with `s.ceil` kept at the owner's strip level. A frame the layer drew, a
  frame of a gated native look, and any frame in the 340 ms after a hand-back is asked
  (`contractLookAllowed()` for a lit frame of another look, then `contractFieldAsk()`); on a no it is
  not latched, what is on the jewel stays, and the tick offers it again every pass, as long as the
  LED function that drew it is still the one selected. On a yes the gates are told
  (`contractLookGate()`, `contractFieldTell()`). Any other frame is stock light: never held, and told.
  The board's own care around the core's gate: the level is whole numbers but seven pixels measure
  in sevenths, so a lit peak creeping up inside one level still moves the gate's clocks; the gate's
  references are rescaled when the owner changes the strip level; a frame held across a lowered
  level is blacked out instead of left at the old scale.
- **The hand-back window.** Stock light is asked for 340 ms after the layer lets go of a jewel
  (`_touch()`), and after a verb-`B` step (`_rideApply()`): that step is the layer's light change even
  when the stock code draws the frame. While a stock frame is being held the window stays open
  (`owed`), and when that frame is shown the 340 ms run once more from it (`extended`, once for each
  hand-back): the stock look's next change keeps its distance from the light the hand-back put up,
  not from the moment the layer let go. After that the stock look runs as written, strobes included.
- **Colour on the jewel (S4).** A frame is a look when its brightest LED is at 4 levels or more: the
  core's rule since CA1.6 (`LOOK_MIN_LEVEL`, `contractLookAllowedAt()` / `contractLookGateAt()` with
  the frame's brightest LED), so a contract look that comes up dim takes the look gate at the first
  frame that shows its colour. What stays this board's: a look that draws its own colours under one
  name, which is all stock light (function 2 mixes a new colour every 100 ms) and Leia. The latch
  takes such a frame's mean colour (its three channels summed over the LEDs, over the strongest),
  and a frame more than 48 of 255 from the colour of the LAST CHANGE in any channel is a change of
  colour (`FLTHY_RECOLOUR`: half of what the test's observers call a change, and measured from the
  last change so that no two frames between changes are a change apart): where the frame is asked
  it waits its 170 ms, and where it is shown the look gate's time moves.
- **A runs-as-stock look's luminance.** The rainbow stands at 75 % by its brightest channel and 33 %
  by luminance. The latch passes both figures, measured on the look's first lit frame, to
  `contractNativeStarted(s, code, level, lum, now)`, and `contractNativeLevel(s, level, lum, now)`
  at a verb-`B` step (`nativeFullY`).
- **Verb `L`.** `contractLevelOffer()` in the handler and `contractLevelDue()` there and once a pass;
  the meter look reads `s.level`. A fall is at once, a raise at most once in 340 ms.
- **One rise is one onset (CA1.6).** The core's `fieldRise`. The latch's own care for the lit peak in
  sevenths moves the gate's times only and does not touch the flag. Pulse and wipe in red, green,
  blue, `116530` and white are never held (`gate_held_frames_table`).
- **A part of the jewel (S12).** The property test watches twenty-two regions of each jewel: the
  whole; the centre with each half of the ring and each half alone (six rotations each); the three
  opposite pairs; the six pairs of neighbours. A region is lit when its level reaches an observer's
  "lit" and two of its pixels (a quarter of the field) are not dark; the limit is three onsets of
  one region in a second. Nothing gates regions. What failed by its own design was redrawn or
  slowed:

  | Look alone, `s=255` | One region in a second, before | Now | What changed |
  | ---- | ---- | ---- | ---- |
  | sparkle | 10 (an opposite pair) | 1 | drawn on a bed: no pixel under two fifths of the look's level |
  | chase | 12 (the opposite pair that is lit) | 1 | the ring between the two bright pixels at the bed |
  | twinkle | 4 | 1 | each pixel from the bed to full |
  | comet | 2 | 0 | the ring behind the comet at the bed; the core: a lap is 684 ms |
  | `native:1` Leia | 4 (a half of the ring) | 3 | frames that brighten a pixel are 340 ms apart |
  | flash | 3 (the whole) | 3 | |
  | pulse, wipe | 2 (the whole) | 2 | |
  | rainbow, `native:6` | 1 | 1 | |
  | solid, scan, meter, gradient, colorcycle | 0 | 0 | |

  (`s12_region_rates_each_look_alone_at_its_fastest`, white and `116530`, 30 s a look; a single
  onset is the look's start.) The bed (`FLTHY_BED`, 102 of 255) works for any observer whose "dark"
  is under two fifths of its "lit": the look's own motion moves a part of the jewel by a factor of
  two and a half at the most, so what lights a part is a change of the whole field, which the field
  gate times. Leia sits at the limit by itself; the look that follows it waits for Leia's last
  brightening frame, as above.

  **By turns with light on the whole jewel**
  (`s12_region_rates_each_look_by_turns_with_full_light`): every look by turns with a solid look
  (510 ms and 170 ms), under a white pulse every 50 ms, and sent again in another colour every
  170 ms, at `s=255` and `s=0`. The worst region: flash 3, pulse 3, scan 3, wipe 3, and 0 for the
  rest. The first of these scripts is the defect the logics fork records as a known failure (a comet
  and a full look by turns); on the jewel it read 4, and a comet under pulses 5 and 6 in the first
  soak. The comet on the bed closes it here: nothing is expected to fail.

  The random streams measure the regions and do not assert them. In the last soak (three
  fresh seed bases, 4,000 streams each, hostile scripts and stock traffic mixed in) no stream broke
  S9 or S4, and 63 of the 12,000 lit one part of a jewel 4 times in a second, never more. Every one
  has the same shape: a part is lit once by a look's own drawing and not by a change of the whole
  field (after the jewel was dark, a look on the bed starts under an observer's "lit" line and its
  comet head or a spark arrives up to 300 ms later; a rainbow's drift; an accent drawn as a wipe),
  and the whole jewel is then lit three times at the gate's pace of 340 ms. Closing it takes a gate
  that knows parts of the field; it is not fixed. (`S9_REPLAY=1` prints a `REGION` line with the LED
  function and effect at each onset, and `S9_ONLY=<n>,<n>` runs chosen streams of a soak again.)
- **Native looks (S10).** `nativeGatedFlthy()` says which kind. `native:6` (rainbow) RUNS AS STOCK:
  `contractNativeMayStart()` before the LEDs are handed over, `contractNativeStarted()` at the first
  lit frame with the level it runs at, `contractNativeLevel()` when a verb-`B` ride moves it,
  `contractNativeEnded()` when a contract look replaces it; its frames are not asked. `native:1`
  (Leia) is GATED: `contractNativeMayStart()` too, then every frame is asked like a contract frame,
  and because Leia re-rolls every pixel by itself, a frame in which any LED gets brighter is latched
  340 ms after the last such frame at the least (S12; a frame that only falls is never held for
  that), and the field gate's time moves with each such frame, so the look that follows Leia keeps
  340 ms from it too (`s12_leia_and_the_look_that_follows_it`). While a native start waits,
  nothing lit is latched, and the look that stays up meanwhile is still the layer's when the layer
  put it there (`keptFn`): a boundary in the wait takes it down. As stock, Leia makes about four
  whole-field swings of a tenth or more a second (an open question for the owner);
  `s10_native_looks_are_classified_by_their_stock_field_swing` measures both looks and fails if the
  core's list and the measurement drift apart.
- **Verb `B`: the step is held.** A fall applies at once; a raise, and the owner's level coming back
  at a boundary, waits until `contractFieldAsk()` allows a white field at the new strip level
  (`_rideTry()`). A ride no longer makes stock frames the layer's.
- **S11.** `contractLookSame()` before a live `A` or a section is written; the start time and
  `contractLookRestart()` only when the look really changes. Sections wait for
  `contractSectionMayApply()`.
- **What the gates cost a look** (`gate_held_frames_table`, simulation, 30 s a look, passes of 1 to
  4 ms and of 10 to 15 ms, white, three speeds; the share of passes with a frame waiting):

  | Look | Held |
  | ---- | ---- |
  | solid, flash, pulse, rainbow, scan, sparkle, meter, comet, chase, wipe, gradient, colorcycle, twinkle | never |
  | pulse and wipe in red, green, blue, `116530` and white, `s=0` and `s=255` | never |
  | the beat pump on solid (`am=1`, `m=` 64 and 255, 60 to 240 BPM) | never |
  | `native:6` rainbow (runs as stock) | never |
  | `native:1` Leia (gated, and paced since S12) | 72.7 % |

  Sparkle was held on 16 to 37 % of passes as first drawn (random pixels at random levels swing a
  seven-pixel field by a tenth most frames). It is redrawn for the jewel: a fixed number of pixels
  at the look's level (2 at `s=0` to 5 at `s=255`) on a bed of two fifths of it, and one of them
  moves every 150 to 40 ms, so the field's level and luminance do not change at all while it runs
  (`sparkle_keeps_a_constant_number_of_sparks_on_a_bed`). The pixels come from the board's `random()`,
  like every stock look's.
- **Clock (C1, C2, C6).** One clock for the three units, seeded once per line; a mid-beat re-anchor
  does not fire that beat's accent again; a nudge never moves a section back; `bpm=0` holds. While
  held, a pumped look is shown still at its own `b=` level (the pump needs the clock), not frozen in
  whatever dip it was in; that step up is under the S9 gate like any other.
- **Score (section 6), load check (section 7), query (section 10).** 32 entries a unit with `b=` and
  every-N accents; overflow is a bad load; `V:n=,k=` idles a unit whose load is wrong and `ok=2`
  stays until the next `M:v=show`, and until then the unit drops every scored line; `Q:r=<route>` to
  one unit gets the routed reply. Each unit has one pending route (a newer `Q` to it replaces the
  route) and the tick sends one reply a pass, formatted at that moment, whole, in one write, when no
  WDP announce is partly sent and the TX ring has room for the longest reply: the line handler never
  writes to `Serial`, so a `Q` cannot stall `loop()`.
- **Shared timing (section 12).** `flash`, `pulse` and `scan` use `fxFlashOn`, `fxPulseBright` and
  `fxScanPos`. A pulse the beat pump drives is a steady fill at the pumped level.

Not in release 1: the `projector`, `kick` and `twitch` looks, and accents over native looks (R2).

## Bench checklist for release 1 (not run)

Nothing below has been done. It is the list to work through, on the Mega ADK on the bench or in
place in the dome, before the README's "never been flashed" sentence changes. Record every
observation here.

Before flashing: take a flash dump as the rollback (`avrdude -p atmega2560 -c wiring -b 115200
-U flash:r:flthy-predump-YYYYMMDD.hex:i`); unplug the WCB serial lead from D0 and D1 (UART0 is shared
with USB); flash the probe build first (`pio run -e FlthyHPs_probe -t upload`) with the lead still
unplugged and a USB serial monitor at 9600.

1. **Loop-pass time (probe build, USB serial).** Record `n` and `max_us` for ten seconds in each
   state: stock idle on the off-colour hold; show armed; show begun and dark; three solids; three
   solids with `m=200,am=2` at 120 BPM; three comets; three twinkles; a stock servo twitch in
   progress; one HP in RC mode (`F102`). **Section 11 is not shown to hold until this is known.** One
   line is handled per pass. In simulation a 400-byte gapless burst arrives whole with random passes
   of up to 10 ms; with passes of 10 to 20 ms 80 five-byte lines lose bytes in 19 runs of 20; in RC
   mode a pass is about 23 ms and lines are lost. If `max_us` in a show is above 10000, stop and
   report it: the next step is handling several lines a pass.
2. **Native regression (release build from here on, WCB lead back on).** `F0055`, `A006`, `R003C5`,
   `F002C`, `A007|25`, `S1`, `S4`, `S9`; `F1013`, `A104`, `A105|5`, `A198`, `A199`; one I2C command at
   `0x19`; `HAB128` then `HAB100`. Then ten minutes untouched: LED twitches and servo twitches on all
   three HPs, off colours between them.
3. **One live cue.** `!HFA:i=solid,c=0080ff` holds azure. `!HFA:i=solid,c=0000ff` is as bright as the
   stock `F0055`. `!HFP:c=ffffff,d=200` ten times: every one fires and the blue returns. With no show
   armed and the stock off colour showing, `!HRP:c=ff0000,d=400` flashes and the off colour returns.
4. **Power (S6).** With the strip at `BRIGHT 100`, `!H*A:i=solid,c=ffffff`: measure the HP board's
   supply current against all dark. Repeat after `HAB255`.
5. **Armed, then Play.** `!**M:v=show`: the stock LED look and its twitches carry on, and no HP moves
   from now. `!H*A:i=solid,c=00ff00,m=200,am=2`, then `!**C:bpm=120,ph=0,bpb=4`: the pump is on the
   beat. `!**C:bpm=20`: nothing changes. At `bpm=200` the pump is every second beat. `!**C:bpm=0`:
   the look stays lit and still; a `C` with a tempo resumes. One minute of Studio's 4 s corrections:
   no extra accents.
6. **Pushed score with V.** A show of at least 12 sections on one HP, Deliver, Play. Between Send and
   Play the HPs keep their stock look. Sections change on the beat; `!HFQ:r=<n>` reads
   `cap=32,n=<sent>,ok=1` on the Pi if the Mega's TX reaches the WCB. Deliver again with one line
   dropped or a wrong `k=`: that HP goes back to its off colour and reads `ok=2`; the other two play.
7. **Watchdog.** Arm with `wd=15` and play; close the Studio tab. About 15 s later all three HPs
   return to their off colours. **Eyes on the servos:** none moves at that moment.
8. **Idle restore, eyes on the servos.** After a show of at least four minutes press Stop. No HP
   moves when the lights go to their off colours. Within three minutes each HP twitches by itself and
   LED twitches resume. Repeat with `A198` sent during the show: no HP twitches until `A199`.
9. **Gapless delivery, I2C and take-over.** Resend the whole show ten times in a row during
   playback: every resend ends with all three HPs on the right section and `ok=1`. Play a show stored
   in a WCB macro (no pacing): the same. Send a stock LED command to one HP mid-show, once over
   serial and once over I2C: it shows, that HP stays out of the show (it is not retaken at the next
   section) and its servo twitch comes back, and the other two carry on. Send a stock servo command
   to an HP mid-show: it moves and its lights stay in the show.

10. **S9, by eye and by camera.** Solid white on one HP, then `!HFB:b=0` and `!HFB:b=255` alternating
    as fast as the WCB will send them; the same with `i=meter` and `!HFL:v=0` / `!HFL:v=255`; then
    `!**M:v=show`, `!**C:bpm=120` and `!**M:v=idle` in a loop on the rear HP (white off colour). In
    each the HP relights at most three times a second and goes dark at once. Film at 240 fps if a
    phone allows and count.
11. **A199 in a show.** Arm with no `wd=`, start a look, unplug the host. After 15 minutes no HP has
    moved. Send `A199`: within three minutes each HP twitches, the look still showing. `A198`: they
    stop.
12. **Q under load.** During a show send `!HFQ:r=<n>` ten times a second for ten seconds: the show
    does not stutter and every reply the Pi receives is a whole line.

13. **Colour and narrow swings (S4, S9's tenth).** One HP: `!HFA:i=solid,c=ff0000` and
    `!HFA:i=solid,c=0000ff` alternating as fast as the WCB sends them: the colour changes at most six
    times a second. `!HFB:b=0` / `!HFB:b=62` over solid white, and `!HFL:v=0` / `!HFL:v=62` on
    `i=meter`: no flicker faster than three a second. A score of one-beat red and blue sections at
    300 BPM: no colour change closer than 170 ms (film it).
14. **Native looks (S10).** `!HFA:i=native:6` beside the stock `R006` on another HP: the two rainbows
    look the same. `!HFA:i=native:1` beside the stock `R001`: the contract Leia flickers more calmly;
    note how it reads to the eye. Start each from a lit look and from dark: the start may wait up to
    a third of a second and never flashes.
15. **Verb `B` between Send and Play.** Arm a show with the off colours showing, then `!H*B:b=0` and
    `!H*B:b=255` alternating fast: the HPs dim at once and come back at most three times a second.
    During a stock look the operator started (`A0022|9`), a steady `!H*B:b=128` dims it and the stock
    flash keeps its own rhythm.
16. **The same look again (S11).** A pulse look, then the same line again every 300 ms: the breathing
    does not restart. A Resend of a score during playback: sections that are already showing do not
    blink.
17. **Sparkle, twinkle and chase on the bed.** `!HFA:i=sparkle,c=ffffff` at `s=0`, `s=128` and `s=255`:
    two, three or five bright pixels on a glow, one moving at a time. `i=twinkle`: every pixel
    between the glow and full. `i=chase`: two bright pixels turning round a glowing ring.
    `i=comet`: head and tail on a glowing ring. Judge each look (drawn for seven pixels in
    simulation only), and whether two fifths is the right bed. A sparkle or a scan sent again in
    another colour changes colour without going dark.
18. **Pulse, wipe, flash and chase after CA1.5.** `s=255` pulse and wipe: a 680 ms cycle; flash: 190 ms
    a state; chase: two of the six ring pixels lit at every step.
19. **After a hand-back.** `F0055|2` (solid blue, 2 s), and a pulse about half a second before it
    ends (`!HFP:c=3b82f6,d=69,b=202`): the off colour comes up a third of a second after the blue
    returns, not at once. The same with `!HFB:b=0` and `!HFM:v=show` alternating over the blue.
20. **CA1.6.** `!HFA:i=pulse,c=22c55e` and `i=wipe` in red and green: the fade comes up in one piece,
    with no step that stops and then jumps. `!HFA:i=meter,c=ffffff` with `!HFL:v=255` and `!HFL:v=64`
    by turns as fast as the host can: the jewel brightens at most three times a second and dims at
    once. `!HFA:i=comet,s=255`: a lap of the ring takes two thirds of a second. `!HFA:i=native:1`:
    about three re-rolls a second; say whether Leia still reads as Leia.
21. **A comet by turns with a full look.** `!HFA:i=comet,c=ffffff` for half a second and
    `!HFA:i=solid,c=ffffff` for a sixth, again and again, and a comet under `!HFP:c=ffffff` sent as
    fast as the host can: no part of the ring should blink faster than three times a second (the
    comet's ring glows between passes).

**Conformance record.** For each of S1, S2, S4, S6, S7, S8, S9, S10, S11, C1, C6, L1 to L6, capacity,
`V`, `Q` and section 11: the line sent, what was seen, pass or fail, and the `fw` from the WDP announce.

## Native brightness

- **Wire form:** `H<unit>B<level>`: unit `F R T X Y Z A` (uppercase, the stock designator
  fan-out), then `B`, then 1-3 digits whose value is 1-255, then the end of the line. Anything
  else is ignored silently, including any line the stock `|<n>` split fired on.
- **Parser:** `src/droidnet/native_ext.h`, `nativeExtParse(line, piped, mask, level)`. Pure C++,
  no Arduino, AVR-safe (at most 3 digits in a `uint16_t`). It returns a jewel mask (bit 0 front,
  bit 1 rear, bit 2 top) and leaves its outputs untouched on a reject.
- **Hooks in `src/main.cpp`** (CRLF, like the rest of the file): the include; one marked
  `const bool dnPiped = (pipeIndex >= 1);` line right after `pipeIndex = inputString.indexOf('|');`,
  because the stock split then resets `pipeIndex` and `halt` cannot tell (`toInt()` is `atol()`,
  and `halt` is a 16-bit `int`); and the `else if( inputBuffer[0]=='H' )` branch between the `!`
  branch and the designators, inside `// DroidNet native brightness BEGIN` / `END` markers.
- **Handler:** per addressed jewel, `setBrightness(level)`, `contractOwnerBrightness(hp, level)` (the
  level is the contract's ceiling from now on, a verb-`B` ride ends, and the memoised solid/meter/off
  looks repaint) and `show()` (held frames update now). RAM only; power-on is still `BRIGHT 100`.
- **Tests:** `test/host/test_native_ext.cpp` (the accept/reject table), `test/host/fuzz_native_ext.cpp`
  (fork-local differential fuzz; `test/host/fuzz_parsers.cpp` stays byte-identical across the three
  forks), and the `run.sh` stage 3 guard, which cuts the marked region out of `src/main.cpp` and
  fails closed if the markers or the `H` branch are missing, or if the region or the parser names
  the servo command path. `pio check` analyses `native_ext.h` too.

## WDP device announce

- **Line:** `@WDP1 {"type":"Flthy HP Controller","fw":"<commit>","caps":["droidnet.contract","contract.1.3","hp.servo","hp.led"]}`
  plus `\n`, 116 bytes with a 7-character commit (122 with `-dirty`; the core's buffer is 128).
  `contract.1.3` is the contract version this build implements (spec section 12). The
  format is Greg Hulette's (`docs/WDP_DEVICE_ANNOUNCE.md` in the WCB repo, WCB 7.0.1); the WCB
  consumes the line and never forwards it.
- **Adapter:** `src/wdp/wdp_fork.h`: the identity, `wdpForkSetup()` (seed + first due time, called in
  `setup()` after the boot banner), `wdpForkTick()` (every loop pass, next to `contractBeatTick()`;
  writes at most `Serial.availableForWrite()` bytes, so it never blocks) and `wdpFinish()` (writes the
  rest of a line in flight; blocking is fine there).
- **Other writers:** the verb-Q reply (`ContractFlthy.h`) waits while `wdpInFlight()` and is then
  written whole; RCHP's `No RC Input Detected!` (`src/main.cpp`) calls `wdpFinish()` first. The
  `DEBUG` prints do neither; `DEBUG` is commented out. `run.sh` stage 3 fails on a non-`DEBUG` write
  guarded by neither, `compile_contract.cpp` checks that the reply is held back during a part-sent
  announce and then goes out in one write, and the simulation checks both lines stay whole on TX.
- **Seed:** 16 `analogRead(3)` samples (A3: unused by the sketch, already its `randomSeed()` source)
  folded FNV-1a style over `micros()`; the core's own xorshift generator then jitters 0.5-3 s for the
  first line and 25-30 s after.
- **fw:** `scripts/wdp_fw.py` (`extra_scripts = pre:`) defines `WDP_FW` with `env.StringifyMacro()`:
  `git rev-parse --short=7 HEAD`, `-dirty` if `git status --porcelain` lists anything, `unknown` if
  git fails; `wdp_fork.h` falls back to `"unknown"` when it is not defined (an Arduino IDE build).
  `firmware/build-image.sh` fails unless the image's fw string is exactly its `source_commit`.
- **Tests:** `run.sh` stages 1b (the shared core test, `-std=c++11 -Werror`) and 1c
  (`test/host/test_wdp_fork.cpp`, compiled with `WDP_FW` "abcdef0-dirty" and without it).
