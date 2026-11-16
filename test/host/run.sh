#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
# SPDX-License-Identifier: MIT
#
# Host checks for the Flthy HPs fork (stages 1-6 need no hardware and no AVR toolchain; stage 7 is the real cross-compile):
#   0.  the shared core files are the ones src/contract/CORE_PIN names (needs the RSeries checkout)
#   1.  contract_core.h parser unit tests
#   1f. contract_core.h v1.3 unit tests (safety limits, clock rules, lifecycle, score, query)
#   1g. the same, built with stand-ins for this fork's flash-string hooks (ContractFlthy.h defines them on the AVR)
#   1h. no floating point in the core or in the layer
#   1b. wdp_announce_core.h (WDP device announce) unit tests, shared with the other two forks
#   1c. wdp_fork.h, this fork's announce adapter and identity
#   2.  ContractFlthy.h firmware-layer type-check against a mock of the board API
#   2b. whole-firmware simulation: the REAL src/main.cpp on a fake Arduino core (test/host/sim)
#   3.  static source guards (the LED-only invariant, the serial wire budget, and no Serial write
#       that could land inside a WDP announce)
#   4.  contract parser fuzz + differential (ASan/UBSan)
#   5.  native_ext.h (H<unit>B<level> brightness) parser unit tests
#   6.  native_ext.h fuzz + differential (ASan/UBSan; fork-local, not the shared fuzz_parsers.cpp)
#   7.  REAL cross-compile for the ATmega2560 (optional; needs PlatformIO)
#
# How it runs (the same shape as the RSeries fork's runner):
#   * Any C++17 compiler: clang++ if present, else g++ (override with CXX=...).
#   * The fuzz stages need ASan + UBSan. The first of $CXX, clang++, g++ that can BUILD AND RUN a
#     sanitized binary here is used for them. If none can, the run FAILS (it does not quietly skip
#     them); ALLOW_NO_SANITIZER=1 runs them unsanitized, with a warning.
#   * Binaries go to a fresh temporary directory that is removed on exit.
#   * ONLY="1 4" runs just those stage ids. The last lines list what ran and what was skipped.
#   * Stage 7 is skipped without PlatformIO; REQUIRE_PIO=1 turns that into a failure.
set -euo pipefail
cd "$(dirname "$0")"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/flthy-host.XXXXXX")"
STAGE="start"
trap 'rc=$?; rm -rf "$OUT"; [ $rc -eq 0 ] || echo "run.sh STOPPED in stage [$STAGE] (exit $rc): the later stages did NOT run."' EXIT

CXX="${CXX:-$(command -v clang++ || command -v g++ || true)}"
[ -n "$CXX" ] || { echo "FAIL: no C++ compiler found (clang++ or g++)"; exit 1; }
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
san_ok() {   # can this compiler build AND run a sanitized binary here?
  echo 'int main(){return 0;}' > "$OUT/san.cpp"
  "$1" $SAN "$OUT/san.cpp" -o "$OUT/san" 2>/dev/null && "$OUT/san"
}
SANCXX=""
for c in "$CXX" clang++ g++; do
  if command -v "$c" >/dev/null 2>&1 && san_ok "$c"; then SANCXX="$c"; break; fi
done
if [ -z "$SANCXX" ] && [ "${ALLOW_NO_SANITIZER:-0}" != 1 ]; then
  echo "FAIL: no compiler here can link ASan/UBSan, so the fuzz stages cannot run."
  echo "      Install one (apt: libclang-rt-dev, or g++), or set ALLOW_NO_SANITIZER=1 to run them unsanitized."
  exit 1
fi
if [ -z "$SANCXX" ]; then
  echo "WARNING: fuzz stages run WITHOUT sanitizers (ALLOW_NO_SANITIZER=1)"; SANCXX="$CXX"; SAN=""
fi
echo "compiler: $CXX   sanitized stages: $SANCXX ${SAN:+(ASan+UBSan)}"

