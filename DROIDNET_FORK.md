# DroidNet fork notes — FlthyHPs

Working notes for the fork itself. The user-facing documentation, attribution, and the
"never run on hardware" warning are in [README.md](README.md) — read that first.

This is an **additive** fork of FlthyHPs by Ryan Sondgeroth
(<https://github.com/ryan-sondgeroth/FlthyHPs>). His firmware is the repository; our layer
is `src/contract/*`, `test/host/*`, and a handful of hooks in `src/main.cpp`.

## What the fork does

Implements the DroidNet **Driveable-Animation Contract** on the `!` command prefix, so
Cantina Studio can drive the three holoprojector jewels (F/R/T) with arbitrary RGB,
beat-locked accents, a host-seeded beat clock, and autonomous board-side section playback.

**LED effects only.** The `!` path never writes `HP_command[]` and never calls
`positionHP` / `twitchHP` / `wagHP` / `RCHP`. `M:v=show` pins `enableTwitchHP[*] = false`;
`M:v=idle` restores exactly what show observed (seeded from the `startEnableTwitchHP[]`
boot snapshot), never a literal `true`. No servo motion, ever.

Every native command (`DT##` LED, `DT1##` servo, `S#` sequences, the auto-twitch system, the
I2C intake) still behaves exactly as it did before.

It also adds **one native command**, `H<unit>B<level>` (library id `flthy.led.brightness`), which
sets jewel brightness 1-255 in RAM. See "Native brightness" below.

And it **announces itself to the WCB** (WDP device announce) every 25-30 s. See "WDP device
announce" below.

## Status

- **Contract layer: built.** Wire parser, beat clock, accent envelope, Phase-2 score table,
  and the per-board render layer are all in.
- **Six parametric effects: built.** comet, chase, wipe, gradient, colorcycle, twinkle —
  rendered around the jewel ring.
- **Host verification: green.** `bash test/host/run.sh` → 341/341 parser checks, a
  type-check of `ContractFlthy.h` against `test/host/mock_flthy.h`, 1,554,686 contract fuzz checks,
  77/77 native brightness parser checks and 2,965,122 native brightness fuzz checks, 37,506 WDP
  announce core checks and 19 adapter checks (run twice).
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
(RSeries / PSI / Flthy). If you change it here, the same change has to land in the other two,
byte for byte, or beat-clock and effect math drift apart between boards. It is pure C++ with
no Arduino dependency, which is why it can be unit-tested on a host.

`src/wdp/wdp_announce_core.h` and `test/host/test_wdp_announce_core.cpp` are byte-identical across
the three forks too (copied from DroidNet-RSeriesLogics 6645689).

## Board-specific notes

- The strand is the jewel: 7 NeoPixels per HP, `NEO_JEWEL_LEDS`. The spatial effects treat the
  6-LED ring as the 1-D strand and handle the center pixel per effect.
- Arbitrary RGB is written straight into the jewel as a packed `0xRRGGBB` via
  `setPixelColor()`, bypassing the native `basicColors` palette. The contract's envelope
  brightness is pre-scaled per write. Note that `setBrightness()` does **not** wait for `show()`:
  in Adafruit NeoPixel 1.15.x it rescales the RAM pixel buffer immediately (and lossily), and
  `show()` only transmits. (The "re-scales at show()" comments in `ContractFlthy.h` say otherwise
  and are wrong; the contract file is left untouched.)
- The per-unit look renders through `LED_command[hp].LEDFunction = 100 + effectId`, i.e. the
  `case 101..115` block added to the native render switch.
- `INPUTBUFFERLEN` was grown 10 → 96 (the contract's params need more than 10 bytes), and
  `serialEvent()` now drops trailing bytes past the buffer instead of overrunning it.

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
- **Handler:** per addressed jewel, `setBrightness(level)`, `gUnit[hp].lastEnvBright = -1` (the
  contract's memoised solid/meter/off looks repaint) and `show()` (held frames update now). RAM
  only; power-on is still `BRIGHT 100`.
- **Tests:** `test/host/test_native_ext.cpp` (the accept/reject table), `test/host/fuzz_native_ext.cpp`
  (fork-local differential fuzz; `test/host/fuzz_parsers.cpp` stays byte-identical across the three
  forks), and the `run.sh` stage 3 guard, which cuts the marked region out of `src/main.cpp` and
  fails closed if the markers or the `H` branch are missing, or if the region or the parser names
  the servo command path. `pio check` analyses `native_ext.h` too.

## WDP device announce

- **Line:** `@WDP1 {"type":"Flthy HP Controller","fw":"<commit>","caps":["droidnet.contract","hp.servo","hp.led"]}`
  plus `\n`, 101 bytes with a 7-character commit (107 with `-dirty`; the core's buffer is 128). The
  format is Greg Hulette's (`docs/WDP_DEVICE_ANNOUNCE.md` in the WCB repo, WCB 7.0.1); the WCB
  consumes the line and never forwards it.
- **Adapter:** `src/wdp/wdp_fork.h`: the identity, `wdpForkSetup()` (seed + first due time, called in
  `setup()` after the boot banner), `wdpForkTick()` (every loop pass, next to `contractBeatTick()`;
  writes at most `Serial.availableForWrite()` bytes, so it never blocks) and `wdpFinish()` (writes the
  rest of a line in flight; blocking is fine there).
- **Other writers:** every other `Serial` write once `loop()` runs calls `wdpFinish()` first: RCHP's
  `No RC Input Detected!` (`src/main.cpp`) and the verb-Q reply (`ContractFlthy.h`). The `DEBUG`
  prints do not; `DEBUG` is commented out. `run.sh` stage 3 fails on an unguarded non-`DEBUG`
  write, and `compile_contract.cpp` checks the verb-Q reply after a part-sent announce.
- **Seed:** 16 `analogRead(3)` samples (A3: unused by the sketch, already its `randomSeed()` source)
  folded FNV-1a style over `micros()`; the core's own xorshift generator then jitters 0.5-3 s for the
  first line and 25-30 s after.
- **fw:** `scripts/wdp_fw.py` (`extra_scripts = pre:`) defines `WDP_FW` with `env.StringifyMacro()`:
  `git rev-parse --short=7 HEAD`, `-dirty` if `git status --porcelain` lists anything, `unknown` if
  git fails; `wdp_fork.h` falls back to `"unknown"` when it is not defined (an Arduino IDE build).
  `firmware/build-image.sh` fails unless the image's fw string is exactly its `source_commit`.
- **Tests:** `run.sh` stages 1b (the shared core test, `-std=c++11 -Werror`) and 1c
  (`test/host/test_wdp_fork.cpp`, compiled with `WDP_FW` "abcdef0-dirty" and without it).
