#!/usr/bin/env bash
# Build the 2026-11-15 review probe (harness.cpp) against the firmware in ../../../src.
# Prints the path of the binary. Needs g++ only. Nothing is written inside the repo.
#   ./build.sh [SRC_DIR] [OUT_DIR]
set -eu
cd "$(dirname "$0")"
SRC="${1:-../../../src}"
OUT="${2:-$(mktemp -d "${TMPDIR:-/tmp}/flthy-probe.XXXXXX")}"
mkdir -p "$OUT/src"
cp -r "$SRC"/. "$OUT/src/"
# The one substitution: stock dimColorVal() divides by zero once per dim-pulse cycle
# (`255/brightness`). avr-libc returns -1 for x/0 and carries on; x86 traps (SIGFPE).
sed -i.bak 's#\([0-9][0-9]*\)/brightness#AVRDIV(\1,brightness)#g' "$OUT/src/main.cpp"
grep -q 'AVRDIV(255,brightness)' "$OUT/src/main.cpp" || { echo "the x/0 substitution matched nothing" >&2; exit 1; }
"${CXX:-g++}" -std=c++17 -O1 -g -w -I fake -I ../../../include -I "$OUT/src" -I . harness.cpp -o "$OUT/harness"
echo "$OUT/harness"