RAN=""; SKIPPED=""
want() { [ -z "${ONLY:-}" ] || case " $ONLY " in *" $1 "*) return 0;; *) return 1;; esac; }
# stage <id> <title> <compiler> <flags and source...>: build into $OUT, run, record.
stage() {
  local id="$1" title="$2" cxx="$3"; shift 3
  want "$id" || return 0
  STAGE="$id"; echo "[$id] $title"
  "$cxx" "$@" -o "$OUT/stage_$id"       # two lines on purpose: `a && b` would hide a failed build from set -e
  "$OUT/stage_$id"
  RAN="$RAN $id"
}

# The six shared files (contract_core.h, its tests, the WDP announce core and its test) are copied
# from the RSeries fork, never edited here. src/contract/CORE_PIN names the RSeries commit they came
# from, and the list is that commit's test/host/core-files.txt. With an RSeries checkout that holds
# the commit next to this one (or at $RSERIES_DIR), every file is compared; CI always has it
# (REQUIRE_CORE_PIN=1 turns "no checkout" into a failure).
if want 0; then
  STAGE="0"; echo "[0] the shared core files match CORE_PIN"
  PINNED="$(tr -d ' \r\n' < ../../src/contract/CORE_PIN)"
  RS="${RSERIES_DIR:-../../../DroidNet-RSeriesLogics}"
  if git -C "$RS" cat-file -e "$PINNED^{commit}" 2>/dev/null; then
    for f in $(git -C "$RS" show "$PINNED:test/host/core-files.txt"); do
      git -C "$RS" show "$PINNED:$f" | cmp -s - "../../$f" || { echo "FAIL: $f is not RSeries $PINNED's. The shared files are edited in the RSeries fork only (docs/CHANGING_THE_CORE.md there)."; exit 1; }
    done
    echo "OK  every shared file is byte-identical to RSeries $PINNED"
    RAN="$RAN 0"
  elif [ "${REQUIRE_CORE_PIN:-0}" = 1 ]; then
    echo "FAIL: REQUIRE_CORE_PIN=1 and no RSeries checkout holding $PINNED was found at $RS"; exit 1
  else
    echo "SKIP: no RSeries checkout holding $PINNED at $RS, so the shared files were not compared."
    SKIPPED="$SKIPPED 0(no-RSeries-checkout)"
  fi
fi
stage 1  "contract_core parser unit tests" "$CXX" -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp
# Shared with the RSeries and PSI forks, like stage 1. -std=c++11: avr-gcc builds the core as gnu++11 here.
stage 1f "contract_core v1.3 unit tests" "$CXX" -std=c++11 -Wall -Wextra -O0 test_contract_v13.cpp
stage 1g "contract_core v1.3 unit tests, flash-string hooks on" "$CXX" -std=c++11 -Wall -Wextra -O0 -DCONTRACT_TEST_FLASH_HOOKS test_contract_v13.cpp
# The core and everything this fork runs per frame are integer only (core revision CA1.3: the beat
# clock and the envelope without float). On an ATmega a float in the render path costs flash, time
# and a second set of rounding rules beside the vector file's.
if want 1h; then
STAGE="1h"; echo "[1h] no float in the core or the layer"
for f in ../../src/contract/contract_core.h ../../src/contract/ContractFlthy.h ../../src/droidnet/dn_strip.h ../../src/droidnet/native_ext.h; do
  if sed 's://.*::' "$f" | grep -nE '\b(float|double)\b|[0-9]\.[0-9]+f\b|<math\.h>|<cmath>|\b(floorf?|ceilf?|roundf?|fabsf?|sqrtf?|powf?)[[:space:]]*\('; then
    echo "FAIL: $f uses floating point (the lines above). The core and the per-frame path are integer only."; exit 1
  fi
