#!/usr/bin/env bash
# Apply a prototype level to a scratch copy of ../../../../src and run the simulation on it.
#   ./try.sh "<main.cpp levels>" "<ContractFlthy.h levels>"      e.g.  ./try.sh 12AD AGBCDE   (everything)
# main.cpp levels:  1 serialEvent + 128-byte line (F1, section 11)   2 I2C buffer + shadowed locals
#                   A the two twitch gates   D owner-brightness hook
# ContractFlthy.h:  A lifecycle (L1; F2 F4 F7)   G letting go   B native allow-list (S7; F3)
#                   C pulse over the stock look (L3)   D brightness (S6; F8)   E pulse floor (S8), d= retired (L4)
# B, C, D, E and G need A. A or D in one file needs the same level in the other.
set -eu
cd "$(dirname "$0")"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/flthy-r1.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
cp -r ../../../../src "$OUT/src"
[ -z "${1:-}" ] || python3 patch_main.py "$OUT/src/main.cpp" "$1"
[ -z "${2:-}" ] || python3 patch_cf.py "$OUT/src/contract/ContractFlthy.h" "$2"
DEFS=""; case "${1:-}" in *A*) DEFS='-DSIM_HELD(i)=contractHolds(i)' ;; esac
# main level 1 goes with -DSERIAL_RX_BUFFER_SIZE=256 in platformio.ini (plan Task 5): a 255-byte ring
case "${1:-}" in *1*) export SIM_RX_RING=255 ;; esac
cp ../sim-prototype/xfail.txt "$OUT/xfail.all"
( cd ../sim-prototype && : > "$OUT/empty" && SIM_XFAIL="$OUT/empty" SIM_DEFS="$DEFS" ./run_sim.sh "$OUT/src" ) || true
