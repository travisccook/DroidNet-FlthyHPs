# DroidNet-FlthyHPs

A small, additive fork of **FlthyHPs — the Holoprojector Control System by Ryan Sondgeroth
(FlthyMcNsty)**.

Nearly everything in this repository is Ryan's work. What we added is one extra command prefix
and a rendering layer behind it, so an external music-analysis tool can drive the holoprojector
LEDs in time with a song. His firmware still does everything it did before, unchanged.

Original project: <https://github.com/ryan-sondgeroth/FlthyHPs>. Go there first. Star it,
read the manual, and support the author.

---

## The original project

FlthyHPs is Ryan Sondgeroth's holoprojector control system for R2-D2-style droids. One Arduino
Mega ADK sketch runs the servo motion *and* the LED display for three holoprojectors (Front, Rear,
Top), each with a 7-LED Adafruit NeoPixel jewel, with servo control handed off to an Adafruit
16-channel I2C PWM breakout so that servo timing and NeoPixel timing stop fighting each other.

Reading the code, a few things stand out as genuinely good engineering, not just working code:

- **The command scheme is a tiny, well-designed language.** `DT##[C][S][R][P]` — one letter for
  the holoprojector (`F`/`R`/`T`, `A` for all, and `X`/`Y`/`Z` for the three pair combinations),
  one digit for LED-vs-servo, two for the sequence, then optional color/speed/position. It is
  compact, it is memorable, and it maps one-to-one onto the physical hardware. Appending `|25`
  to run a sequence for 25 seconds and then fall back to whatever the system was doing before is
  the kind of detail you only get from someone who actually operates the droid.
- **The twitch system is the whole illusion.** Auto LED twitch and auto HP twitch, both randomized
  in interval and run-time, per holoprojector, with an optional random-sequence mode. This is what
  makes the holos read as *alive* rather than as three lights that turn on when told. That is a
  design insight, and it is the reason people notice these HPs on a finished droid.
- **Running servos and LED sequences on one board without either one hitching is real work.** The
  choice of the PWM breakout plus Big Happy Dude's slow-servo library over the stock Arduino servo
  library is deliberate and correct, and the PROGMEM discipline used to keep the settings tables
  out of SRAM (v1.6) is careful, unglamorous engineering of the sort that makes a loop stable.
- **The documentation is better than most commercial products ship with.** The FlthyHPs manual is
  a real manual. Get it from Ryan's repo.

Ryan credits **LostRebel** and **Knightshade** for significant input on the system's design,
**skelmir** for a bug find (v1.7), and **IOIIOOO (Jason Cross)** for the custom "off colors"
feature (v1.8). Those names are carried forward here.

The slow-servo library bundled in `lib/Servos` is **Graham Short's** ("Big Happy Dude") work,
copyright (c) 2013, GPL-3.0-or-later.

---

## Warning: this has never run on hardware. Not once.

**Nothing in this fork has ever been flashed to a real board.** Not to a Mega ADK, not to a
NeoPixel jewel, not to a servo, not to anything.

Be precise about what is and is not verified, because the two are easy to blur:

**What IS verified.** It cross-compiles for the real microcontroller. `pio run` builds a complete
firmware image with the real avr-gcc for the real ATmega2560, and the real linker signs off on it.
That is a genuine step up from where this fork started — it used to be checked only against a
hand-written mock, and mocks lie (on the sister PSI fork, one hid a critical bug by making the
non-latching render primitive a no-op). Verified here:

- `test/host/test_contract_core.cpp` — 341 checks against the wire parser, beat clock, effect math
  and score table, compiled and run on a normal computer.
- `test/host/compile_contract.cpp` — a type-check of the firmware layer against a **mock** of the
  board's API (`test/host/mock_flthy.h`), plus the LED-only invariant (this layer must never move a
  holoprojector servo), the serial wire budget, and that a verb-Q reply never lands inside a WDP
  announce.
- `test/host/test_native_ext.cpp` — 77 accept/reject checks for the native brightness parser
  (`src/droidnet/native_ext.h`), and `test/host/fuzz_native_ext.cpp` — 2,965,122 differential
  checks of the same parser against an independent model under ASan/UBSan.