done
echo "OK  contract_core.h, ContractFlthy.h, dn_strip.h and native_ext.h are integer only"
RAN="$RAN 1h"
fi
# Shared byte-identically with the RSeries and PSI forks (src/wdp/wdp_announce_core.h and this test).
# -std=c++11: avr-gcc builds the same header as gnu++11 here.
stage 1b "wdp_announce_core WDP device announce unit tests" "$CXX" -std=c++11 -Wall -Wextra -Werror -O0 test_wdp_announce_core.cpp
# Fork-local. Twice: with the longest fw scripts/wdp_fw.py makes (a 7-character commit + "-dirty",
# 13 characters, which must still fit WDP_LINE_MAX), and with no WDP_FW at all (a build without the
# script, e.g. the Arduino IDE), which must announce "unknown".
if want 1c; then
  STAGE="1c"; echo "[1c] wdp_fork.h announce adapter + this fork's identity"
  # -Wno-unused-variable: the test includes mock_flthy.h for Serial and never names the mock's board
  # globals, which g++ (not clang++) reports for a header's static variables.
  "$CXX" -std=c++11 -Wall -Wextra -Werror -Wno-unused-variable -O0 -DWDP_FW='"abcdef0-dirty"' -DWDP_FW_EXPECT='"abcdef0-dirty"' \
         test_wdp_fork.cpp -o "$OUT/stage_1c"
  "$OUT/stage_1c"
  "$CXX" -std=c++11 -Wall -Wextra -Werror -Wno-unused-variable -O0 -DWDP_FW_EXPECT='"unknown"' test_wdp_fork.cpp -o "$OUT/stage_1c"
  "$OUT/stage_1c"
  RAN="$RAN 1c"
fi
stage 2  "ContractFlthy.h firmware type-check" "$CXX" -std=c++17 -Wall -Wextra compile_contract.cpp
# The firmware itself, not a mock of it: sim/ compiles the real src/main.cpp against a fake Arduino
# core and runs every scenario in its own process. A scenario named in sim/xfail.txt records a known
# defect and must FAIL; it leaves that file only when the change that fixes it lands.
if want 2b; then
  STAGE="2b"; echo "[2b] whole-firmware simulation (real main.cpp, fake Arduino core)"
  CXX="$CXX" ./sim/run_sim.sh
  RAN="$RAN 2b"
fi

if want 3; then
STAGE="3"; echo "[3] static source guards"
# --- LED-ONLY INVARIANT (README §11) -------------------------------------------------
# The '!' contract layer must never move a holoprojector SERVO. compile_contract.cpp has a
# runtime tripwire for this, but that can only catch a servo touch on a path the guard
# actually exercises. This is the complementary STATIC check: the contract layer must not
# even NAME the servo command path. (Comments are stripped first — the header documents the
# invariant in prose. enableTwitchHP / gSavedTwitchHP are a different, capital-T symbol: the
# show/idle interlock legitimately DISABLES the native auto-twitch, which is precisely what
# keeps the servo still.)
if sed 's://.*::' ../../src/contract/ContractFlthy.h \
     | grep -nE '(HP_command|positionHP|twitchHP|wagHP|RCHP|flushCommandArray)'; then
  echo "FAIL: ContractFlthy.h names the HP SERVO command path (see the lines above)."
  echo "      This fork is LED-EFFECTS-ONLY: the contract must never move a holoprojector."
  exit 1
fi

# --- LED-ONLY INVARIANT, native brightness (H<unit>B<level>) ----------------------------
# The fork's one native command is LED-only too: its parser (src/droidnet/native_ext.h) and its
# handler in src/main.cpp (between the "DroidNet native brightness BEGIN" and "... END" markers)
# must not name the servo command path either. src/main.cpp is CRLF, and the comment strip would
# also delete the markers, so the region is cut out FIRST (CRs removed), and the check fails
# CLOSED: exactly one BEGIN and one END marker, and the H branch itself inside the region.
# Only then are comments stripped and the banned names grepped.
NATIVE_BEGINS=$(tr -d '\r' < ../../src/main.cpp | grep -c 'DroidNet native brightness BEGIN' || true)
NATIVE_ENDS=$(tr -d '\r' < ../../src/main.cpp | grep -c 'DroidNet native brightness END' || true)
if [ "$NATIVE_BEGINS" != "1" ] || [ "$NATIVE_ENDS" != "1" ]; then
  echo "FAIL: src/main.cpp must hold exactly one 'DroidNet native brightness BEGIN' marker and one"
  echo "      '... END' marker (found $NATIVE_BEGINS and $NATIVE_ENDS), or this guard would check nothing."
  exit 1
