#!/usr/bin/env bash
# Apply a prototype level to a scratch copy of ../../../../src and run the simulation on it.
#   ./try.sh "<main.cpp levels>" "<ContractFlthy.h levels>"      e.g.  ./try.sh 12AD AGBCDE   (everything)
# main.cpp levels:  1 serialEvent (F1)   2 I2C buffer + shadowed locals   A twitch gates   D owner-brightness hook
# ContractFlthy.h:  A lifecycle (F2 F4 F7)   G letting go   B native allow-list (F3)   C pulse over the stock look (L3)
#                   D brightness (S6 F8)   E pulse floor and d= (F9)
# B, C, D, E and G need A. D in one file needs D in the other. A in one file needs A in the other.
set -eu
cd "$(dirname "$0")"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/flthy-r1.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
cp -r ../../../../src "$OUT/src"
[ -z "${1:-}" ] || python3 patch_main.py "$OUT/src/main.cpp" "$1"
[ -z "${2:-}" ] || python3 patch_cf.py "$OUT/src/contract/ContractFlthy.h" "$2"
DEFS=""; case "${1:-}" in *A*) DEFS='-DSIM_HELD(i)=contractHolds(i)' ;; esac
cp ../sim-prototype/xfail.txt "$OUT/xfail.all"
( cd ../sim-prototype && : > "$OUT/empty" && SIM_XFAIL="$OUT/empty" SIM_DEFS="$DEFS" ./run_sim.sh "$OUT/src" ) || true