- `test/host/test_wdp_announce_core.cpp` — 37,506 checks of the WDP device announce core (the line,
  the WCB's field limits, the 25-30 s schedule and its per-board jitter, pieces no bigger than the TX
  room), shared with the other two forks; and `test/host/test_wdp_fork.cpp` — this board's announce
  identity and its `Serial` adapter, with a 13-character `-dirty` fw and with none.
- `bash test/host/run.sh` now finishes by cross-compiling the firmware for real.

Flash is comfortable on this board, unlike its PSI sibling: **34,510 B of 253,952 B (13.6%)**, with
SRAM at 2,578 B of 8,192 B (31.5%). Nowhere near either ceiling.

The stack has been bounded statically too (`test/host/stack_report.py`): **394 B of the 5,614 B
available (7%)**, with no recursion, no `alloca`/VLA, and — unlike the PSI — **no interrupt-nesting
hazard**. That last point is down to Ryan's choice of library, and it is worth crediting:
Adafruit_NeoPixel's `show()` saves and *restores* `SREG` rather than blindly re-enabling interrupts,
so even though this firmware also renders LEDs from inside its I2C interrupt, that interrupt cannot
re-enter itself. The sister PSI fork, which uses FastLED, is not so lucky. (One caveat: this board
*does* link the heap — Arduino `String` pulls in `malloc`/`free` — and the heap grows up toward the
stack. With 5.2 KB free that is not an overflow risk, but long-run fragmentation is not something a
static analysis can rule out.)

**What is NOT verified.** Everything only a physical board can tell you. **A successful link is not a
bench test.** That the loop stays stable, that the timing holds, that a jewel looks like anything you
would want on a droid, what the power draw is — all **unverified**. The code has never executed a
single instruction outside a host test.

If you are going to put this near a real droid: bench-test it on a jewel that is not installed in a
dome, and be ready for it to be wrong. This is a starting point, not a release.

---

## What this fork adds

Three things:

1. an additive **Driveable-Animation Contract**, so an external tool (Cantina Studio — a
   music-analysis and dome-lighting authoring tool) can drive the holoprojector jewels in time with
   a song (the rest of this section);
