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

## Notice: licensing, permission, and how to make us take this down

Please read this before you do anything else with this repository.

**The original FlthyHPs firmware is copyright Ryan Sondgeroth.** It carries no license file — not
in v1.8 (our baseline) and not in the current v2.1. His manual has a section titled "The Rules",
which says, verbatim:

> "These files are shared for personal use only. You may not offer/sell a system using my design
> in part or whole without my express permission."

We take that at face value and we intend to respect it exactly.

- We claim **no rights whatsoever** over Ryan's work. It is his.
- This is a hobby project on one person's droid. It is entirely non-commercial. Nothing here is
  sold, nothing here will be sold, and this fork is not a product and never will be.
- Whether to license the original project, and how, is Ryan's call and nobody else's.
- **If Ryan would prefer this repository not exist, we will delete it immediately, without
  argument or discussion.** If that is you, Ryan: just say the word, anywhere, and it is gone.
- We do **not** redistribute his manual. `FlthyHPsManual_v1.8.pdf` was in our baseline import and
  has been removed; republishing his documentation wholesale would be indefensible and it is not
  ours to hand out. Get the manual, and the original firmware, from Ryan:
  <https://github.com/ryan-sondgeroth/FlthyHPs>.

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
  holoprojector servo) and the serial wire budget.
- `bash test/host/run.sh` now finishes by cross-compiling the firmware for real.

Flash is comfortable on this board, unlike its PSI sibling: **32,570 B of 253,952 B (12.8%)**, with
SRAM at 2,332 B of 8,192 B (28.5%). Nowhere near either ceiling.

**What is NOT verified.** Everything only a physical board can tell you. **A successful link is not a
bench test.** That the loop stays stable, that the timing holds, that a jewel looks like anything you
would want on a droid, what the power draw is — all **unverified**. The code has never executed a
single instruction outside a host test.

If you are going to put this near a real droid: bench-test it on a jewel that is not installed in a
dome, and be ready for it to be wrong. This is a starting point, not a release.

---

## What this fork adds

One thing: an additive **Driveable-Animation Contract**, so an external tool (Cantina Studio — a
music-analysis and dome-lighting authoring tool) can drive the holoprojector jewels in time with a
song.

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
as they did before. The contract path is entered only when the first byte of a command is `!`.

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

---

## What is theirs, and what is ours

Honest, file-level:

| Path | Whose | What |
| --- | --- | --- |
| `src/main.cpp` | **Ryan Sondgeroth** | The firmware. 1,567 lines. Ours: ~45 lines of hooks (see below). |
| `include/functions.h` | **Ryan Sondgeroth** | Function declarations. Untouched. |
| `lib/Servos/*` | **Graham Short** (BHD) | The slow-servo library, GPL-3.0-or-later. Untouched except that we added the missing `LICENSE` file. |
| `README_C2B5.md` | prior collection | The README that came with our vendored copy. Not Ryan's own words. |
| `src/contract/contract_core.h` | **ours** (362 lines) | Wire parser, beat clock, effect math, score table. Pure C++, no dependencies. Byte-identical across all three DroidNet forks. |
| `src/contract/ContractFlthy.h` | **ours** (523 lines) | The render layer: maps the contract onto *this* board's LED primitives. |
| `test/host/*` | **ours** (723 lines) | The host test harness. |
| `LICENSE-DroidNet-Contract` | **ours** | MIT, covering only our own files. |

The hooks in `src/main.cpp` are the whole of our footprint in Ryan's file, and they are small on
purpose:

- one `#include` of the contract layer,
- one `else if (inputBuffer[0] == '!')` branch at the front of the command dispatch,
- one `contractBeatTick()` call in the loop,
- a contiguous block of render slots (`case 101..115`) added to the existing LED switch,
- `INPUTBUFFERLEN` grown from 10 to 80, with a flood guard added in `serialEvent()` so a long line
  drops its tail instead of overrunning,
- two `&& !gUnit[i].active` guards so the native auto-twitch does not stomp a live contract look,
- one boot-time snapshot (`startEnableTwitchHP[]`) mirroring the existing `startEnableTwitchLED[]`,
  so show mode can restore what it found.

`git diff 5385a7c..HEAD --stat` says 2,806 insertions across 14 files. Only 1,645 of those
lines are code we wrote — the seven files listed above; the rest is the GPL-3.0 license text we
added for Graham's library, the READMEs, and this notice. Ryan's firmware is the rest of the
repository.

---

## Building and testing

The host suite needs nothing but a C++ compiler:

```bash
bash test/host/run.sh
# [1/4] contract_core parser unit tests   -> 341/341 checks
# [2/4] ContractFlthy.h firmware type-check
# [3/4] static source guards (LED-only invariant + wire budget)
# [4/4] REAL cross-compile (ATmega2560)   <- needs PlatformIO; skips cleanly without it
```

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
  collection is private and stays private; it is not a public source you can go and read, and we
  are not presenting it as one.
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

---

## License

There are three different situations in this tree. All three matter.

**Ryan Sondgeroth's firmware (`src/main.cpp`, `include/functions.h`): no license, all rights
reserved by the author, personal use only, not for sale.** There is no LICENSE file for it here,
because it is not ours to write one. See the notice at the top of this README. If you want to use
his firmware, get it from him.

**Graham Short's slow-servo library (`lib/Servos/`): GPL-3.0-or-later.** It is compiled into the
sketch (used at `src/main.cpp:168`, `:686`, `:780`). The GPL requires the license text to travel
with the code, and it was missing, so we have added the full GPL-3.0 text at `lib/Servos/LICENSE`.
Graham's copyright headers in those four files are preserved exactly as he wrote them.

Combining a GPL-3.0 library with a personal-use-only sketch is a tension that exists in the
original project and that we inherited along with the code. We are not in a position to resolve it
on Ryan's or Graham's behalf, and we are not going to pretend we have. We are stating it plainly
because anyone redistributing this — including us — should know it is there.

**Our own additions (`src/contract/*`, `test/host/*`): MIT**, per
[LICENSE-DroidNet-Contract](LICENSE-DroidNet-Contract). That license covers those files and
nothing else. It grants you no rights to anyone else's code in this repository.
