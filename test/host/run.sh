#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
# SPDX-License-Identifier: MIT
#
# Host checks for the Flthy HPs fork (stages 1-6 need no hardware and no toolchain; stage 7 is the real cross-compile):
#   1. contract_core.h parser unit tests
#      1b. wdp_announce_core.h (WDP device announce) unit tests, shared with the other two forks
#      1c. wdp_fork.h, this fork's announce adapter and identity
#   2. ContractFlthy.h firmware-layer type-check against a mock of the board API
#   3. static source guards (the LED-only invariant, the serial wire budget, and no Serial write
#      that could land inside a WDP announce)
#   4. contract parser fuzz + differential (ASan/UBSan)
#   5. native_ext.h (H<unit>B<level> brightness) parser unit tests
#   6. native_ext.h fuzz + differential (ASan/UBSan; fork-local, not the shared fuzz_parsers.cpp)
#   7. REAL cross-compile for the ATmega2560 (optional; needs PlatformIO)
set -e
cd "$(dirname "$0")"
echo "[1/7] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/flthy_contract_test
/tmp/flthy_contract_test
echo "[1b/7] wdp_announce_core WDP device announce unit tests"
# Shared byte-identically with the RSeries and PSI forks (src/wdp/wdp_announce_core.h and this test).
# -std=c++11: avr-gcc builds the same header as gnu++11 here.
clang++ -std=c++11 -Wall -Wextra -Werror -O0 test_wdp_announce_core.cpp -o /tmp/flthy_wdp_test
/tmp/flthy_wdp_test
echo "[1c/7] wdp_fork.h announce adapter + this fork's identity"
# Fork-local. Twice: with the longest fw scripts/wdp_fw.py makes (a 7-character commit + "-dirty",
# 13 characters, which must still fit WDP_LINE_MAX), and with no WDP_FW at all (a build without the
# script, e.g. the Arduino IDE), which must announce "unknown".
clang++ -std=c++11 -Wall -Wextra -Werror -O0 -DWDP_FW='"abcdef0-dirty"' -DWDP_FW_EXPECT='"abcdef0-dirty"' \
        test_wdp_fork.cpp -o /tmp/flthy_wdp_fork_test
/tmp/flthy_wdp_fork_test
clang++ -std=c++11 -Wall -Wextra -Werror -O0 -DWDP_FW_EXPECT='"unknown"' \
        test_wdp_fork.cpp -o /tmp/flthy_wdp_fork_test
/tmp/flthy_wdp_fork_test
echo "[2/7] ContractFlthy.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/flthy_fw_syntax
/tmp/flthy_fw_syntax

echo "[3/7] static source guards"
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

# --- WDP: NO TEXT INSIDE AN ANNOUNCE ---------------------------------------------------
# The WDP announce leaves Serial in pieces over a few loop() passes (src/wdp/wdp_fork.h), so every
# other write to Serial once loop() runs must call wdpFinish() first -- on the same line or the
# code line above -- or its text can land in the middle of an announce and the WCB drops both.
# compile_contract.cpp proves it for the verb-Q ack; this finds every writer. In src/main.cpp it
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
        if (code !~ /wdpFinish\(\)/ && prev !~ /wdpFinish\(\)/) { print "  " NR ": " $0; bad = 1 }
      }
      if (code ~ /[^ \t]/) prev = code
    }
    END { print "checked=" n; exit bad }'
}
for spec in "../../src/main.cpp|^void loop[(]" "../../src/contract/ContractFlthy.h|"; do
  f="${spec%%|*}"; start="${spec#*|}"
  if ! out=$(wdp_writers "$f" "$start"); then
    printf '%s\n' "$out" | grep -v '^checked='
    echo "FAIL: $f writes to Serial (lines above) without calling wdpFinish() first, so that text"
    echo "      can land inside a WDP announce in flight. Call wdpFinish() on that line or the one above."
    exit 1
  fi
  if [ "$(printf '%s\n' "$out" | sed -n 's/^checked=//p')" = "0" ]; then
    echo "FAIL: the WDP writer scan found no Serial write at all in $f, so it checked nothing."
    exit 1
  fi
done
echo "LED-only invariant (contract + native brightness) + wire budget (INPUTBUFFERLEN=$FW_BUF) + WDP writers OK"

echo "[4/7] parser fuzz + differential (ASan/UBSan, deterministic)"
# The parsers are the one place this project eats bytes off a shared, noisy serial bus, and to
# save 1,008 B of flash on the PSI they were hand-rolled instead of using strtol/strtoul. This
# stage is the differential that keeps them honest: ~1.55M checks against an INDEPENDENT model
# and a true 32-BIT strtol/strtoul oracle (the host's own is 64-bit and would disagree on
# overflow -- which is the whole trap), plus hostile/whole-byte-range lines through
# contractParse() under AddressSanitizer + UndefinedBehaviorSanitizer.
# Fixed seed: deterministic and reproducible, not a slot machine.
clang++ -std=c++17 -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
        fuzz_parsers.cpp -o /tmp/flthyhps_fuzz
/tmp/flthyhps_fuzz

echo "[5/7] native_ext parser unit tests (H<unit>B<level> brightness)"
# The fork's one native command. nativeExtParse() is pure (no Arduino), so its whole accept/
# reject table runs here. -Wconversion -Werror because the same code runs with a 16-bit int
# on the ATmega2560, where a silent narrowing is a real bug.
clang++ -std=c++17 -Wall -Wextra -Wconversion -Werror -O0 test_native_ext.cpp -o /tmp/flthy_native_ext_test
/tmp/flthy_native_ext_test

echo "[6/7] native_ext fuzz + differential (ASan/UBSan, deterministic)"
# Fork-local: every line through nativeExtParse() with piped = false and true, against an
# independent model, each in a heap buffer of exactly strlen()+1 bytes. Fixed seed.
# (test/host/fuzz_parsers.cpp is shared byte-identically across three forks; it is not touched.)
clang++ -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
        fuzz_native_ext.cpp -o /tmp/flthy_native_ext_fuzz
/tmp/flthy_native_ext_fuzz

echo "[7/7] REAL cross-compile (ATmega2560 (Arduino Mega ADK)) -- optional"
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
if [ -z "$PIO" ] || [ ! -x "$PIO" ]; then
  echo "SKIP: PlatformIO not found, so NOTHING here was compiled for the real MCU."
  echo "      Stages 1-3 passed, but they only type-check against our mock."
  echo "      Install it and re-run for the real check:  pip install platformio"
  echo "      Or point this at an existing install:      PIO=/path/to/pio $0"
  exit 0
fi
echo "using $PIO"
( cd ../.. && "$PIO" run -e FlthyHPs )
echo "cross-compiles for the real MCU (ATmega2560 (Arduino Mega ADK)) OK"
echo "    (a successful LINK is not a bench test: this firmware has never been flashed.)"

