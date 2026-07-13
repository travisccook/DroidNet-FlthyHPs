#!/usr/bin/env bash
# Part of the DroidNet Driveable-Animation Contract test harness — an additive layer
# bolted onto Ryan Sondgeroth's FlthyHPs firmware. Copyright (c) 2026 Travis Cook.
# SPDX-License-Identifier: MIT (see LICENSE-DroidNet-Contract)
# The firmware this harness checks is Ryan Sondgeroth's work; it carries no license and
# is all rights reserved by him, and that MIT license covers our files only. See README.
#
# Host checks for the Flthy HPs contract fork (no hardware / no AVR toolchain needed):
#   1. contract_core.h parser unit tests
#   2. ContractFlthy.h firmware-layer type-check against a mock of the board API
#   3. static source guards (the LED-only invariant + the serial wire budget)
set -e
cd "$(dirname "$0")"
echo "[1/3] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/flthy_contract_test
/tmp/flthy_contract_test
echo "[2/3] ContractFlthy.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/flthy_fw_syntax
/tmp/flthy_fw_syntax

echo "[3/3] static source guards"
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

