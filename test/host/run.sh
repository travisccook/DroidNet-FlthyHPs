#!/usr/bin/env bash
# Host checks for the Flthy HPs contract fork (no hardware / no AVR toolchain needed):
#   1. contract_core.h parser unit tests
#   2. ContractFlthy.h firmware-layer type-check against a mock of the board API
set -e
cd "$(dirname "$0")"
echo "[1/2] contract_core parser unit tests"
clang++ -std=c++17 -Wall -Wextra -O0 test_contract_core.cpp -o /tmp/flthy_contract_test
/tmp/flthy_contract_test
echo "[2/2] ContractFlthy.h firmware type-check"
clang++ -std=c++17 -Wall -Wextra compile_contract.cpp -o /tmp/flthy_fw_syntax
/tmp/flthy_fw_syntax
