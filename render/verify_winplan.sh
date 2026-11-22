#!/bin/bash
# 贪心定窗口端到端验收：
#   1) 四组图形（g1..g4）各报窗口数、覆盖率、重叠率、耗时；覆盖率必须达标；
#   2) g5 把重叠上限压到 0.22 / 0.20，必须拒绝并说明，已接受集合仍守上限；
#   3) 窗口足够大的场景（g6/g7）必须报 FULL 立即停，且与旧整幅渲染逐字节相同；
#   4) 关掉新逻辑（build/cli）的产物与仓库基线 tests/baseline/*.pgm 逐字节相同；
#   5) 每组再用 tools/verify_winplan.py（绕数法、独立重放）复算一遍。
set -u
cd "$(dirname "$0")"
CLI=build/winplan_cli
OLD=build/cli
VER=build/test_winplan
OUT=winplan-out
BASE=tests/baseline
mkdir -p "$OUT"

make -s build/winplan_cli build/cli "$VER" >/dev/null || { echo "build failed"; exit 1; }
"$VER" >/dev/null || { echo "unit tests failed"; exit 1; }

fail=0
row() { printf '%-22s %5s %9s %9s %9s %9s %-9s %s\n' "$@"; }

echo "==== 四组图形：贪心窗口（40x40, stride 8, target 0.98, cap 0.60） ===="
row scene wins coverage overlap rejections total_ms stop target
echo "------------------------------------------------------------------------------------------"
for s in g1_sparse g2_clusters g3_thin g4_mixed; do
  log=$OUT/$s.log
  $CLI run tests/scenes/$s.txt "$OUT/$s.pgm" 256 256 > "$log" 2>&1
  rc=$?
  nw=$(sed -n 's/^plan stop=[A-Z]* nwins=\([0-9]*\).*/\1/p' "$log")
  cov=$(sed -n 's/.*coverage=\([0-9.]*\).*/\1/p' "$log" | head -1)
  ov=$(sed -n 's/.*overlap=\([0-9.]*\).*/\1/p' "$log" | head -1)
  rej=$(grep -c '^reject #' "$log")
  ms=$(sed -n 's/.*total_ms=\([0-9.]*\).*/\1/p' "$log")
  stop=$(sed -n 's/^plan stop=\([A-Z]*\).*/\1/p' "$log")
  tgt=$(sed -n 's/^target_met=\([a-z]*\).*/\1/p' "$log")
  res=ok
  if [ "$rc" -ne 0 ] || [ "$tgt" != yes ]; then res="FAIL rc=$rc"; fail=1; fi
  row "$s" "$nw" "$cov" "$ov" "$rej" "$ms" "$stop" "$res"
  ver=$(python3 -I tools/verify_winplan.py tests/scenes/$s.txt "$log" 256 256 40 40 8 0.98 0.60)
  echo "    verify: $(printf '%s\n' "$ver" | tail -1)  $(printf '%s\n' "$ver" | head -1)"
  [ "$(printf '%s\n' "$ver" | tail -1)" = "MATCH" ] || { fail=1; }
done

echo
echo "==== 重叠上限拒绝（g5_overlap / g2_clusters，收紧 cap） ===="
for spec in "g5_overlap 0.20" "g5_overlap 0.22" "g2_clusters 0.20" "g2_clusters 0.30"; do
  set -- $spec; s=$1; cap=$2
  log=$OUT/${s}_cap$cap.log
  $CLI run tests/scenes/$s.txt "$OUT/${s}_cap$cap.pgm" 256 256 40 40 8 0.98 "$cap" > "$log" 2>&1
  rc=$?
  nw=$(sed -n 's/^plan stop=[A-Z]* nwins=\([0-9]*\).*/\1/p' "$log")
  cov=$(sed -n 's/.*coverage=\([0-9.]*\).*/\1/p' "$log" | head -1)
  ov=$(sed -n 's/.*overlap=\([0-9.]*\).*/\1/p' "$log" | head -1)
  rej=$(grep -c '^reject #' "$log")
  stop=$(sed -n 's/^plan stop=\([A-Z]*\).*/\1/p' "$log")
  first=$(grep -m1 '^reject #' "$log")
  if [ "$stop" = "OVERLAP" ]; then
    res="REJECT rc=$rc"
  elif awk "BEGIN{exit !($ov <= $cap + 1e-12)}"; then
    res="ok rc=$rc"
  else
    res="FAIL overlap>cap"; fail=1
  fi
  printf '%-14s cap=%-5s wins=%-3s cov=%-9s ov=%-9s rej=%-3s stop=%-8s %s\n' \
         "$s" "$cap" "$nw" "$cov" "$ov" "$rej" "$stop" "$res"
  [ -n "${first:-}" ] && echo "    $first"
  ver=$(python3 -I tools/verify_winplan.py tests/scenes/$s.txt "$log" 256 256 40 40 8 0.98 "$cap")
  echo "    verify: $(printf '%s\n' "$ver" | tail -1)"
  [ "$(printf '%s\n' "$ver" | tail -1)" = "MATCH" ] || fail=1