2. one native **brightness** command, `H<unit>B<level>` (see
   [One native command: brightness](#one-native-command-brightness) below);
3. a **WDP device announce**, so the WCB it is wired to knows what it is (see
   [WDP device announce](#wdp-device-announce) below).

The wire grammar is a single new command prefix:

```text
!<cls><unit><verb>[:k=v,k=v,...]

  cls    L = logic displays   P = PSI   H = holoprojectors   * = all
  unit   F = Front            R = Rear  T = Top              * = all
  verb   A = animate (set the base look)      P = pulse (a beat accent)
         C = beat-clock seed                  B = brightness
         L = level                            X = stop
         M = mode (show / idle)               Q = query
```

Example: `!HFA:i=comet,c=ff8800,s=200,m=64,am=1` — run a comet around the Front HP's jewel in
orange, at speed 200, pumped on the downbeat.

**The `!` prefix was chosen specifically because the stock parser already ignores it.** In the
original, a command that does not start with `F`/`R`/`T`/`A`/`X`/`Y`/`Z`/`S` is simply not a
command. That is what makes this layer additive instead of invasive: it lives in a space the
original firmware had already left empty.

What it buys:

- **Arbitrary RGB.** Colors go into the jewels as packed `0xRRGGBB`, bypassing the native 1-9
  palette entirely. The native palette still works for native commands.
- **Beat-locked accents.** A brightness envelope that pumps with the music (`am=` for the accent
  mode, `m=` for its depth).
- **A host-seeded beat clock** (verb `C`, with `bpm=` / `ph=` / `bpb=` / `beat=`), so the board
  keeps its own phase and stays with the song between messages.
- **Autonomous board-side section playback.** The host can push a compact "score" — a set of
  `verb A` looks each tagged with an `at=` beat — once, and the board plays the whole song's
  section changes by itself, on-beat, without further traffic.

Six new parametric effects render along a 1-D "strand" mapped onto the jewel: **comet, chase,
wipe, gradient, colorcycle, twinkle**. (On this board the strand is the six-LED ring; the center
pixel is handled separately per effect.)

### Every native command still works

This is additive and backward-compatible. It is **not** a rewrite. Every `DT##` LED command, every
servo command, every `S#` sequence, the auto-twitch system, and the I2C intake all behave exactly
as they did before. The contract path is entered only when the first byte of a command is `!`, and
the brightness command only when it is `H`.

### LED effects only. The contract never moves a servo. Ever.

This is a deliberate safety invariant, and builders should know about it:

- The `!` path **never writes `HP_command[]`** and never calls `positionHP` / `twitchHP` / `wagHP`
  / `RCHP`. It cannot command holoprojector motion, because it has no code path to.
- Entering show mode (`!H*M:v=show`) explicitly *disables* the auto servo twitch, so a light show
  cannot cause the HPs to start moving on their own mid-song.
- Leaving show mode (`v=idle`) restores **exactly** the operator's previous auto-twitch setting —
  never a hardcoded `true`. If you had HP twitch off, it stays off. A stray `idle` that never
  followed a `show` touches nothing.

Ryan's servo code is entirely his and is untouched.

### One native command: brightness

`H<unit>B<level>` sets the brightness of one or more jewels. Library id: `flthy.led.brightness`.

```text
H <unit> B <level>        e.g. HAB128 (all), HFB255 (front), HTB1 (top)

  unit    F R T X Y Z A, uppercase only, fanned out exactly like the stock designators:
          F, X, Y, A -> front;  R, X, Z, A -> rear;  T, Y, Z, A -> top
  level   1 to 3 digits, value 1-255 (leading zeros are accepted: HAB007 is 7)
```

- **Strict.** The line must end right after the level. Anything else is ignored silently, with no
  clamp and no reply: `HAB0`, `HAB256`, `HAB1000`, `hab128`, `HAb128`, `HAB128 x`, a merged line
  such as `HAB1283P100`, and any line the stock `|<n>` split fired on (`HAB128|5`). 0 never reaches
  `setBrightness()`, which would wipe the pixel buffer.
- **RAM only.** Nothing is written to EEPROM. The level lasts until the next command or a reset, and
  power-on is still `BRIGHT 100`. Adafruit stores `brightness + 1` and scales by `>> 8`, so
  `BRIGHT 100` is 101/256, about 39.5% of this command's scale; 255 is the unscaled hardware maximum,
  about 2.53× today's look.
- **Up to 255 on purpose.** The native command goes above the contract's
  `FLTHY_SAFE_MAX_BRIGHT = 200` jewel clamp. That is a deliberate choice (1-255), not a regression.
  The contract's own `B` verb is unchanged, and whichever of the two ran last sets the level.
- **Repaints at once.** For each addressed jewel the handler calls `setBrightness(level)` (which
  rescales the RAM buffer immediately), marks the contract's memoised looks for a repaint, and calls
  `show()`, so held frames change too.
- **LED-only, like the contract.** The parser is pure (`src/droidnet/native_ext.h`, no Arduino
  includes) and the handler sits between `// DroidNet native brightness BEGIN` and `END` markers in
  `src/main.cpp`. `test/host/run.sh` fails if either names the servo command path.
- **The `B` is load-bearing.** If the `H` is ever lost, the tail (`AB128`) is inert under the stock
  parser. Without it, `HA101` would leave `A101`, which centres every holoprojector servo.

Stock v1.81 has no `H` branch, so an unflashed board ignores the command.

### WDP device announce

The board tells the WCB it is wired to what it is, with the WDP device announce of Greg Hulette's
WCB firmware (7.0.1; `docs/WDP_DEVICE_ANNOUNCE.md` in his repo). 0.5-3 s after boot, then every
25-30 s, it sends one line:

```text
@WDP1 {"type":"Flthy HP Controller","fw":"<commit>","caps":["droidnet.contract","hp.servo","hp.led"]}
```

- **Where:** `Serial` (UART0, D0/D1) at 9600 baud, the WCB's port. The Mega's USB-serial chip shares
  UART0, so with a USB cable connected the line also shows on the serial monitor.
- **`fw` is the commit the firmware was built from:** `git rev-parse --short=7 HEAD`, the name
  `firmware/build-image.sh` gives the image, plus `-dirty` if the tree had uncommitted changes, or
  `unknown` for a build without git or without PlatformIO (`scripts/wdp_fw.py` sets it).
- **The WCB keeps the line to itself** and never forwards it; `?WDP,DA` on the WCB shows what each
  port has announced. WCB firmware that does not know WDP ignores the line.
- **It never holds up the loop.** The line is longer than the 63 bytes of `Serial`'s TX buffer, so it
  leaves a piece at a time, never more than the buffer has room for. Anything else this firmware
  writes to `Serial` (RCHP's `No RC Input Detected!` and the contract's verb-Q reply) first sends the
  rest of a line in flight, so no other text lands inside an announce. The `DEBUG` prints do not:
  with `DEBUG` turned on, one can split an announce, and the WCB drops that one.
- **Each board picks its own timing.** The interval is jittered from a seed of 16 reads of the unused
  A3 pin and `micros()`, so boards that power up together do not announce in lockstep.
- **Flashing is unchanged:** the WCB serial lead (D0/RX0 and D1/TX0) must still be unplugged while
  flashing; see [`firmware/README.md`](firmware/README.md).

`src/wdp/wdp_announce_core.h` builds and schedules the line, byte-identical across the three DroidNet
forks; `src/wdp/wdp_fork.h` holds this board's identity and its `Serial` adapter.

---

## What is theirs, and what is ours

Honest, file-level:

| Path | Whose | What |
| --- | --- | --- |
| `src/main.cpp` | **Ryan Sondgeroth** | The firmware. Ours: the hooks listed below. |
| `include/functions.h` | **Ryan Sondgeroth** | Function declarations. Untouched. |
| `lib/Servos/*` | **Graham Short** (BHD) | The slow-servo library, GPL-3.0-or-later. Untouched except that we added the missing `LICENSE` file. |
| `README_C2B5.md` | prior collection | The README that came with our vendored copy. Not Ryan's own words. |
| `src/contract/contract_core.h` | **ours** | Wire parser, beat clock, effect math, score table. Pure C++, no dependencies. Byte-identical across all three DroidNet forks. |
| `src/contract/ContractFlthy.h` | **ours** | The render layer: maps the contract onto *this* board's LED primitives. |
| `src/droidnet/native_ext.h` | **ours** | The pure parser for the native brightness command `H<unit>B<level>`. No dependencies. |
| `src/wdp/wdp_announce_core.h` | **ours** | The WDP device announce: builds the line, schedules it, hands it out in pieces. Pure C++, no dependencies. Byte-identical across all three DroidNet forks. |
| `src/wdp/wdp_fork.h` | **ours** | This board's announce identity and its `Serial` adapter. |
| `scripts/wdp_fw.py` | **ours** | PlatformIO pre-build script that puts the commit in the announce's `fw`. |
| `test/host/*` | **ours** | The host test harness. |
| `LICENSE-DroidNet-Contract` | **ours** | MIT license for the contract layer. |

The hooks in `src/main.cpp` are the whole of our footprint in Ryan's file, and they are small on
purpose:

- one `#include` of the contract layer,
- one `#include` of `droidnet/native_ext.h`, the native brightness parser,
- one `else if (inputBuffer[0] == '!')` branch at the front of the command dispatch,
- one marked `const bool dnPiped = (pipeIndex >= 1);` line right after the stock `|` search, so the
  brightness branch can tell that the stock split fired (the split itself is unchanged),
- one `else if (inputBuffer[0] == 'H')` branch, between the `!` branch and the designators, inside
  `// DroidNet native brightness BEGIN` / `END` markers,
- one `contractBeatTick()` call in the loop,
- one `#include` of `wdp/wdp_fork.h`, one `wdpForkSetup()` call in `setup()` right after the boot
  banner, one `wdpForkTick()` call in the loop next to `contractBeatTick()`, and one `wdpFinish();`
  in front of the stock `No RC Input Detected!` print, for the WDP device announce,
- a contiguous block of render slots (`case 101..115`) added to the existing LED switch,
- `INPUTBUFFERLEN` grown from 10 to 96, with a flood guard added in `serialEvent()` so a long line
  drops its tail instead of overrunning,
- two `&& !gUnit[i].active` guards so the native auto-twitch does not stomp a live contract look,
- one boot-time snapshot (`startEnableTwitchHP[]`) mirroring the existing `startEnableTwitchLED[]`,
  so show mode can restore what it found.

Of everything `git diff 5385a7c..HEAD --stat` lists, only the files marked **ours** above and our
hooks in `src/main.cpp` are code we wrote; the rest is the GPL-3.0 license text we added for
Graham's library, the READMEs and the build files, and the prebuilt images under `firmware/`.
Ryan's firmware is the rest of the repository.

---

## Building and testing

The host suite needs nothing but a C++ compiler:

```bash
bash test/host/run.sh
# [1/7] contract_core parser unit tests            -> 341/341 checks
# [1b/7] wdp_announce_core unit tests              -> 37,506/37,506 checks
# [1c/7] wdp_fork.h announce adapter + identity    -> 19/19 checks, with fw "abcdef0-dirty" and "unknown"
# [2/7] ContractFlthy.h firmware type-check
# [3/7] static source guards (LED-only invariant for the contract and the brightness branch,
#       wire budget, no Serial write that could land inside a WDP announce)
# [4/7] contract parser fuzz + differential        -> 1,554,686 checks (ASan/UBSan)
# [5/7] native_ext brightness parser unit tests    -> 77/77 checks
# [6/7] native_ext fuzz + differential             -> 2,965,122 checks (ASan/UBSan)
# [7/7] REAL cross-compile (ATmega2560)            <- needs PlatformIO; skips cleanly without it
```

`python3 test/host/stack_report.py --elf .pio/build/FlthyHPs/firmware.elf --mcu atmega2560 --objdump
~/.platformio/packages/toolchain-atmelavr/bin/avr-objdump` gives the static stack bound after a build.

To build the firmware:

```bash
pio run                 # build (ATmega2560 / Mega ADK)
pio run -t upload       # flash it (add --upload-port /dev/cu.usbmodemXXXX)
bash test/host/run.sh   # host checks + the same cross-compile
```

`platformio.ini` is in the tree and builds out of the box. (It did not used to be: the PlatformIO
layout — `src/` + `include/` — came from the droid-side working collection this was seeded from, not
from Ryan, whose upstream FlthyHPs is a flat Arduino sketch. That collection kept one project file at
its root, and it stayed behind when this board was vendored out. It has now been written and used.)

Dependencies are derived from what `src/main.cpp` actually includes — Adafruit NeoPixel and Adafruit
PWM Servo Driver, plus Wire from the Arduino AVR core. Graham Short's `Servos`/`SlowServo` library is
bundled at `lib/Servos` and is picked up automatically, since `lib/` is already PlatformIO's default
library directory.

For the original firmware and the real documentation, go to Ryan's repo.

Remember: a green suite means it compiles for the real MCU. It still proves nothing about hardware —
see the warning above.

---

## Provenance

- **Upstream:** FlthyHPs by Ryan Sondgeroth — <https://github.com/ryan-sondgeroth/FlthyHPs>. Our
  baseline is v1.8 (the sketch header says v1.81).
- **Then:** that firmware was vendored into a private working collection of firmware for one
  specific droid (a build called C2B5) and lightly customized there for that droid. That
  collection is private.
- **Then:** this repository — the same firmware, plus the additive contract layer described above,
  and nothing else.

Each step is someone else's work with a little more piled on top. The original is Ryan's.

---

## Credits

- **Ryan Sondgeroth** (FlthyMcNsty) — FlthyHPs: the firmware, the command language, the twitch
  system, the manual. All of it.
- **LostRebel** and **Knightshade** — significant input on both the general functions of the system
  and the code (credited by Ryan).
- **skelmir** — bug fix identified in v1.7 (credited by Ryan).
- **IOIIOOO (Jason Cross)** — the custom "off colors" feature in v1.8 (credited by Ryan).
- **Graham Short** ("Big Happy Dude") — the SlowServo / Servos library, (c) 2013, GPL-3.0-or-later.
- **Adafruit** — the NeoPixel and PWM Servo Driver libraries.
- The DroidNet contract layer — Travis Cook.
