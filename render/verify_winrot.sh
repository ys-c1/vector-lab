#!/bin/bash
# 四组窗口旋转反解：每组报角度、空角像素、耗时；超过 45 度拒绝；
# 退化包围盒单独报；零旋转与原管线逐字节比对。
set -u
cd "$(dirname "$0")"
BIN=build/winrot_cli
ZERO=build/cli_zero
OUT=winrot-out
mkdir -p "$OUT"

make -s build/winrot_cli build/cli build/cli_zero build/test_winrot >/dev/null || exit 1
./build/test_winrot >/dev/null || { echo "unit tests failed"; exit 1; }

fail=0
row() { printf '%-6s %-26s %10s %8s %8s %10s %10s\n' "$@"; }
row group scene angle_deg corner_px empty_px total_ms result
echo "-------------------------------------------------------------------------------"

run_group() {
  local name=$1 scene=$2 ww=$3 wh=$4 px=$5 py=$6
  local out="$OUT/$name.pgm"
  local log rc
  log=$($BIN run "$scene" "$ww" "$wh" "$out" "$px" "$py" 4 45 2>&1); rc=$?
  local angle corners empt ms res
  angle=$(printf '%s\n' "$log" | sed -n 's/.*angle_deg \([0-9.]*\).*/\1/p')
  corners=$(printf '%s\n' "$log" | sed -n 's/.*corner_empty_px \([0-9-]*\).*/\1/p')
  empt=$(printf '%s\n' "$log" | sed -n 's/.*empty_px \([0-9]*\)\/.*/\1/p')
  ms=$(printf '%s\n' "$log" | sed -n 's/.*total_ms \([0-9.]*\).*/\1/p')
  if [ "$rc" -eq 0 ] && [ "${corners:-1}" -eq 0 ]; then res=ok; else res="FAIL rc=$rc"; fail=1; fi
  row "$name" "$(basename "$scene")" "${angle:--}" "${corners:--}" "${empt:--}" "${ms:--}" "$res"
  if [ "$rc" -ne 0 ]; then printf '%s\n' "$log" | sed 's/^/       /' >&2; fi
}

run_group g1 tests/scene_g1_rect.txt  200 120 240 160
run_group g2 tests/scene_g2_tris.txt  280  60 300 150
run_group g3 tests/scene_g3_quads.txt 200 100 230 190
run_group g4 tests/scene_g4_fan.txt   200  50 210 167

echo
echo "拒绝与退化（都不产出图）"
log=$($BIN run 210,192@105,96 200 60 - 210 192 4 45 2>&1); rc=$?
echo "  g5 超角: $(printf '%s\n' "$log" | grep REJECT || true)  [rc=$rc]"
[ "$rc" -eq 3 ] || { echo "  expected rc=3 for over-cap"; fail=1; }
log=$($BIN run tests/scene_degen_line.txt 100 10 - 120 40 4 45 2>&1); rc=$?
echo "  g6 退化线(120x0): $(printf '%s\n' "$log" | grep REJECT || true)  [rc=$rc]"
[ "$rc" -eq 4 ] || { echo "  expected rc=4 for degenerate line"; fail=1; }
log=$($BIN run tests/scene_degen_point.txt 10 10 - 20 20 4 45 2>&1); rc=$?
echo "  g7 退化点(0x0):  $(printf '%s\n' "$log" | grep REJECT || true)  [rc=$rc]"
[ "$rc" -eq 4 ] || { echo "  expected rc=4 for degenerate point"; fail=1; }

echo
echo "零旋转关掉新逻辑：与原管线逐字节比对"
for spec in "tests/scene_g1_rect.txt 240 160 r1" "tests/scene_g2_tris.txt 300 150 r2" "tests/scene_g3_quads.txt 230 190 r3"; do
  set -- $spec
  $ZERO "$1" "$OUT/zero_old_$4.pgm" "$2" "$3" 1 >/dev/null || { echo "  old cli failed"; fail=1; }
  $BIN run "$1" "$2" "$3" "$OUT/zero_new_$4.pgm" "$2" "$3" 1 45 --theta 0 >/dev/null || { echo "  new cli failed"; fail=1; }
  if bash ../tools/diff.sh "$OUT/zero_old_$4.pgm" "$OUT/zero_new_$4.pgm" >/dev/null; then
    echo "  $4 $(sha256sum "$OUT/zero_old_$4.pgm" | cut -c1-16) IDENTICAL"
  else
    echo "  $4 DIFF"; fail=1
  fi
done

echo
if [ "$fail" -eq 0 ]; then echo "ALL OK"; else echo "FAILURES PRESENT"; fi
exit "$fail"
