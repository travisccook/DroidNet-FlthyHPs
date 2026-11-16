#!/usr/bin/env bash
# Run every scenario of the review probe and print what it prints. One process per scenario
# (the firmware's globals cannot be reset). The probe asserts nothing: read the RESULT lines.
set -u
cd "$(dirname "$0")"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/flthy-probe.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
H="$(./build.sh ../../../src "$OUT")" || exit 1
for s in "idle 0" "idle 1" idlejerk xnoshow stalehalt showkeepsnative opoff hostdead dur \
         bright "native 3" native3b "native 2" "native 5" nativeaccent scorenativeaccent sc7 \
         showrate "pulsehz 255" "pulsehz 128" "driftaccent 0" "driftaccent 1" replay2; do
  echo "== $s"; "$H" $s
done
echo "== concat: loop-pass us : trials | all 3 looks applied | show entered | Stop's idle honoured"
for pass in 300 700 1000 1500 2000 3000 4000 6000; do
  n=0; looks=0; show=0; idle=0
  for ph in $(seq 0 97 $((pass - 1)) | head -40); do
    "$H" concat "$pass" "$ph"; rc=$?; n=$((n + 1))
    [ $((rc & 3)) -eq 3 ] && looks=$((looks + 1))
    [ $((rc & 4)) -ne 0 ] && show=$((show + 1))
    [ $((rc & 8)) -ne 0 ] && idle=$((idle + 1))
  done
  echo "$pass : $n | $looks | $show | $idle"
done
