# Review probe, 2026-11-15

**Unreviewed review probe.** These files were written during the lighting review of 2026-11-15 to check findings
against the real firmware. Nobody has reviewed them, `test/host/run.sh` does not run them, and nothing here is part
of the build. Part 0 of `docs/superpowers/plans/2026-11-16-lighting-firmware-flthy.md` (in the DroidNetSignalBooster
repo) promotes the simulation into `test/host/sim/` and wires it into `run.sh`. Delete this directory when that
lands.

## What it is

A host program that compiles the real `src/main.cpp`, with `src/contract/*`, `src/droidnet/*` and `src/wdp/*`,
against a fake Arduino core, and drives it through its real `setup()`, `loop()`, `serialEvent()` and `i2cEvent()`:

- `fake/Arduino.h`, `fake/Wire.h`, `fake/Servos.h`, `fake/Adafruit_PWMServoDriver.h`: the least of the Arduino
  API that `main.cpp` uses. `Servos::moveTo()` counts calls; nothing else models a servo.
- `fake/Adafruit_NeoPixel.h`: a strip that keeps the RAM buffer and the last latched frame, with the brightness
  math copied from Adafruit NeoPixel 1.15.5 (`setPixelColor()` at `Adafruit_NeoPixel.cpp:3447`, `setBrightness()`
  at `:3663`). The library source is not here. Fetch it to compare:
  `https://raw.githubusercontent.com/adafruit/Adafruit_NeoPixel/1.15.5/Adafruit_NeoPixel.cpp`. 1.15.5 is the
  version `firmware/manifest.json` records for the shipped image.
- A wire model: bytes arrive 1041.67 us apart (9600 baud, 8N1), `loop()` runs once per pass, `serialEvent()`
  after it, and the pass time is a parameter.

## The one substitution

`build.sh` and `sim-prototype/run_sim.sh` compile a temporary copy of `src/` in which `255/brightness` and
`180/brightness` in the stock `dimColorVal()` (`src/main.cpp:1429-1436`) become `AVRDIV(255,brightness)` and
`AVRDIV(180,brightness)`. The stock dim pulse divides by zero once per cycle. avr-libc returns -1 and carries on;
x86 raises SIGFPE. `AVRDIV` (in `fake/Arduino.h`) returns -1 for a zero divisor. Nothing else in the firmware is
changed, and the copy is deleted afterwards.

## Build and run

```bash
./run-findings.sh             # every probe scenario; needs g++ only
./build.sh                    # or build once; prints the binary's path
"$(./build.sh)" idlejerk      # and run one scenario
cd sim-prototype && ./run_sim.sh    # the assertion-based simulation (below)
```

The probe (`harness.cpp`, `extra.inc`) asserts nothing. Each scenario prints state and a `RESULT` line.

| Scenario | Finding | What it shows on the firmware as reviewed |
| -------- | ------- | ------------------------------------------ |
| `concat <pass_us> <phase_us>` (swept by `run-findings.sh`) | F1 | Back-to-back lines are spliced once a loop pass exceeds one byte time: at 1.5 ms a look is lost in every phase, at 2 ms and above Stop's `M:v=idle` is lost too |
| `replay2` | F1 | With a hand-placed interleaving, a beheaded `!**X` replays the last stock command: `HPFunction=5`, 96 `moveTo()` calls in 10 s |
| `idle 0`, `idle 1` | F2 | After `M:v=idle` no LED auto twitch starts in 30 min (15 in the 10 min before); without `X` the last frame stays |
| `idlejerk` | F2 | The pass that handles `M:v=idle` after a 4 min show twitches all three HPs: 6 `moveTo()` calls |
| `xnoshow` | F2, F4 | `X` outside a show leaves `LEDFunction=0`; here a stale stock timer happens to revive the twitch |
| `native 2`, `native 3`, `native 5`, `native3b` | F3 | `native:` looks run with option bytes of 255: `basicColors[255]`, and a dim pulse with `inter = -1908` |
| `sc7` | F3 | A second `native:7` never plays (`SCloop` stays 21) |
| `nativeaccent`, `scorenativeaccent` | F3 | One accent replaces a Leia section with the previous contract effect; a scored Leia section with `ae=` never shows Leia |
| `stalehalt` | F4 | A stock twitch timer flushes the contract look to black 22.6 s into the show |
| `showkeepsnative` | F4 | `M:v=show` leaves a stock look in flight rendering (900 `show()` calls in 2 s) |
| `hostdead` | F5 | Three hours after the last byte the board is still accenting, with both twitches off |
| `driftaccent 0`, `driftaccent 1` | F6 | Five bare re-anchors fire five extra accents (25 in 20 beats) |
| `opoff` | F7 | `A198` sent during a show is undone by idle |
| `bright` | F8 | Verb `B` does not show on a held solid, outlives the show, and `c=ffffff,b=255` latches 78 of the stock 100 |
| `pulsehz 255`, `pulsehz 128` | F9 | `pulse` at `s=255` is 4.0 full cycles a second |
| `dur` | F9 | `d=500` is inherited by the next look that has no `d=` |
| `showrate` | Performance | `show()` runs every pass for nine of twelve effects, and for the stock off-colour hold |