done

echo
echo "==== 已全覆盖：立即停（FULL）并与旧整幅渲染逐字节比对 ===="
for s in g1_sparse g6_cover_all g7_dense; do
  $CLI run tests/scenes/$s.txt "$OUT/${s}_full.pgm" 256 256 128 128 8 0.98 0.60 > "$OUT/${s}_full.log" 2>&1
  stop=$(sed -n 's/^plan stop=\([A-Z]*\).*/\1/p' "$OUT/${s}_full.log")
  nw=$(sed -n 's/^plan stop=[A-Z]* nwins=\([0-9]*\).*/\1/p' "$OUT/${s}_full.log")
  cov=$(sed -n 's/.*coverage=\([0-9.]*\).*/\1/p' "$OUT/${s}_full.log" | head -1)
  $OLD tests/scenes/$s.txt "$OUT/${s}_old.pgm" 256 256 1.0 >/dev/null
  if [ "$stop" != "FULL" ]; then echo "  $s FAIL stop=$stop"; fail=1; continue; fi
  if cmp -s "$OUT/${s}_full.pgm" "$OUT/${s}_old.pgm"; then
    echo "  $s stop=FULL wins=$nw coverage=$cov  IDENTICAL to old render"
  else
    echo "  $s stop=FULL wins=$nw coverage=$cov  DIFFERS from old render"; fail=1
  fi
done

echo
echo "==== 关掉新逻辑：旧 CLI 与仓库基线逐字节比对 ===="
for s in g1_sparse g2_clusters g3_thin g4_mixed g5_overlap; do
  $OLD tests/scenes/$s.txt "$OUT/${s}_legacy.pgm" 256 256 1.0 >/dev/null
  if cmp -s "$BASE/$s.pgm" "$OUT/${s}_legacy.pgm"; then
    echo "  $s $(sha256sum "$BASE/$s.pgm" | cut -c1-16) IDENTICAL"
  else
    echo "  $s DIFF"; fail=1
  fi
done

echo
echo "==== 固定窗口数对照（--fixed=N，不做贪心、不查重叠上限） ===="
echo "  老做法：窗口数量定死，按扫描序取前 N 个。g4_mixed 同参数下贪心需 21 个"
echo "  窗口达到 0.980794 覆盖率；定死数量时无论 4 / 8 / 21 个都只覆盖一角。"
for n in 4 8 21; do
  log=$OUT/fixed_$n.log
  $CLI run tests/scenes/g4_mixed.txt "$OUT/fixed_$n.pgm" 256 256 40 40 8 0.98 0.60 --fixed=$n > "$log" 2>&1
  nw=$(sed -n 's/^plan stop=[A-Z]* nwins=\([0-9]*\).*/\1/p' "$log")
  cov=$(sed -n 's/.*coverage=\([0-9.]*\).*/\1/p' "$log" | head -1)
  ov=$(sed -n 's/.*overlap=\([0-9.]*\).*/\1/p' "$log" | head -1)
  stop=$(sed -n 's/^plan stop=\([A-Z]*\).*/\1/p' "$log")
  printf '  --fixed=%-2s stop=%-6s wins=%-3s coverage=%-9s overlap=%s\n' "$n" "$stop" "$nw" "$cov" "$ov"
  [ "$stop" = "FIXED" ] || { echo "  expected FIXED stop"; fail=1; }
  # 定死数量时覆盖率应当明显低于目标（这正是旧做法的问题）。
  if awk "BEGIN{exit !($cov >= 0.98)}"; then
    echo "  fixed mode unexpectedly reached target"; fail=1
  fi
done

echo
if [ "$fail" -eq 0 ]; then echo "ALL OK"; else echo "FAILURES PRESENT"; fi
exit "$fail"
