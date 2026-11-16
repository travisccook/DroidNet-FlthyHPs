# DroidNet-FlthyHPs

Part of [DroidNet](https://github.com/travisccook/DroidNetSignalBooster-releases), the Signal Booster for droids. This is one of its board firmwares.

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

## Status

**Work in progress.** This DroidNet build is provided as is, without warranty of any kind. Use it at your own risk. It has never been flashed to a board; see [`firmware/README.md`](firmware/README.md). The prebuilt image there was built from commit `9d28c52`, before contract v1.3: no image has been built from the code described on this page. To go back, flash the upstream firmware (Ryan's original firmware, linked at the top of this page).

**Testing.** It cross-compiles for the real microcontroller. `pio run` builds a complete
firmware image with the real avr-gcc for the real ATmega2560, and the real linker signs off on it.
That is a genuine step up from where this fork started — it used to be checked only against a
hand-written mock, and mocks lie (on the sister PSI fork, one hid a critical bug by making the
non-latching render primitive a no-op). Verified here:

- `test/host/test_contract_core.cpp` — 341 checks against the wire parser, beat clock, effect math
  and score table, and `test/host/test_contract_v13.cpp` — 1,895,037 checks of the v1.3 rules in the
  shared core (safety limits, clock, show lifecycle, score, load check, query reply), run a second
  time with stand-ins for the flash-string hooks this board uses. Both run on a normal computer.
- `test/host/sim` — the **real `src/main.cpp`** on a fake Arduino core, with a 9600-baud wire in
  both directions (the receive ring that loses bytes when full, the transmit ring that blocks a write
  when full) and loop passes of random length: 76 scenarios, from the stock commands and the auto
  twitch to bursts, a pushed score, the watchdog and the load check. Three things are checked on
  every simulated pass: a pass that handles a `!` line (whole, spliced or corrupted) starts no servo
  move and writes no `HP_command[]`; a unit the contract held does not move in the pass that lets it
  go; and, in the photometry scenarios, no jewel goes from dark to lit more than three times a
  second. One scenario runs every single-fault corruption of every realistic contract line (45,786
  of them), one runs 150 random and hostile line streams and measures the latched frames; in every
  second stream every relight counts, whoever drew the pixels.
  A scenario named in `test/host/sim/xfail.txt` records a known defect and must fail; the file is
  empty.
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

Flash is comfortable on this board, unlike its PSI sibling: **40,096 B of 253,952 B (15.8%)**, with
SRAM at 4,534 B of 8,192 B (55.3%); 2,112 B of that is the three 32-section score tables.

The stack has been bounded statically too (`test/host/stack_report.py`): **430 B of the 3,658 B
available (12%)**, with no recursion, no `alloca`/VLA, and — unlike the PSI — **no interrupt-nesting
hazard**: no interrupt handler reaches a `sei`, so interrupts cannot nest. The I2C handler does not
render LEDs and does not touch a `String`: it copies the bytes into its own fixed buffer, and `loop()`
takes the line from there with interrupts off. (The tool takes any two bytes of initialised data that
equal a function's address for a function pointer. On one build of this round two letters of the
string "Flthy HP Controller" matched `RCHP()` and it reported an interrupt path that does not exist;
if its verdict ever reads "not sound", check for that first.) (One caveat: this board
*does* link the heap — Arduino `String` pulls in `malloc`/`free` — and the heap grows up toward the
stack. With 3.6 KB free that is not an overflow risk, but long-run fragmentation is not something a
static analysis can rule out.)

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
         V = verify a pushed score
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

### Contract v1.3 on this board

This build implements release 1 of contract v1.3 (`ver=1.3,rel=1` in the query reply, `contract.1.3`
in the WDP announce). What that means on the holoprojectors:

- **A show holds a unit; it does not change your settings.** `!H*M:v=show` arms a show. From then the
  auto servo twitch waits, and the stock LED look (off colour, LED twitch) stays until the show
  begins: its first `C`, live `A` or `P`. `M:v=idle` gives the jewel back at its off colour with both
  twitch timers a full interval ahead. `enableTwitchLED[]`, `enableTwitchHP[]` and the off-colour
  override are never written, so `A198`/`A199` sent mid-show still mean what they say afterwards.
- **`X`** blacks the unit out and keeps it in its mode. Outside a show the jewel is the stock
  firmware's again after one twitch interval.
- **Watchdog.** `M:v=show,wd=<seconds>` idles each unit by itself when no contract line for this
  board has arrived for that long. No `wd=`, no watchdog. The board remembers the last watched show:
  a unit that left it (your button, a failed load) and was lit again by the same host is idled with
  the rest when that host goes silent.
- **"Enable servo twitch" works in a show.** `A199` (any designator) sent while a show holds a unit
  lets that unit's servo twitch run again until the next `M:v=show` or `M:v=idle`; the lights stay
  the show's. `A198` stops it again.
- **Score.** 32 sections a unit, each with its own `b=` and accent. `V:n=<count>,k=<checksum>`
  checks what a unit holds; a bad load idles that unit instead of playing a partial show.
- **Query.** `Q:r=<route>` addressed to one unit is answered with a routed line,
  `;S0!H<u>q:ver=1.3,rel=1,cap=32,n=<held>,ok=<0|1|2>,bpm=<n>` (or behind `;W<n>,`), so the WCB does
  not broadcast it. Any other `Q` gets no reply. Each unit asked gets its own reply, one a loop
  pass, sent whole when the transmit buffer has room for it, never in the middle of a WDP announce,
  and never by waiting.
- **Brightness.** `b=` is a fraction of the jewel's own level (`BRIGHT 100`, or what `H<unit>B<level>`
  last set): `b=255` is exactly as bright as a stock command. Verb `B` rides below that level inside
  a show only, and never outlives it.
- **Native looks.** `i=native:<n>` runs `1` (Leia) and `6` (rainbow). A line naming any other code is
  dropped whole. An accent never restarts or replaces a native look.
- **A button always works.** A stock LED command or `S<n>` sequence addressed to a unit in a show
  ends the show on that unit, as `M:v=idle` would, and then runs; the other units play on. A stock
  servo command runs as it always did and leaves the show's lights alone. A `P` with no show armed
  overlays the stock look and gives it back afterwards.
- **Limits.** Tempo 30 to 300 BPM (a `C` outside it is ignored); no light state shorter than 170 ms
  (live looks, accents, flash, pulse); `flash`, `pulse` and `scan` run at the same rates as on the
  Logics and the PSIs; `d=` on `A` is ignored.
- **One limit for every path (S9).** Whatever lines arrive, a jewel goes from dark to lit across its
  whole field at most three times a second. This is kept where frames are sent to the LEDs, with the
  rule all three DroidNet boards share: the whole field may step up by a quarter of its ceiling or
  more only 340 ms after it last lit or last got brighter; a frame that comes sooner waits, and a
  fall or a blackout never waits. It applies to a jewel the contract has, or handed back in the last
  340 ms, native looks the contract started included (a contract Leia flickers a little more calmly
  than the stock one), and to a jewel verb `B` is dimming, even when the light on it is still your
  stock look (between Send and Play). Otherwise your own stock looks are never held back, the fast
  ones included.
- **Pause.** `C:bpm=0` holds the clock and the score. A look that was pumping with the beat is shown
  still, at its own `b=` level, while held.
- **Lines and bursts.** A line may be 128 bytes. A PlatformIO build has a 256-byte UART receive ring;
  an Arduino IDE build keeps the core's 64 bytes and needs paced lines. One line is handled per loop
  pass, so how big a burst survives depends on how long a pass takes, **which has not been measured
  on a board**. In simulation a 400-byte burst with no gaps arrives whole with random passes of up
  to 10 ms; with passes of 10 to 20 ms a burst of 80 five-byte lines loses bytes in 19 runs of 20.
  With a holoprojector in stock RC mode (`F102`, `F103`) a pass takes about 23 ms and bursts do lose
  lines. A lost or cut line can cost a look; it cannot move a servo (below).

### Every native command still works

This is additive and backward-compatible. It is **not** a rewrite. Every `DT##` LED command, every
servo command, every `S#` sequence, the auto-twitch system, and the I2C intake all behave exactly
as they did before, with two differences. A stock LED command or sequence sent mid-show ends the show
on the units it addresses (above). And a stock line too short to carry a type or a function (one to
three characters, such as a bare `X`) now does nothing: stock ran the previous command's type and
function again, which is how one wire fault in `!**X` could repeat the last servo command. The contract path is entered only when the first byte of a
command is `!`, and the brightness command only when it is `H`.

### LED effects only. The contract never moves a servo. Ever.

This is a deliberate safety invariant, and builders should know about it:

- The `!` path **never writes `HP_command[]`** and never calls `positionHP` / `twitchHP` / `wagHP`
  / `RCHP`. It cannot command holoprojector motion, because it has no code path to.
- An armed show (`!H*M:v=show`) makes the auto servo twitch *wait*, so a light show cannot cause the
  HPs to start moving on their own mid-song. It does this without writing your twitch setting.
- Whenever the contract lets go of a unit (`v=idle`, the watchdog, a bad load, a stock command taking
  over), it first puts both twitch timers a full interval ahead. The timers are long overdue by
  then, and without that the stock loop would twitch the servo in the very pass that handled the
  line. A stray `idle` that never followed a `show` touches nothing.
- The simulation checks this on the real `src/main.cpp`, on every pass that handles a `!` line,
  whole, cut short, spliced by a full receive buffer or corrupted by a wire fault: every single-fault
  corruption of every realistic contract line is run, after each kind of stock servo command.

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
- **Up to 255 on purpose, and the contract follows it.** The level this command sets is the ceiling
  the contract's `b=` is a fraction of. Sent during a show it moves that ceiling and ends any
  verb-`B` ride; the contract never writes the level back.
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
@WDP1 {"type":"Flthy HP Controller","fw":"<commit>","caps":["droidnet.contract","contract.1.3","hp.servo","hp.led"]}
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
| `src/droidnet/dn_strip.h` | **ours** | The jewels' strip type: `Adafruit_NeoPixel` whose `show()` passes the contract's onset gate (S9). |
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
- `INPUTBUFFERLEN` grown from 10 to 128, with a flood guard added in `serialEvent()` so a long line
  drops its tail instead of overrunning, and `serialEvent()` stops at a line's terminator so the next
  line cannot be appended to one `loop()` has not handled yet,
- the I2C receive handler copies into its own buffer instead of the serial line's `String`, and
  `loop()` takes an I2C line first,
- two guards on the native auto twitch, `!contractLedTwitchHeld(i) &&` and
  `!contractServoTwitchHeld(i) &&`, first in each test, so it does not stomp a contract look or twitch
  a servo during a show, and so a hold that ends re-arms an overdue timer before the stock test reads
  it; and `dnServoTwitchOverdue()`, a one-line read-only view of the servo twitch timer for the layer,
- `typeState = 255; functionState = 255;` in front of the stock designator parse, so a short line
  cannot reuse the previous command's type and function,
- the jewels declared as `DnStrip` (`droidnet/dn_strip.h`: `Adafruit_NeoPixel` whose `show()` passes
  the contract's onset gate) instead of `Adafruit_NeoPixel`,
- one `contractOwnerBrightness()` call in the brightness branch, `contractNativeTakeover()` calls in
  front of the stock LED command and `S<n>` dispatch (a button ends the show on its unit), and
  `contractOwnerServoTwitch()` calls for `198` and `199`,
- a `#ifdef DN_LOOP_PROBE` block at the end of `loop()`, compiled only into the bench build.

Of everything `git diff 5385a7c..HEAD --stat` lists, only the files marked **ours** above and our
hooks in `src/main.cpp` are code we wrote; the rest is the GPL-3.0 license text we added for
Graham's library, the READMEs and the build files, and the prebuilt images under `firmware/`.
Ryan's firmware is the rest of the repository.

---

## Building and testing

The host suite needs nothing but a C++ compiler:

```bash
bash test/host/run.sh
# [0]  the shared core files are the ones src/contract/CORE_PIN names (needs an RSeries checkout)
# [1]  contract_core parser unit tests             -> 341/341 checks
# [1f] contract_core v1.3 unit tests               -> 1,895,037/1,895,037 checks
# [1g] the same, with flash-string hook stand-ins  -> 1,895,037/1,895,037 checks
# [1b] wdp_announce_core unit tests                -> 37,506/37,506 checks
# [1c] wdp_fork.h announce adapter + identity      -> 19/19 checks, with fw "abcdef0-dirty" and "unknown"
# [2]  ContractFlthy.h firmware type-check, against a mock
# [2b] whole-firmware simulation                   -> 76 scenarios: the real src/main.cpp on a fake
#      Arduino core; a scenario named in test/host/sim/xfail.txt must fail (the file is empty)
# [3]  static source guards (LED-only invariant for the contract and the brightness branch,
#      wire budget, no Serial write that could land inside a WDP announce)
# [4]  contract parser fuzz + differential         -> 1,581,274 checks (ASan/UBSan)
# [5]  native_ext brightness parser unit tests     -> 77/77 checks
# [6]  native_ext fuzz + differential              -> 2,965,122 checks (ASan/UBSan)
# [7]  REAL cross-compile (ATmega2560), firmware and the bench probe build
#                                                  <- needs PlatformIO; skipped (and reported) without it
# ONLY="1 4" runs just those stages. The fuzz stages need a compiler that can link ASan/UBSan.
```

`python3 test/host/stack_report.py --elf .pio/build/FlthyHPs/firmware.elf --mcu atmega2560 --objdump
~/.platformio/packages/toolchain-atmelavr/bin/avr-objdump` gives the static stack bound after a build.

To build the firmware:

```bash
pio run                 # build (ATmega2560 / Mega ADK)
pio run -t upload       # flash it (add --upload-port /dev/cu.usbmodemXXXX)
pio run -e FlthyHPs_probe   # bench build: adds "#lp n=<passes> max_us=<longest pass>" once a second on Serial
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

Remember: a green suite means it compiles for the real MCU. It does not replace running it on a board —
see Status above.

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