## sim-prototype

The same harness reshaped into assertions, as plan Part 0 wants it: `sim.h` (the world, the wire, the
behavioural LED-only invariant, `SCENARIO` and `CHECK`), `sim_main.cpp`, four scenario files and `run_sim.sh`,
which runs one process per scenario and treats the names in `xfail.txt` as expected failures.

- On the firmware as reviewed: 10 pass, 34 fail (all 34 are in `xfail.txt`).
- The LED-only invariant is behavioural: the pass that handles a line sent as `!...` (whole, spliced or
  beheaded) must call no `moveTo()` and write no `HP_command`. A stock servo twitch that was due in that pass
  anyway is excused.
- `scenarios_v13.inc` holds v1.3 conformance scenarios that need the shared core's R1 (watchdog, verb `V`, the
  `Q` line, the BPM range, the rate divisor, the live-look hold, saturating parameters, capacity 32). They have
  only been seen to fail. Nothing has made them pass yet.

## r1-prototype

A prototype of the Flthy-side R1 fixes, used to check the plan's code. It is not the implementation.

- `patch_main.py`, `patch_cf.py`: the changes to `src/main.cpp` and `src/contract/ContractFlthy.h` as exact
  text replacements, in levels that match the plan's tasks.
- `try.sh "<main levels>" "<ContractFlthy levels>"`: applies the levels to a temporary copy and runs the
  simulation. `./try.sh 12AD AGBCDE` is everything: 34 pass, and the 10 that need the core's R1 fail.
- `r1-prototype.patch`: all levels as one diff against commit `f462a89` (`main.cpp` hunks carry CRLF).
- `host-guards.patch`: what `test/host/mock_flthy.h` and `test/host/compile_contract.cpp` need so the existing
  mock guards pass against the prototype.

Scenarios that turn green at each level (cumulative, measured):

| Level | Newly passing |
| ----- | ------------- |
| main `1` | `f1_back_to_back_lines`, `f1_beheaded_tail_cannot_reach_the_servos`, `f1_crlf_and_blank_lines` |
| main `2` | `i2c_does_not_wipe_a_serial_line` |
| main `A`, contract `A` | `f1_led_only_fuzz`, the five `f2_*`, `f4_show_cancels_a_stock_look_in_flight`, `f4_stale_halt_does_not_kill_a_look`, both `f7_*` |
| contract `G` | `f4_a_stock_command_takes_the_jewel_back`, `f9_d_expiry_outside_a_show_gives_the_jewel_back` |
| contract `B` | `f3_native_allow_list`, `f3_no_accent_replaces_a_native_look` |
| contract `C` | `l3_pulse_over_the_stock_look` |
| main `D`, contract `D` | `f8_verb_b`, `s6_full_brightness_is_the_owners_level`, `s6_owner_brightness_during_a_show_sticks` |
| contract `E` | `f9_pulse_obeys_the_rate_floor`, `f9_d_is_not_inherited_by_a_new_look` |

## Limits

- Host `int` is 32 bits; the ATmega2560's is 16. Width bugs do not show here.
- No timing is real. The loop-pass time is an input, not a measurement. The plan's bench checklist measures it.
- `random()` is the host's `rand()` with a fixed seed.
- The I2C interrupt is a function call between passes. It cannot land inside a `String` operation.