fi
NATIVE_REGION=$(tr -d '\r' < ../../src/main.cpp \
  | awk '/DroidNet native brightness BEGIN/,/DroidNet native brightness END/' \
  | sed 's://.*::')
if [ -z "$NATIVE_REGION" ] || ! printf '%s\n' "$NATIVE_REGION" | grep -qF "inputBuffer[0]=='H'"; then
  echo "FAIL: the marked native brightness region in src/main.cpp is empty or holds no"
  echo "      inputBuffer[0]=='H' branch, so this guard would check nothing."
  exit 1
fi
if printf '%s\n' "$NATIVE_REGION" | grep -nE '(HP_command|positionHP|twitchHP|wagHP|RCHP|flushCommandArray)'; then
  echo "FAIL: the native brightness branch in src/main.cpp names the HP SERVO command path"
  echo "      (see the lines above, numbered from the BEGIN marker). Brightness is LED-only."
  exit 1
fi
if sed 's://.*::' ../../src/droidnet/native_ext.h \
     | grep -nE '(HP_command|positionHP|twitchHP|wagHP|RCHP|flushCommandArray)'; then
  echo "FAIL: src/droidnet/native_ext.h names the HP SERVO command path (see the lines above)."
  echo "      The native brightness parser is LED-only and must stay pure."
  exit 1
fi

# --- v1.2 ACCENT ALLOW-GATE ON VERB P ------------------------------------------------
# The scored accent is gated at PARSE time (ae= refuses to store a rejected effect). Verb P is
# the other door: it hands the wire's i= straight to the overlay. BOTH the CV_PULSE call site
# and _fireAccent() must allow-gate it, exactly as the Logics and the PSI do.
# compile_contract.cpp's A6 guard proves the BEHAVIOUR, and its white-box leg catches the gate
# inside _fireAccent() going missing — but the two gates are redundant BY DESIGN, so no runtime
# test can see the CALL-SITE one deleted on its own (the inner gate still returns CE_SOLID and
# the board still behaves). That is exactly how it rotted away unnoticed before. Pin it here.
if ! sed 's://.*::' ../../src/contract/ContractFlthy.h \
     | awk '/case CV_PULSE:/,/break;/' | grep -q 'accentEffectAllowed'; then
  echo "FAIL: ContractFlthy.h's CV_PULSE handler does not allow-gate the wire's i= with"
  echo "      accentEffectAllowed(). A stateful i= (scan/sparkle/meter) would reach the overlay"
  echo "      and corrupt the base look's shared frame counters; a native i= would hand the"
  echo "      render slot to a renderer contractRenderHP never dispatches, so the accent could"
  echo "      NEVER EXPIRE and the jewel would LATCH. Gate at the call site AND in _fireAccent."
  exit 1
fi

# --- WIRE BUDGET ---------------------------------------------------------------------
# main.cpp's serial line buffer truncates SILENTLY. compile_contract.cpp pins the longest v1.2
# line against the MOCK's INPUTBUFFERLEN; this pins the mock against the FIRMWARE's, so the
# two cannot drift apart and quietly render that guard meaningless.
# (src/main.cpp is CRLF — upstream's line endings, left alone — so strip the CR before comparing.)
FW_BUF=$(grep -E '^#define[[:space:]]+INPUTBUFFERLEN[[:space:]]' ../../src/main.cpp | tr -d '\r' | awk '{print $3}')
MOCK_BUF=$(grep -E '^#define[[:space:]]+INPUTBUFFERLEN[[:space:]]' mock_flthy.h | tr -d '\r' | awk '{print $3}')
if [ "$FW_BUF" != "$MOCK_BUF" ]; then
  echo "FAIL: INPUTBUFFERLEN drifted — src/main.cpp says $FW_BUF, test/host/mock_flthy.h says $MOCK_BUF."
  echo "      The wire-budget guard only means something if the mock matches the firmware."
  exit 1
fi

# --- ONE inputString ------------------------------------------------------------------
# loop() used to end its command block with `String inputString = "";`, a LOCAL that shadowed the
# global line buffer: it reset nothing and allocated one byte per command. It is gone; this keeps
# it from coming back.
if [ "$(tr -d '\r' < ../../src/main.cpp | grep -c '^[[:space:]]*String inputString')" != "1" ]; then
  echo "FAIL: src/main.cpp must declare 'String inputString' exactly once (a second one inside loop()"
  echo "      shadows the serial line buffer)."
  exit 1
