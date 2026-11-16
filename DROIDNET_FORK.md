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
- **Whole-firmware simulation: 78 scenarios pass.** Stage 2b of `run.sh`
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
- **Build:** flash 43,140 B of 253,952; SRAM 4,606 B of 8,192 (the score tables are 2,112 B).
  `stack_report.py`: worst-case stack 451 B of the 3,586 B left, 3,135 B spare. The heap (Arduino
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
- **Native looks (S10).** `nativeGatedFlthy()` says which kind. `native:6` (rainbow) RUNS AS STOCK:
  `contractNativeMayStart()` before the LEDs are handed over, `contractNativeStarted()` at the first
  lit frame with the level it runs at, `contractNativeLevel()` when a verb-`B` ride moves it,
  `contractNativeEnded()` when a contract look replaces it; its frames are not asked. `native:1`
  (Leia) is GATED: `contractNativeMayStart()` too, then every frame is asked like a contract frame,
  and because Leia re-rolls the colour of the whole field by itself, a frame whose mean colour is far
  from the one on the jewel also waits 170 ms after the last change. While a native start waits,
  nothing lit is latched. As stock, Leia makes about four whole-field swings of a tenth or more a
  second (an open question for the owner); `s10_native_looks_are_classified_by_their_stock_field_swing`
  measures both looks and fails if the core's list and the measurement drift apart.
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
  | solid, flash, pulse, rainbow, scan, meter, comet, chase, wipe, gradient, colorcycle | never |
  | the beat pump on solid (`am=1`, `m=` 64 and 255, 60 to 240 BPM) | never |
  | twinkle | 0.9 % at `s=255`, never slower |
  | **sparkle** | **1.1 % at `s=0`, 16.4 % at `s=128`, 37.5 % at `s=255`** (3.3, 18.2, 35.9 with slow passes) |
  | `native:6` rainbow (runs as stock) | never |
  | `native:1` Leia (gated) | 39.8 % |

  Sparkle is a finding, not a pass: on seven pixels it re-rolls enough of the field at once to swing
  the whole-field level by a tenth most frames, so it runs visibly calmer than drawn. Redrawing it
  (fewer pixels a frame, or a steadier total) is the fix; it is not done.
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
17. **Sparkle.** `!HFA:i=sparkle,c=ffffff` at `s=0`, `s=128` and `s=255` beside a jewel on the stock
    firmware's sparkle-like look if there is one: judge whether the gated sparkle is acceptable or
    should be redrawn (the gate holds over a third of its frames at `s=255` in simulation).
18. **Pulse, wipe, flash and chase after CA1.5.** `s=255` pulse and wipe: a 680 ms cycle; flash: 190 ms
    a state; chase: two of the six ring pixels lit at every step.

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
