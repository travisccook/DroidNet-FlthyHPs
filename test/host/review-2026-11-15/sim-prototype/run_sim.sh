#!/usr/bin/env bash
# Build the whole-firmware simulation and run every scenario, one process each.
#   run_sim.sh [SRC_DIR]     SRC_DIR defaults to ../../../../src (the firmware under test)
# Scenarios named in xfail.txt (or $SIM_XFAIL) must FAIL (they record a known defect); all others must pass.
set -u
cd "$(dirname "$0")"
SRC="${1:-../../../../src}"
INC="${SIM_INCLUDE:-../../../../include}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/flthy-sim.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
# The one substitution: stock dimColorVal() divides by zero once per dim-pulse cycle
# (main.cpp, `255/brightness`). avr-libc returns -1 for x/0 and carries on; x86 traps.
mkdir -p "$OUT/src" && cp -r "$SRC"/. "$OUT/src/"
sed -i.bak 's#\([0-9][0-9]*\)/brightness#AVRDIV(\1,brightness)#g' "$OUT/src/main.cpp"
grep -q 'AVRDIV(255,brightness)' "$OUT/src/main.cpp" || { echo "FAIL: the x/0 substitution matched nothing in main.cpp"; exit 1; }
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O1 -g -w ${SIM_DEFS:-} -I "${SIM_FAKE:-../fake}" -I "$INC" -I "$OUT/src" -I . sim_main.cpp -o "$OUT/sim" || { echo "FAIL: the simulation did not build"; exit 1; }
pass=0; fail=0; xfail=0; bad=""
for s in $("$OUT/sim" --list); do
  if "$OUT/sim" "$s" > "$OUT/log" 2>&1; then rc=0; else rc=1; fi
  if grep -qx "$s" "${SIM_XFAIL:-xfail.txt}" 2>/dev/null; then
    if [ $rc -eq 0 ]; then echo "UNEXPECTED PASS  $s  (remove it from xfail.txt)"; fail=$((fail+1)); bad="$bad $s"
    else echo "xfail            $s"; xfail=$((xfail+1)); fi
  elif [ $rc -eq 0 ]; then echo "ok               $s"; pass=$((pass+1))
  else echo "FAIL             $s"; sed 's/^/    /' "$OUT/log"; fail=$((fail+1)); bad="$bad $s"; fi
done
echo "simulation: $pass passed, $xfail expected failures, $fail failed"
[ $fail -eq 0 ] || { echo "failed:$bad"; exit 1; }
