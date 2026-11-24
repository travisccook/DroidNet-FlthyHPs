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
- **Whole-firmware simulation: 122 scenarios pass and 2 fail as expected (known limits, S12); 1 of the review's four fails on its count of builds** (see the v1.4 section). (117 after Task F4, 105 at core CA2 with the stand-ins, 102 in release 1.) Stage 2b of `run.sh`
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
- **CA1.7 round.** Core `fe1c8ad`: a chase steps every 130 ms at the least and a scan laps the ring in
  680 ms; a change of colour alone recolours a look in place; `b=` on a look goes up at most once
  in 340 ms, in verb `L`'s window, and this board's verb-`B` ride shares that window. The core's
  `test_fx_regions.cpp` is stage 1j. The regions are also counted the gate's way; sparkle is slowed
  for it. The logics fork's known-failure scripts run on the jewel: two of the five read 4 by the
  gate's count and are recorded. The first soak on this core broke on 3 of 12,000 streams: one
  fault of the layer (the lit peak kept in sevenths was not cleared when a held frame was blacked
  out; `s9_the_lit_peak_starts_afresh_at_every_onset`) and two of the oracle (its colour test now
  allows for the LEDs' rounding). See "Known limits" below.
- **The R2 line (`lighting-r2`): core CA2, contract v1.4, release 2, with stand-ins.** Core `9991f5a`
  (RSeries; "Core revision CA2" in its `docs/CHANGING_THE_CORE.md`): the ids `logic`, `projector` and `vu`
  and the math of the first two; the query reply says `ver=1.4,rel=2`, the WDP caps `contract.1.4`. The
  three ids reach the jewel through their own render slots, 116 to 118 (`FLTHY_FX_MAX` is
  `_fxCode(CE_VU)`; `main.cpp`'s LED function switch takes the three codes), and draw stand-ins until
  Task F4: `logic` as twinkle, `projector` as sparkle (with sparkle's counters, started afresh like it),
  `vu` as meter. None of them is an accent. `test/host/sim/scenarios_v14.inc` checks them (logic and
  twinkle, vu and meter, draw the same frames side by side; projector keeps two sparks at `s=0` and moves
  one every 477 ms; a `P` naming any of them overlays solid), the white-box accent guard of stage 2 takes
  them, and a stock line naming slot 118 shows only the off colour. The two known limits of S12 stay as
  they were. Build, on clean trees (`pio run -e FlthyHPs`, `stack_report.py`):

  | | Release 1 (`86b5ec0`, CA1.7) | Core CA2 alone | Core CA2 and the stand-ins |
  | --- | --- | --- | --- |
  | flash | 44,894 B | 44,912 B | 44,948 B |
  | SRAM | 4,670 B | 4,670 B | 4,670 B |
  | stack headroom (sound bound) | 3,036 B | 3,073 B | 3,074 B |

  The headroom moved because of how the tool bounds an indirect call, not because a path got shorter:
  the synchronous worst case is 412 B, then 412 B, then 411 B, and the tool charges each interrupt with
  the worst of the address-taken functions it finds, which was `String::substring()` (74 B) and is now
  a copy of `beatAccentFires()` (37 B). Host-tested and cross-compiled; not run on a board.
- **Task F4 on the R2 line: `projector` and `vu` are this board's own looks.** `logic` is still drawn as
  twinkle (it is the logics' look). See "Contract v1.4, release 2" below. Build, on clean trees:

  | | CA2 and the stand-ins (`9f4137a`) | Task F4 (`89a3cf9`) | |
  | --- | --- | --- | --- |
  | flash | 44,948 B | 46,544 B | +1,596 B |
  | SRAM | 4,670 B | 4,694 B | +24 B (8 B a jewel) |
  | stack headroom (sound bound) | 3,074 B | 3,042 B | -32 B |

  The synchronous worst case is 411 B both times. 24 B of the headroom is the new RAM; 8 B is the
  tool's charge for an interrupt (its worst address-taken function is now `TwoWire::endTransmission()`,
  23 B, where it was a copy of `beatAccentFires()`). The flash is more than the plan's estimate (0.5
  to 0.9 KB): +762 B in `_renderLook()` for the projector's case, and the pump's amount now one function
  shared by `_envBright()` and `vu` (+522 B out of line, -422 B from `loop()`), the vu offer in the tick
  (+298 B). Host-tested, simulated and cross-compiled; not run on a board.
- **The review of Task F4 (four findings, fixed in `8a4f2eb`).** The latch paces a projector's frames as
  Leia's; the 340 ms hold after an overlay runs on every path; vu's hand-over between pumped and not
  starts the level window and unpumped vu is the meter; the colour observer dates a dim colour as the
  core does. Build, on a clean checkout: flash 46,910 B (+366 on `89a3cf9`), SRAM 4,720 B (+26: each LED's
  luminance in the latch, 7 B a jewel, and vu's two bytes), stack headroom 3,003 B (-39: the synchronous
  worst case is 424 B, 13 more, the latch's per-LED luminance on the stack).
- **Build:** flash 44,900 B of 253,952; SRAM 4,676 B of 8,192 (the score tables are 2,112 B).
  `stack_report.py`: worst-case stack 476 B of the 3,516 B left, 3,040 B spare. The heap (Arduino
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
`src/contract/CORE_PIN` names the RSeries commit the shared files were copied from (`9991f5a`, core
revision CA2, on the R2 line), the list of shared files is that commit's `test/host/core-files.txt`, and `run.sh`
stage 0 compares every one of them with the pinned commit when an RSeries checkout is at hand (CI
always has one). A change to the core is made there and re-copied here with a new pin. It is pure
C++ with no Arduino dependency, which is why it can be unit-tested on a host.

The shared files: `src/contract/contract_core.h`, `test/host/test_contract_core.cpp`,
`test/host/test_contract_v13.cpp`, `test/host/fuzz_parsers.cpp`, `test/host/test_fx_regions.cpp`,
`src/wdp/wdp_announce_core.h` and `test/host/test_wdp_announce_core.cpp`.

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
- **Verb `L`.** `contractLevelOffer()` in the handler, `contractLevelDue()` there, and
  `contractHoldsDue()` once a pass (it moves `L` and `b=` in one window);
  the meter look reads `s.level`. A fall is at once, a raise at most once in 340 ms.
- **One rise is one onset (CA1.6).** The core's `fieldRise`. The latch's own care for the lit peak in
  sevenths moves the gate's times only and does not touch the flag. Pulse and wipe in red, green,
  blue, `116530` and white are never held (`gate_held_frames_table`).
- **A part of the jewel (S12).** The property test watches twenty-two regions of each jewel: the
  whole; the centre with each half of the ring and each half alone (six rotations each); the three
  opposite pairs; the six pairs of neighbours. Two observers count a region's onsets, and the limit
  for each is three in a second. **The bands:** a region is lit when its level reaches an observer's
  "lit" and two of its pixels (a quarter of the field) are not dark, and dark again at its "dark"
  (10 and 50 %, 19 and 75 %, 4 and 31 % of the ceiling). **The gate's count** (core revision CA1.7,
  `test_fx_regions.cpp`): a rise of more than a tenth of the ceiling above the region's last
  trough. Nothing gates regions. Every look alone, `s=255`, white and `116530`, 30 s
  (`s12_region_rates_each_look_alone_at_its_fastest`, asserted for both observers):

  | Look | Bands, as first drawn | Bands now | The gate's count now | What changed |
  | ---- | ---- | ---- | ---- | ---- |
  | sparkle | 10 | 1 | 2 | on a bed, and one spark moves every 477 to 350 ms |
  | chase | 12 | 1 | 3 | on a bed; the core: a step is 130 ms at the least |
  | twinkle | 4 | 1 | 3 | each pixel from the bed to full |
  | comet | 2 | 1 | 3 | on a bed; the core: a lap is 684 ms |
  | `native:1` Leia | 4 | 3 | 3 | frames that brighten a pixel are 340 ms apart |
  | scan | 0 | 0 | 3 | the core: a lap of the ring is 684 ms |
  | flash | 3 | 3 | 3 | |
  | pulse, wipe | 2 | 2 | 2 | |
  | rainbow | 1 | 1 | 2 | |
  | solid, meter, gradient, colorcycle, `native:6` | 0 or 1 | 0 or 1 | 0 | |

  **The bed** (`FLTHY_BED`, 102 of 255): sparkle, twinkle, chase and comet keep every pixel of the
  look at two fifths of its level or more (the centre of chase and comet stays off). The look's own
  motion then moves a part of the jewel by a factor of two and a half at the most, which no band
  observer whose "dark" is under two fifths of its "lit" sees as dark and lit. The core's floors of
  CA1.7 (chase 130 ms a step, scan and comet 680 ms a lap) keep each look alone to three by the
  gate's count, and they came after the bed. The bed stays because a ring of six and a centre is
  not a strand of six: the chase's two bright pixels are always an opposite pair, a quarter of the
  field that the core's test of neighbouring windows does not look at, and by turns with a full
  look a chase or a comet without the bed lights a pair again and again (below). **Sparkle** is
  this fork's own drawing, so the core's test does not cover it: by the gate's count it read 10 at
  a move every 40 ms, and one spark now moves every 350 ms at the least.

  **By turns with light on the whole jewel**
  (`s12_region_rates_each_look_by_turns_with_full_light`): every look by turns with a solid look
  (510 ms and 170 ms), under a white pulse every 50 ms, and sent again in another colour every
  170 ms, at `s=255` and `s=0`. By the bands (asserted): flash, pulse, scan and wipe 3 at the most,
  0 for the rest. By the gate's count (reported, not asserted): 4 to 6 for scan, comet, chase and
  twinkle, 4 for rainbow and sparkle.

  **The scripts the logics fork records as known failures**, 20 s each on the jewel
  (`s12_known_*`, one scenario for each observer):

  | Script | Logics | Jewel, bands | Jewel, the gate's count |
  | ---- | ---- | ---- | ---- |
  | a scan and a chase by turns every 170 ms | 6 | 3 | 3 |
  | a scan and a full look by turns every 170 ms | 6 | 3 | 3 |
  | `C` seeks across sections of solid, scan, comet and wipe | 5 | 3 | **4** |
  | a wipe with a wipe accent over it every 340 ms | 5 | 3 | 3 |
  | a comet and a full look by turns (510 ms and 170 ms) | 4 | 0 | **4** |

  The two figures of 4 are in `xfail.txt`. The comet case is closed on the jewel for the bands (the
  ring behind the comet never goes dark, so the full look does not light it "again") and not for
  the gate's count (from the bed to full is more than a tenth).

  The random streams measure the regions and do not assert them. In the last soak (three fresh
  seed bases, 4,000 streams each, hostile scripts and stock traffic mixed in) no stream broke S9 or
  S4. By the bands, 68 of the 12,000 lit one part of a jewel 4 times in a second, never more, all
  of one shape: a part is lit once by a look's own drawing and not by a change of the whole field
  (after the jewel was dark, a look on the bed starts under an observer's "lit" line and its comet
  head or a spark arrives up to 300 ms later; a rainbow's drift; an accent drawn as a wipe), and the
  whole jewel is then lit three times at the gate's pace of 340 ms. By the gate's count, 4,577 of
  the 12,000 (38 %) lit one part more than three times in a second, 8 times or more at the worst:
  that count sees every hand-over between two looks that light different parts, and the streams
  are made of those. Closing either takes a gate that knows parts of the field. (`S9_REPLAY=1`
  prints a `REGION` line with the LED function and effect at each onset, and `S9_ONLY=<n>,<n>` runs
  chosen streams of a soak again.)
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
- **Verb `B`: the step is held.** On this board verb `B` is a ride on the STRIP's level (it dims the
  stock light under an armed show too), not the look's own `b=`, so it is not the core's `bright`.
  A fall applies at once. A raise, and the owner's level coming back at a boundary, keeps the
  core's rule in the core's one window with `L` and `b=` (`levelMs`: at most once in 340 ms), and
  also waits until `contractFieldAsk()` allows a white field at the new strip level (`_rideTry()`),
  because the frame it relights may be stock light. The look's `b=` (a live `A`, a section) goes
  through `contractBrightOffer()` and `contractHoldsDue()`.
- **S11, and a change of colour alone.** `contractLookContinues()` before a live `A` or a section is
  written: the same effect, native code and speed is the look that is up, recoloured in place; the
  start time and `contractLookRestart()` only when the look really changes. Sections wait for
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
  moves every 477 to 350 ms, so the field's level and luminance do not change at all while it runs
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

## Contract v1.4, release 2 (the R2 line)

What this board draws for the three ids of spec v1.4 (`2026-11-21-driveable-animation-contract-v1.4-design.md`
in the Signal Booster), on core CA2. None of them is an accent (`accentEffectAllowed()`); an overlay may run
over each. Everything here is host-tested, simulated on the real `src/main.cpp` and cross-compiled. Nothing
of it has run on a board.

| id | Render slot | What the jewel shows |
| --- | --- | --- |
| `logic` | 116 | twinkle (the logics' look; this board's stand-in) |
| `projector` | 117 | the colour projector, paced for the gate (N6 to N9) |
| `vu` | 118 | the meter (a solid fill at its level), the pump moving the level (N10 to N12) |

**`projector`** (`ContractFlthy.h`, `case CE_PROJECTOR` in `_renderLook()`). A jewel keeps seven slot
numbers (`projSlot`), the next step's time (`frameMs`) and whether a frame of the look waits at the latch
(`projWait`): 8 B a jewel. A step comes every 50 to 149 ms (`random()`, the board's dice, as the stock
`colorProjectorLED()` draws them) and draws a candidate slot of `fxProjectorSlot()`'s row for each pixel.
A brightening step gives every pixel its candidate and takes the window (`levelMs = now`); any other step
crumbles through `fxProjectorTake()`. Its level is `b=`: on this board the strip applies the ceiling to
every write (S6), so `contractLevel(ceil, b=)` is the light `s.bright` gives as the per-write fraction; the
layer never multiplies by `s.ceil`. The pump does not reach it (`am=` and `m=` are kept and ignored), `s=`
is ignored (taken as 128, so another `s=` is the same look, S11), and a change of colour alone recolours
the slots that are up. Where the spec could be read more than one way, the safe reading was taken:

1. **"Since the unit's light last went up"** (N7) is the core's `levelMs` (verb L, verb B, `b=`, and every
   brightening step) and the field gate's last onset (`s.fieldOnsetMs`: an overlay or the look before this
   one lighting the jewel). Without the second, 2 of the property stage's 150 random streams lit a part
   of a jewel 4 times in a second by the bands (0 of 150 with it); the named scenarios pass either way.
2. **A frame that waits at the latch holds the steps.** A brightening frame the field gate or the look gate
   holds is the frame the jewel will show: no step is taken until it is up, and when it is up, if it raised
   light, it takes the window there. So the closest two frames in which a LED gets brighter are 340 ms
   apart on the jewel, not only in the dice.
3. **S4: the latch judges its colours as Leia's.** A row is the colour, white and three mixes, so the
   field's colour moves at a step. The latch's rule for a look that draws its own colours under one name
   (`FLTHY_RECOLOUR`, 48 of 255 from the colour of the last change) takes the projector's frames too
   (`DnStrip::show()`, the `projector` test, which reads the look that drew the frame and not what is up
   now). A step that would change the colour within 170 ms of the last change waits. In white nothing
   waits; in `116530` the steps are 106.6 ms apart on average (26 % of passes have a frame waiting).
   Without this, 31 of 150 random streams changed a jewel's colour under 165 ms apart; judged by what is
   up instead of by the frame's own look, 6 of 150 still did (a frame of it that had waited went out in
   the pass an overlay began, as if it were the overlay's).
4. **After an overlay over it ends, by any path, the jewel holds still for 340 ms** (`_projHold()`, from
   `_expireOverlay()` and from `_setLook()` when a line ends the overlay: review finding f2, where an `A`
   that ended each overlay skipped the hold and a part lit 4 times a second by the gate's count): a crumbling
   step there let the next overlay, which may come 340 ms after the last began, light a pair again within
   a third of a second of the base's return. Before this, a white overlay every 340 ms at `b=` 30 to 200
   lit a part of the jewel 4 times in a second by the gate's count (over white and `116530`), and over
   white at `b=80` (250 ms long) and `b=128` (170 ms) by the bands too; after it, 3 at most by every
   observer. Under an overlay every 340 ms the projector does not step.
5. **The start.** Every slot is dark and the first step is due at once, so the look's first frame is a
   brightening step when the window lets it start; until then it stays dark.
6. **The latch paces every frame drawn while the unit's base is a projector, as it paces Leia's**
   (`DnStrip::show()`, `gated`): a frame in which a LED gets brighter, by its brightest channel or by its
   luminance (more than 2 counts, what the two roundings of a write can make of a fall), is latched at
   least 340 ms after the last such frame, and when it is shown it moves the field gate's onset time.
   This covers the base's own frames and an overlay's over it. The look's own rule (1) spaces its
   brightening steps; the latch holds what that rule cannot see. Review finding f1: a brightening step that
   moved light onto two ring pixels without raising the field's level left `fieldOnsetMs` old, and a storm
   of dim overlays then lit that pair 4 times a second by the bands as well as the gate's count (white 42
   of 60 builds, red 20 of 59). Finding f3: a change of colour alone repainted the slots at once, and
   frames with a LED brighter came 170 ms apart. Known limit 1 of the first round of this section (a dim
   overlay storm that begins after a crumbling step, the gate's count 4) closes with it. Its cost is an
   accent's: see known limit 7 below.

The scenarios (`test/host/sim/scenarios_r2.inc`, stage 2b), one pass a millisecond where the timing is
measured, at the owner's level 255 where the colours are compared exactly:

| Scenario | What it showed |
| --- | --- |
| `rel2_projector_slot_histogram_is_flat_over_20000_steps` | 140,000 candidates from `random(0, 10)`: the farthest slot 1.21 % from the mean (limit 3 %) |
| `rel2_projector_step_intervals_are_50_to_149_ms_with_a_mean_of_99_5` | drawn 50 to 149 ms, mean 99.37 (white) and 99.40 (`116530`); between steps in white the same, in `116530` 50 to 170 ms, mean 106.58 (S4 waits) |
| `rel2_projector_no_led_gets_brighter_in_a_crumbling_step` | `116530`, `ff0000`, `3b82f6`, 6,000 steps each: of about 1,520 frames with a LED brighter, none outside a brightening step |
| `rel2_projector_brightening_steps_are_340_ms_apart_or_more` | 20,000 steps, 5,100 brightening: the closest two 340 ms apart (418 ms on average); frames with a LED brighter, the closest two 340 ms |
| `rel2_projector_ten_row_colours_for_six_c_values_match_the_vector_file` | `000000`, `ff0000`, `00ff00`, `116530`, `3b82f6`, `808080`: every pixel of 60 s of frames is its slot's colour of the vector file's row, and all six colours of each row appear |
| `rel2_projector_b_and_B_raised_every_50_ms_light_no_part_more_than_three_times_a_second` | white and `116530`, alone and under `b=`, `B` and both raised every 50 ms: at most 3 onsets in a second for the whole jewel, for any part by the bands and by the gate's count; closest whole-jewel onsets 340 ms |
| `rel2_projector_an_overlay_returns_to_the_same_slots` | 59 overlays each of four kinds: the same slots after every one, and the first base frame shows them; under an overlay every 340 ms at most 3 by every observer |
| `rel2_projector_a_dim_overlay_storm_right_after_a_crumbling_step` | built on purpose 300 times in each of `116530`, white and red: at most 3 by every observer (it was known limit 1: the gate's count 4 in 74 of 300) |
| `rel2_projector_an_accent_over_it_waits_at_most_340_ms` | `P` on every beat at 120 BPM for 20 s: white, 1 ms late at the most; `ff00ff` at `b=120`, 250 ms, 68.7 ms late on average, 201 at the most, 8 of 40 never shown (known limit 7) |
| `rel2_vu_the_hand_over_between_pumped_and_not_keeps_the_window` | as above |
| `review_r2_f1_...` to `review_r2_f4_...` (the review's, `scenarios_r2_review.inc`) | f2, f3, f4 pass. f1: every build at most 3 by every observer in all three colours; its count of builds (50 asked) is 32, 9 and 10, because with reading 6 a brightening step that lights a LED moves `fieldOnsetMs`, which is the condition it builds on (reported to the coordinator; the check is not edited) |
| `rel2_projector_am_and_m_do_not_move_the_level` | 60, 120 and 300 BPM with `am=2,m=255`: every lit pixel of 20 s at exactly `b=200` |
| `rel2_projector_and_vu_the_led_only_invariant_holds` | 90 s of the two looks on all three jewels with the auto twitches on: no servo moved; the pass checks (LED-only, the hold) on every pass |
| `rel2_vu_pumped_at_five_tempos` | `am=2,m=200` at 30, 120, 176, 177 and 300 BPM: the level 55 between beats, 10, 40, 59, 30 and 50 rises in 20 s, the closest 2000, 500, 340, 677 and 400 ms apart; at most 3 onsets a second by every observer; brightness 255 |
| `rel2_vu_pumped_under_a_seek_every_50_ms` | a `C` seek every 50 ms and `B` 0 / 200 every 50 ms, at 120 and 300 BPM: at most 3 by every observer |
| `rel2_vu_an_l_during_the_pump_changes_nothing` | two jewels on one pumped `vu`, `L` to one every 25 ms: the same frame at 10,000 of 10,000 passes; with the clock held each takes its own `L` |

The tables of the S12 scenarios have the two looks too: alone at their fastest, `projector` 3 by the bands
and 3 by the gate's count, `vu` 0; by turns with full light, `projector` 3/3 (with solid), 3/3 (under a
pulse every 50 ms), 0/0 (sent again in another colour); `vu` 0/2, 0/3, 0/0. The held-frames table: both
never held in white. The random streams of the property stage name the three ids, and its hostile pairs
gain six (projector under its level raised, recoloured, overlaid, ridden by `B`; vu pumped under `L` and
under a seek). `compile_contract.cpp` checks the level, `s=`, the offer and the `L` rule on the mock.

**`vu`** (`_vuPumped()`, the hand-over and the offer in `contractBeatTick()`, the meter's case). Pumped
means the base is `vu` and the clock runs (`C:bpm=0` holds it: not pumped) and `am=` and `m=` are not 0.
- **Pumped:** every pass offers `envBright(255, m, amount)` (the core's `vuLevel`; the amount is
  `beatAccentAmount()` with the section's progress for `am=3`) to the core's level hold before
  `contractHoldsDue()`: a fall at once, a rise at most once in 340 ms in the window `L`, `B` and `b=`
  share. The meter draws `s.level` at `b=` (the brightness is never pumped). An `L` that arrives is
  dropped and not remembered (N12).
- **Not pumped:** the meter, on every board's reading of N12 (the coordinator's decision of 2026-11-21):
  its level is the last `L` the unit took (0 if none since the show began: dark).
- **The hand-over** (pumped to not, or back: Pause and resume, `am=` or `m=` to 0 and back, `vu` put up or
  taken down while the clock runs; any pass, an overlay up or not) starts the level window
  (`levelMs = now`). To the pump the level starts from 0 (a fall, at once) and the pump's first rise waits
  340 ms; from the pump the meter's last `L` is offered (a fall at once, a rise 340 ms after the hand-over).
  Review finding f4: under `C:bpm=0` the level froze where the pump had it (208 or 79 by when Pause landed).
  `rel2_vu_the_hand_over_between_pumped_and_not_keeps_the_window`: Pause and resume every 60 and 100 ms,
  `am=` and `m=` toggled every 60 and 90 ms, the meter at full by `L`: at most 3 onsets a second by every
  observer, the closest 340 ms.

**The soak.** The property stage with the three ids in its random streams and six hostile pairs for them,
4,000 streams at each of three seed bases (5200000, 5600000, 6000000; `SIM_DEFS="-DS9_SEEDS=4000
-DS9_SEED0=<base>"`), on the layer after the review's fixes (`8a4f2eb`): none of the 12,000 broke a
whole-jewel limit (S9 or S4), went over the ceiling, moved a servo or was left off the stock firmware. A
part of a jewel, measured and not asserted: by the bands 52 of the 12,000 lit one part 4 times in a
second, never more (15, 19, 18 by base); by the gate's count 4,331 (36 %), 8 times or more at the worst.
The first round, on `89a3cf9` with the oracle as it was: 1 of 12,000 broke (5200000 + 1561, two changes
of colour 129 ms apart; the layer had kept 170 ms by the core's rule and the observer dated a dim colour
late, now fixed in the oracle), 89 by the bands, 4,829 (40 %) by the gate's count. (R1's last soak: 68 of
12,000 by the bands, 38 % by the gate's count.)

The servo paths are as they were (the `!` path is LED-only: stage 3's static guard and the pass checks of
every scenario), Leia's gated path is unchanged, and the core is not edited here.

## Known limits (release 1)

Everything here is from simulation. Nothing in this fork has run on a board.

1. **A part of the jewel under two looks by turns (S12).** The gates have one state a unit and
   measure the whole field. By the gate's own count of a part (a rise of more than a tenth above
   its last trough), `C` seeks across sections of solid, scan, comet and wipe, and a comet by turns
   with a full look (510 ms and 170 ms), light one part of a jewel 4 times in a second; the limit
   is 3 (`xfail.txt`). Scan, comet, chase and twinkle under a pulse every 50 ms, or by turns with a
   solid look, read 4 to 6 by that count. By the wide observers all of these read 3 or fewer.
2. **Hostile random streams and a part of the jewel.** In 12,000 random streams (hostile scripts,
   corrupted lines and stock traffic mixed in) none broke the whole-jewel limits. One part of a
   jewel was lit more than three times in a second in 68 streams by the wide observers (4 times,
   never more) and in 4,577 streams by the gate's count (8 times or more at the worst).
3. **Four looks are drawn on a glowing bed** (sparkle, twinkle, chase, comet): no pixel of the look
   goes under two fifths of its level, so they are never dark between their bright pixels. Sparkle
   moves one spark every 350 to 477 ms. Chase steps every 130 ms at the least and scan and comet
   lap the ring in 684 ms at the least (the core), so `s` does little near the top for those.
4. **Leia through the contract is slower than from a button:** about three re-rolls a second (it
   waits at the latch in three passes of four), and the look that follows it waits up to 340 ms.
5. **Stock light after the contract lets go.** For 340 ms after a hand-back or a verb-`B` step, and
   once more from a frame that had to wait, stock frames are asked of the gates and can be held.
   An animated stock look can wait longer than those two windows: in an independent run of 360
   hand-backs the last held frame came up to 846 ms after the owner's command (Leia 835 ms, the
   colour projector 846 ms, the colour cycle 680 ms). The jewel was lit within 341 ms every time
   and the look then ran at its stock rate; a solid colour was up within 341 ms in 700 of 700.
   The stock light's own change of colour delays the next contract look by up to 170 ms.
6. **Verb `B` is a ride on the strip's level** on this board, not the look's `b=`. It dims stock
   light under an armed show too. A raise waits for the core's 340 ms window and for the gate.
7. **Colour under 4 levels is not judged.** A frame whose brightest LED is under 4 levels is not a
   look to the look gate (the core's rule), and the test's colour observer starts there too. The
   observer also leaves out a change of colour between frames so dim that the LEDs' rounding alone
   could explain it (one count an LED a channel).
8. **A held frame can be up for under a loop pass** when the look's next frame is due in the pass
   that releases it. The gate counts it as the onset.
9. **Take-over (L6) differs from the spec's wording in two places, by decision** (see "Native
   take-over" above): a stock servo command is not a take-over, and the owner's brightness command
   is not one either.
10. **The burst of section 11** (400 bytes with no gaps) holds in simulation with a 256-byte
    receive ring and passes of up to 4 ms. How long a pass takes on a board is not measured.
11. **The test's oracle sets the owner's brightness command aside** for 400 ms (it rescales the
    field under the observers), like any stock command.
12. **The beat clock can outlive the show.** After a take-over or a watchdog expiry with no unit
    holding anything, the clock can stay marked as running (5 of 300 hostile streams in an
    independent run). Nothing is drawn from it; `Q` reports a `bpm` where it should report 0.
13. **No image has been built from this code**, and the bench checklist below has not been run.

## Known limits (release 2, v1.4)

Everything here is from simulation. Nothing in this fork has run on a board. (The first round's limit 1,
a dim overlay storm after a crumbling step, said the bands read 3; in white and red they read 4, and
reading 6 of the v1.4 section closes it. Its limit 8, a colour that comes up dim dated later by the test's
observer than by the gate, is closed in the oracle: the observer now dates such a change at the first
dim frame that shows the new colour within its rounding, as the core's `LOOK_MIN_LEVEL` does. Soak stream
5200000 + 1561 no longer breaks on the layer of `63993d2`.)

1. **Under storms of `B` and `b=` raises the projector is mostly dark.** Every raise takes the window the
   brightening steps need: lit 1 to 12 % of the time in the scenarios, 0 % with `B` and `b=` by turns.
2. **In a colour the projector steps more slowly than stock** (S4, reading 3): 106.6 ms on average in
   `116530`, up to 170 ms between two steps.
3. **Under an overlay every 340 ms the projector holds still** (reading 4): its pixels do not move
   between overlays, and where the overlay is dim the base may not come back between two of them.
4. **A projector started within 340 ms of a rise of the unit's light** (another look's onset, a raise of
   `b=`) is dark until the window opens.
5. **`vu` is the meter on this board:** a solid fill at its level. With no clock and no `L` it is dark,
   and across a hand-over (Pause, resume) it can be dark for up to 340 ms.
6. **`logic` is twinkle here.** It is the logics' look (spec v1.4 section 4).
7. **An accent over a projector waits for the latch** (reading 6): its first frame comes at least 340 ms
   after the jewel's last frame in which a LED got brighter. White accents on every beat at 120 BPM were 1 ms
   late at the most (between them the base holds still, reading 4, and returns no brighter than the
   accent); `P:c=ff00ff,b=120,d=250` on every beat came 68.7 ms late on average, 201 ms at the most, and 8
   of 40 were over before they could show (the base's return lights what the dim accent had dimmed, and
   the next accent waits 340 ms from it). Accents over a projector do not always land on the beat.

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
    two, three or five bright pixels on a glow, one moving about every half second down to every
    third of a second. `i=twinkle`: every pixel
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

22. **CA1.7.** `!HFA:i=chase,s=255` and `!HFA:i=scan,s=255`: a chase step is about an eighth of a second
    and a scan takes two thirds of a second round the ring, whatever `s` says above that.
    `!HFA:i=comet,c=ffffff` then `!HFA:i=comet,c=00ff00`: the comet turns green where it is. A solid
    look with `!HFA:b=40` and `!HFA:b=255` by turns as fast as the host can: it brightens at most
    three times a second.

**Conformance record.** For each of S1, S2, S4, S6, S7, S8, S9, S10, S11, C1, C6, L1 to L6, capacity,
`V`, `Q` and section 11: the line sent, what was seen, pass or fail, and the `fw` from the WDP announce.

## Bench list for release 2: the holoprojectors (not run)

Gate 2 of the plan `2026-11-21-lighting-wave-2.md` (Signal Booster), the holoprojector rows and the rows for
every board, with this board's own checks after them. Gate 1 (the release 1 checklist above) comes first.
Nothing below has been done; write what was seen in the last column.

| # | Check | Result |
| --- | --- | --- |
| 11 | `!HFA:i=projector` on one HP beside stock Leia (`R001`) on another, and beside R1's `!HFA:i=native:1`: does it read as a hologram? | |
| 12 | `projector` in red (`ff0000`), amber (`ffbf00`), white and magenta (`ff00ff`): the tints look like the stock colour projector's rows (LED function 2 in the same colour) | |
| 13 | A two-bar burst from dark and back to dark, on a downbeat (two scored entries, Studio's planned twitch): it comes up on the beat and goes dark on time | |
| 14 | No servo moves during any of 11 to 13, nor during 18 to 21 | |
| 15 | A full song from Studio at release 2, filmed, beside its filmstrip | |
| 16 | A dome with one board left on R1: Studio plans it as R1 and the show plays | |
| 17 | Seed-to-light latency again with the R2 looks up | |
| 18 | `!HFL:v=128`, then `!HFA:i=vu,c=ffffff,am=2,m=200` and `!**C:bpm=120,ph=0,bpb=4`: the jewel goes dark, comes up about a third of a second later and jumps on the beat, falling between, never more than three times a second; `!HFL:v=255` during it changes nothing; `!**C:bpm=0`: back to the meter at 128 (the last `L` before the pump), at once or within a third of a second; resume: the pump again | |
| 19 | `projector` with `!HFP:c=ffffff,d=170` every beat at 120 BPM: after each accent the same pixels come back, and they hold still while the accents go on (known limit 3) | |
| 20 | `projector` with `!HFA:b=40` and `!HFA:b=255` by turns as fast as the host can: it dims at once and comes back at most three times a second | |
| 21 | `projector` in `116530` and in white side by side: say whether the colour one flickers noticeably more slowly (known limit 2) | |
| 22 | `projector` in `116530` with `!HFP:c=ff00ff,b=120,d=250` on every beat at 120 BPM: say how late the accents look and how many are missing (known limit 7); the same with a white accent | |

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
  `contract.1.3` is the contract version this build implements (spec section 12; `contract.1.4` on the R2
  line). The
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
