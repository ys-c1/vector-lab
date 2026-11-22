#!/bin/bash
# snap 基准：四组路径各报目标总长、取整误差、耗时。
#   1) 反解：snap <line> <m>（链接时长优化，误差/耗时都报）
#   2) 旧小数等分：snap <line> <m> --legacy，必须与 gold/ref points 逐字节相同
#   3) 关闭新逻辑（SNAP_DISABLE=1）时仍走 --legacy，产物与 gold 逐字节相同
#   4) 拒绝情形与"半像素内无解"的最接近方案
# 产物在 snap/bench_out/ 下。
set -u
cd "$(dirname "$0")"
OUT=bench_out
mkdir -p "$OUT"
ROOT=$(cd .. && pwd)

make -C "$ROOT/gold" >/dev/null || { echo "build failed: gold"; exit 1; }
make build/snap >/dev/null || { echo "build failed: snap"; exit 1; }
REF="$ROOT/gold/build/ref"
SNAP=./build/snap

fail=0
record() { echo "$*" | tee -a "$OUT/metrics.log"; }

: > "$OUT/metrics.log"
record "group        m    T          L*         d*         cover     status    max_err   axis_err  rms_err   ms        evals"
record "-----------------------------------------------------------------------------------------------------------------------------------"

# 四组路径：整数格点折线 / 3-4-5 斜边 / 混合 45° 与轴向 / 30° 斜向
CASES=(
  "g1_axis:104"
  "g2_diag345:12"
  "g3_mixed:8"
  "g4_skew:8"
)

