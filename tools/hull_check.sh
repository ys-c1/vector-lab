#!/bin/bash
# 凸层剥解全流程：四组点集反解、PGM/层文件与 gold 参照逐字节比对、
# 拒绝路径（共线退化、层号超总层数）验证。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ART="$ROOT/artifacts/hull"
TARGET=${TARGET:-0.25}
W=240; H=240
SETS=(random cluster collinear twocluster)

mkdir -p "$ART"
make -s -C "$ROOT/gold" all
make -s -C "$ROOT/hull" build/demo build/legacy_demo
"$ROOT/hull/build/demo" gen "$ART"

# 额外的全共线退化点集（用于拒绝路径演示）。
awk 'BEGIN{for(i=0;i<20;++i) printf "%.3f %.3f\n", i*4.0, 0.5*i*4.0+3.0}' \
    > "$ART/degen_collinear.txt"

printf '\n== 四组点集反解（target=%.2f） ==\n' "$TARGET"
printf '%-12s %-8s %-8s %-12s %-10s %-10s %s\n' \
       set points layers solved1 ratio ms pgm_cmp
allok=1
for s in "${SETS[@]}"; do
  rep=$("$ROOT/hull/build/demo" run "$ART/$s.txt" "$TARGET" \
            "$ART/$s.pgm" "$ART/$s.layers" "$W" "$H" 2>"$ART/$s.err")
  rc=$?
  getv() { echo "$rep" | sed -n "s/.*$1=\\([0-9.]*\\).*/\\1/p" | head -1; }
  pts=$(getv points)
  nl=$(getv total_layers)
  sol=$(getv solved_layer)
  ratio=$(getv remaining_area_ratio)
  ms=$(getv elapsed_ms)

  # gold 参照：不同算法（gift wrapping），输出层文件与灰度 PGM。
  "$ROOT/gold/build/ref" layers "$ART/$s.txt" "$TARGET" \
      > "$ART/$s.ref.layers" 2>"$ART/$s.ref.err" || { echo "ref layers failed: $s"; allok=0; continue; }
  "$ROOT/gold/build/ref" lpgm "$ART/$s.txt" "$TARGET" "$W" "$H" \
      > "$ART/$s.ref.pgm" 2>/dev/null || { echo "ref lpgm failed: $s"; allok=0; continue; }

  cmp_pgm=$("$ROOT/tools/diff.sh" "$ART/$s.pgm" "$ART/$s.ref.pgm" | awk '{print $1}')
  cmp_lay=$("$ROOT/tools/diff.sh" "$ART/$s.layers" "$ART/$s.ref.layers" | awk '{print $1}')
  printf '%-12s %-8s %-8s %-12s %-10s %-10s pgm:%s layers:%s\n' \
         "$s" "$pts" "$nl" "$sol" "$ratio" "$ms" "$cmp_pgm" "$cmp_lay"
  note=$(echo "$rep" | grep -c 'NOTE' || true)
  [ "$note" -gt 0 ] && echo "$rep" | grep 'NOTE' | sed 's/^/  [warn] /'
  [ -s "$ART/$s.err" ] && sed 's/^/  [stderr] /' "$ART/$s.err"
  [ "$cmp_pgm" != IDENTICAL ] || [ "$cmp_lay" != IDENTICAL ] || [ $rc -ne 0 ] && allok=0
  # 硬约束：剩余占比必须严格低于目标。
  awk -v r="$ratio" -v t="$TARGET" 'BEGIN{exit !(r+0 < t+0)}' \
      || { echo "  [FAIL] ratio $ratio not below target $TARGET ($s)"; allok=0; }
done

printf '\n== 拒绝路径 ==\n'
echo "-- 全共线退化集（应拒绝并说明不足三点） --"
"$ROOT/hull/build/demo" run "$ART/degen_collinear.txt" "$TARGET" \
    "$ART/degen.pgm" 2>&1 | sed 's/^/  /' || true

echo "-- 目标过严，层号超总层数（应拒绝并说明上限） --"
"$ROOT/hull/build/demo" run "$ART/random.txt" 0.001 \
    "$ART/overcap.pgm" 2>&1 | sed 's/^/  /' || true

echo "-- gold 参照对退化集同样拒绝 --"
"$ROOT/gold/build/ref" lpgm "$ART/degen_collinear.txt" "$TARGET" "$W" "$H" \
    > /dev/null 2>"$ART/degen.ref.err" && echo "  [FAIL] gold did not reject" \
    || sed 's/^/  /' "$ART/degen.ref.err"

printf '\n== 旧逻辑（新逻辑关闭）demo.pgm 逐字节稳定性 ==\n'
"$ROOT/hull/build/legacy_demo" "$ART/demo.pgm" >/dev/null
if [ -f "$ART/demo.pgm.base" ]; then
  "$ROOT/tools/diff.sh" "$ART/demo.pgm" "$ART/demo.pgm.base"
else
  cp "$ART/demo.pgm" "$ART/demo.pgm.base"
  echo "  baseline established: $ART/demo.pgm.base"
fi
# 连续再跑两次，确认确定性。
"$ROOT/hull/build/legacy_demo" "$ART/demo.pgm" >/dev/null
"$ROOT/tools/diff.sh" "$ART/demo.pgm" "$ART/demo.pgm.base"

[ $allok -eq 1 ] && echo 'RESULT: ALL OK' || { echo 'RESULT: FAILURES'; exit 1; }