fi

# --- WDP: NO TEXT INSIDE AN ANNOUNCE ---------------------------------------------------
# The WDP announce leaves Serial in pieces over a few loop() passes (src/wdp/wdp_fork.h), so every
# other write to Serial once loop() runs must call wdpFinish() first, or be held back by a
# wdpInFlight() test -- on the same line or the code line above -- or its text can land in the middle
# of an announce and the WCB drops both. compile_contract.cpp and the simulation prove it for the
# verb-Q reply; this finds every writer. In src/main.cpp it
# checks from `void loop()` down (setup() writes before the first announce can start), skipping
# what a build without DEBUG leaves out (#ifdef DEBUG, which is commented out). It fails closed:
# each file must hold at least one checked write, or the scan has gone blind.
wdp_writers() {   # <file> <start-regex>: prints "checked=<n>", then each unguarded write; exit 1 if any
  tr -d '\r' < "$1" | awk -v start="$2" '
    BEGIN { on = (start == ""); depth = 0; n = 0; bad = 0; prev = "" }
    !on && $0 ~ start { on = 1 }
    {
      code = $0; sub(/\/\/.*/, "", code)
      if (code ~ /^[ \t]*#[ \t]*if/) {
        depth++
        dbg[depth] = (code ~ /^[ \t]*#[ \t]*ifdef[ \t]+DEBUG[ \t]*$/) ? 1 : ((code ~ /^[ \t]*#[ \t]*ifndef[ \t]+DEBUG[ \t]*$/) ? -1 : 0)
        next
      }
      if (code ~ /^[ \t]*#[ \t]*el/)    { dbg[depth] = -dbg[depth]; next }
      if (code ~ /^[ \t]*#[ \t]*endif/) { depth--; next }
      indebug = 0
      for (i = 1; i <= depth; i++) if (dbg[i] == 1) indebug = 1
      if (on && !indebug && code ~ /Serial[ \t]*\.[ \t]*(print|println|write)[ \t]*\(/) {
        n++
        if (code !~ /wdpFinish\(\)|wdpInFlight\(\)/ && prev !~ /wdpFinish\(\)|wdpInFlight\(\)/) { print "  " NR ": " $0; bad = 1 }
      }
      if (code ~ /[^ \t]/) prev = code
    }
    END { print "checked=" n; exit bad }'
}
for spec in "../../src/main.cpp|^void loop[(]" "../../src/contract/ContractFlthy.h|"; do
  f="${spec%%|*}"; start="${spec#*|}"
  if ! out=$(wdp_writers "$f" "$start"); then
    printf '%s\n' "$out" | grep -v '^checked='
    echo "FAIL: $f writes to Serial (lines above) without calling wdpFinish() or testing wdpInFlight()"
    echo "      first, so that text can land inside a WDP announce in flight. Do it on that line or the one above."
    exit 1
  fi
  if [ "$(printf '%s\n' "$out" | sed -n 's/^checked=//p')" = "0" ]; then
    echo "FAIL: the WDP writer scan found no Serial write at all in $f, so it checked nothing."
    exit 1
  fi
done
echo "LED-only invariant (contract + native brightness) + wire budget (INPUTBUFFERLEN=$FW_BUF) + WDP writers OK"
RAN="$RAN 3"
fi

# The parsers are the one place this project eats bytes off a shared, noisy serial bus, and to
# save 1,008 B of flash on the PSI they were hand-rolled instead of using strtol/strtoul. This
# stage is the differential that keeps them honest: ~1.55M checks against an INDEPENDENT model
# and a true 32-BIT strtol/strtoul oracle (the host's own is 64-bit and would disagree on
# overflow -- which is the whole trap), plus hostile/whole-byte-range lines through
# contractParse() under AddressSanitizer + UndefinedBehaviorSanitizer.
# Fixed seed: deterministic and reproducible, not a slot machine.
stage 4  "parser fuzz + differential (deterministic)" "$SANCXX" -std=c++17 -O1 $SAN fuzz_parsers.cpp
# The fork's one native command. nativeExtParse() is pure (no Arduino), so its whole accept/
# reject table runs here. -Wconversion -Werror because the same code runs with a 16-bit int
# on the ATmega2560, where a silent narrowing is a real bug.
stage 5  "native_ext parser unit tests (H<unit>B<level> brightness)" "$CXX" -std=c++17 -Wall -Wextra -Wconversion -Werror -O0 test_native_ext.cpp
# Fork-local: every line through nativeExtParse() with piped = false and true, against an
# independent model, each in a heap buffer of exactly strlen()+1 bytes. Fixed seed.
# (test/host/fuzz_parsers.cpp is shared byte-identically across three forks; it is not touched.)
# -Wno-comment: g++ warns about a backslash at the end of a // comment in that file's header.
stage 6  "native_ext fuzz + differential (deterministic)" "$SANCXX" -std=c++17 -Wall -Wextra -Wno-comment -O1 $SAN fuzz_native_ext.cpp

if want 7; then
STAGE="7"; echo "[7] REAL cross-compile (ATmega2560 (Arduino Mega ADK)) -- optional"
# THIS is the stage that makes "it compiles" a claim about the FIRMWARE rather than a claim
# about our mock. Stages 1-3 are host checks: they prove the contract logic and they pin the
# board API against a HAND-WRITTEN MOCK -- and a mock is only ever as honest as its author.
# This one was not honest. The first real avr-gcc/xtensa build caught two things stages 1-3
# could never see, by construction:
#   * FastLED declares RGB as an ENUMERATOR of fl::EOrder, which name-hides a `struct RGB`.
#     Our shared core defined exactly that struct. Every plain use of the type failed to
#     compile on both FastLED boards. The mocks never modelled EOrder, so the type-check
#     passed happily.
#   * fill_column() is DEFINED in the PSI firmware but declared in no header, and our include
#     point sits above the definition. The mock DEFINED it, so the name was always in scope.
# (Both are fixed. The lesson is why this stage exists.)
#
# OPTIONAL because it needs PlatformIO and a ~200 MB toolchain. Skipped cleanly without it:
# stages 1-3 still run and still mean something -- just strictly less than they appear to.
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
[ -x "$PIO" ] || PIO="$(command -v pio 2>/dev/null || true)"
if [ -n "$PIO" ] && [ -x "$PIO" ]; then
  echo "using $PIO"
  ( cd ../.. && "$PIO" run -e FlthyHPs )
  ( cd ../.. && "$PIO" run -e FlthyHPs_probe ) > "$OUT/probe.log" 2>&1 || { cat "$OUT/probe.log"; echo "FAIL: the bench build (FlthyHPs_probe, -DDN_LOOP_PROBE) does not build"; exit 1; }
  echo "the loop-pass probe build (FlthyHPs_probe) links too"
  echo "cross-compiles for the real MCU (ATmega2560 (Arduino Mega ADK)) OK"
  echo "    (a successful LINK is not a bench test: this firmware has never been flashed.)"
  RAN="$RAN 7"
elif [ "${REQUIRE_PIO:-0}" = 1 ]; then
  echo "FAIL: REQUIRE_PIO=1 and PlatformIO was not found (pip install platformio, or PIO=/path/to/pio)"
  exit 1
else
  echo "SKIP: PlatformIO not found, so NOTHING here was compiled for the real MCU."
  echo "      Stages 1-6 passed, but only stage 2b runs the firmware's own code, and on a host."
  echo "      Install it and re-run for the real check:  pip install platformio"
  echo "      Or point this at an existing install:      PIO=/path/to/pio $0"
  SKIPPED="$SKIPPED 7(no-PlatformIO)"
fi
fi

echo
echo "stages run:$RAN"
echo "stages skipped:${SKIPPED:- none}"
if [ -n "$SKIPPED" ]; then
  echo "NOTE: a skipped stage proved nothing. Stage 7 is the only real cross-compile; stage 0 is the"
  echo "      only check that the shared core files were not edited here."
fi
