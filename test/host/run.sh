#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
# SPDX-License-Identifier: MIT
#
# Host checks for the Flthy HPs contract fork (stages 1-3 need no hardware and no toolchain; stage 4 is the real cross-compile):
#   1. contract_core.h parser unit tests
#   2. ContractFlthy.h firmware-layer type-check against a mock of the board API
#   3. static source guards (the LED-only invariant + the serial wire budget)
#   4. REAL cross-compile for the ATmega2560 (optional; needs PlatformIO)
set -e
cd "$(dirname "$0")"
echo "[1/5] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/flthy_contract_test
/tmp/flthy_contract_test
echo "[2/5] ContractFlthy.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/flthy_fw_syntax
/tmp/flthy_fw_syntax

echo "[3/5] static source guards"
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
echo "LED-only invariant + wire budget (INPUTBUFFERLEN=$FW_BUF) OK"

echo "[4/5] parser fuzz + differential (ASan/UBSan, deterministic)"
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

echo "[5/5] REAL cross-compile (ATmega2560 (Arduino Mega ADK)) -- optional"
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