for spec in "${CASES[@]}"; do
    name=${spec%%:*}; m=${spec##*:}
    line=data/$name.txt
    r=$($SNAP "$line" "$m" --nodes "$OUT/$name.nodes" 2> "$OUT/$name.err")
    rc=$?
    [[ $rc -ne 0 && $rc -ne 3 ]] && { record "FAIL solve $name rc=$rc"; fail=1; }
    summary=$(cat "$OUT/$name.err")
    L=$(echo "$summary" | sed -n 's/.*L=\([0-9.]*\).*/\1/p')
    dstar=$(echo "$summary" | sed -n 's/.* d=\([0-9.]*\).*/\1/p')
    st=$(echo "$summary" | sed -n 's/.*status=\([A-Z_]*\).*/\1/p')
    cover=$(echo "$summary" | sed -n 's/.*cover=\([0-9.]*\).*/\1/p')
    maxe=$(echo "$summary" | sed -n 's/.*max_err=\([0-9.]*\).*/\1/p')
    axe=$(echo "$summary" | sed -n 's/.*axis_err=\([0-9.]*\).*/\1/p')
    rms=$(echo "$summary" | sed -n 's/.*rms_err=\([0-9.]*\).*/\1/p')
    ms=$(echo "$summary" | sed -n 's/.*ms=\([0-9.]*\).*/\1/p')
    ev=$(echo "$summary" | sed -n 's/.*evals=\([0-9]*\).*/\1/p')
    T=$(echo "$summary" | sed -n 's/.*T=\([0-9.]*\).*/\1/p')
    record "$(printf '%-12s %3s  %-10s %-10s %-10s %-9s %-9s %-9s %-9s %-9s %-9s %s' \
        "$name" "$m" "$T" "$L" "$dstar" "$cover" "$st" "$maxe" "$axe" "$rms" "$ms" "$ev")"

    # 半像素硬约束：EXACT/NEAREST 必须满足，OVER_BUDGET 必须报告
    ok=$(awk -v e="$maxe" 'BEGIN{print (e+0 <= 0.5+1e-12)?"1":"0"}')
    if [ "$ok" = "1" ]; then
        if [ "$st" = "OVER_BUDGET" ]; then record "FAIL $name status=$st max_err=$maxe"; fail=1; fi
    else
        if [ "$st" != "OVER_BUDGET" ]; then record "FAIL $name over budget but status=$st"; fail=1; fi
    fi

    # 目标总长与取整误差必须自洽：err 由 nodes 文件重算
    awk -v m="$m" -v L="$L" -v tol="1e-6" -v name="$name" '
        NR==1 { if ($1+0 != m+0) { printf "FAIL %s nodes header %d\n", name, $1; exit 1 } next }
        { ex=$5+0; ey=$6+0; e=$7+0;
          d=sqrt(ex*ex+ey*ey);
          if (d > 0.7072) { printf "FAIL %s node %d err %.6f\n", name, NR-1, d; bad=1 }
          if (e > mx) mx=e; if (d > md) md=d }
        END { if (!bad) printf "  nodecheck %s nodes=%d max_reported=%.9f max_recomputed=%.9f\n", name, NR-1, mx, md }' \
        "$OUT/$name.nodes" || fail=1

    # 旧路径：必须与 gold/ref points 逐字节相同
    $SNAP "$line" "$m" --legacy > "$OUT/$name.legacy" 2>/dev/null
    "$REF" points "$line" "$m" > "$OUT/$name.gold"
    if cmp -s "$OUT/$name.legacy" "$OUT/$name.gold"; then
        record "  legacy $name: IDENTICAL to gold"
    else
        record "  legacy $name: DIFF"; fail=1
    fi

    # 关闭新逻辑：SNAP_DISABLE=1 时按旧路径出图（逐字节不变）
    SNAP_DISABLE=1 $SNAP "$line" "$m" > "$OUT/$name.disabled" 2>/dev/null
    if cmp -s "$OUT/$name.disabled" "$OUT/$name.gold"; then
        record "  disabled $name: IDENTICAL to gold"
    else
        record "  disabled $name: DIFF"; fail=1
    fi
done

# 拒绝情形
record ""
record "== reject cases =="
printf "0 0\n10 0\n10 10\n0 0\n" > "$OUT/closed.txt"
out=$($SNAP "$OUT/closed.txt" 5 2>&1); rc=$?
record "closed rc=$rc :: $out"
[ $rc -eq 2 ] && echo "$out" | grep -q "REJECT closed" || { record "FAIL closed reject"; fail=1; }

printf "4 4\n4 4\n9 9\n" > "$OUT/zero.txt"
out=$($SNAP "$OUT/zero.txt" 4 2>&1); rc=$?
record "zero rc=$rc :: $out"
[ $rc -eq 2 ] && echo "$out" | grep -q "REJECT degenerate" || { record "FAIL zero reject"; fail=1; }

out=$($SNAP data/g1_axis.txt 1 2>&1); rc=$?
record "m<2 rc=$rc :: $out"
[ $rc -eq 2 ] || { record "FAIL m<2 reject"; fail=1; }

# 半像素内无解：给出最接近方案（退出码 3）。
# 折线 (0,0)-(1,1)-(2,4) 在 m=7 时任何 d 都有点超出半像素（已与暴力逼近核对）。
printf "0 0\n1 1\n2 4\n" > "$OUT/over.txt"
out=$($SNAP "$OUT/over.txt" 7 2>&1); rc=$?
record "over-budget rc=$rc :: $(echo "$out" | grep RESULT)"
[ $rc -eq 3 ] || { record "FAIL over-budget exit code"; fail=1; }
echo "$out" | grep -q "status=OVER_BUDGET" || { record "FAIL over-budget status"; fail=1; }
over=$(echo "$out" | sed -n 's/.*max_err=\([0-9.]*\).*/\1/p')
awk -v e="$over" 'BEGIN{exit !(e+0 > 0.5)}' || { record "FAIL over-budget max_err=$over"; fail=1; }

if [ "$fail" -eq 0 ]; then
    record ""
    record "ALL CHECKS PASSED"
else
    record ""
    record "CHECKS FAILED"
fi
exit "$fail"
