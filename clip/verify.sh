#!/bin/bash
# 端到端验证：一刀 vs 两刀、逐步面积对 gold、拒绝用例、内存四倍预算。
set -u
cd "$(dirname "$0")"
ROOT=..
REF=$ROOT/gold/build/ref
DIFF=$ROOT/tools/diff.sh
T=tests
OUT=run-out

mkdir -p "$OUT"
python3 -I "$T/gen_data.py"
make -s -C "$ROOT/gold"
make -s run || exit 1
RUN=./build/run

echo "==================== 用例 1：两刀 vs 一刀 ===================="
$RUN clip "$T/poly_rect.txt" "$T/manifest_two.txt" "$OUT/two"   "$REF"; rc_two=$?
$RUN clip "$T/poly_rect.txt" "$T/manifest_one.txt" "$OUT/one"   "$REF"; rc_one=$?
echo "--- 两刀报告 ---"; cat "$OUT/two/report.tsv"
echo "--- 一刀报告 ---"; cat "$OUT/one/report.tsv"
$RUN cmp "$OUT/one/steps/step01_poly.txt" "$OUT/one/steps/step01_area.txt" \
         "$OUT/two/steps/step02_poly.txt" "$OUT/two/steps/step02_area.txt"
rc_cmp=$?
for d in "$OUT/two" "$OUT/one"; do
  for g in "$d"/gold/step*_area.txt; do
    [ -e "$g" ] || continue
    s=${g/gold/steps}
    "$DIFF" "$s" "$g" || true
  done
done

echo "==================== 用例 2：顺时针窗口 ===================="
$RUN clip "$T/poly_rect.txt" "$T/manifest_cw.txt" "$OUT/cw" "$REF"
cat "$OUT/cw/report.tsv"

echo "==================== 用例 3：120 顶点多边形（旧实现静默截断）==================="
$RUN clip "$T/poly_big.txt" "$T/manifest_big.txt" "$OUT/big" "$REF"
cat "$OUT/big/report.tsv"
"$DIFF" "$OUT/big/steps/step01_area.txt" "$OUT/big/gold/step01_area.txt" || true

echo "==================== 用例 4：非轴对齐（gold 容差路径）==================="
$RUN clip "$T/poly_tri.txt" "$T/manifest_rot.txt" "$OUT/rot" "$REF"
cat "$OUT/rot/report.tsv"

echo "==================== 用例 5：拒绝输入 ===================="
for m in bad_empty bad_two bad_dupid bad_degenerate; do
  echo "--- $m ---"
  $RUN clip "$T/poly_rect.txt" "$T/manifest_$m.txt" "$OUT/rej_$m" "$REF"
  echo "退出码 $?"
done

echo "==================== 用例 6：四倍内存预算（故障注入）==================="
make -s build/run_fault
./build/run_fault clip "$T/poly_rect.txt" "$T/manifest_one.txt" "$OUT/fault" "$REF"
echo "故障实现退出码 $?（期望 2=拒绝）"
[ -f "$OUT/fault/report.tsv" ] && cat "$OUT/fault/report.tsv"

echo "==================== 汇总 ===================="
echo "两刀 rc=$rc_two 一刀 rc=$rc_one 一刀vs两刀 cmp rc=$rc_cmp (0=一致)"
