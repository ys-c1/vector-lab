#!/bin/bash
# 逐刀账目端到端验证。
#   1) 生成四组窗口序列，逐刀裁剪，每步面积/多边形落盘；
#   2) 用账目反推第一个窗口的有效区域，与独立方法直接求出的区域比面积，
#      相对偏差必须 ≤ 1e-6；任一步对不上就停并指出步号；
#   3) 拒绝用例：空窗口、顶点不足三、窗口号重复；
#   4) 故障注入：人为让第 2 步偏 2e-4，确认账目能停在正确的步。
set -u
cd "$(dirname "$0")"

OUT=ledger-out
PLAN=tests/ledger/plan.txt
LEDGER=./build/ledger
FAULT=./build/ledger_fault
fails=0

note() { printf '%s\n' "$*"; }
check() { # check <期望退出码> <说明> <命令...>
    local want=$1 desc=$2
    shift 2
    "$@"
    local rc=$?
    if [ "$rc" -eq "$want" ]; then
        note "PASS  $desc（退出码 $rc）"
    else
        note "FAIL  $desc：期望退出码 $want，实际 $rc"
        fails=$((fails + 1))
    fi
}

rm -rf "$OUT"
python3 -I tests/gen_ledger.py || { note "FAIL  生成测试数据"; exit 1; }
make -s build/ledger build/ledger_fault || { note "FAIL  构建"; exit 1; }

note "==================== 四组窗口序列 ===================="
check 0 "四组序列逐刀对账" "$LEDGER" plan "$PLAN" "$OUT"
note ""
note "--- 逐步账目（report.tsv）---"
cat "$OUT/report.tsv"
note ""
note "--- 每序列汇总（summary.tsv）---"
cat "$OUT/summary.tsv"
note ""

# 每步产物齐全性：steps/eff/direct 三类各 stepNN_poly+area（含 step00 基线）
for s in seq1_axis2 seq2_rot120 seq3_three seq4_cw; do
    missing=0
    for f in "$OUT/$s"/steps/step00_poly.txt "$OUT/$s"/steps/step00_area.txt; do
        [ -f "$f" ] || missing=1
    done
    n=$(ls "$OUT/$s"/steps/step*_area.txt 2>/dev/null | wc -l)
    for k in $(seq 1 $((n - 1))); do
        for d in steps eff direct; do
            st=$(printf 'step%02d' "$k")
            [ -f "$OUT/$s/$d/${st}_poly.txt" ] || missing=1
            [ -f "$OUT/$s/$d/${st}_area.txt" ] || missing=1
        done
    done
    if [ "$missing" -eq 0 ]; then
        note "PASS  $s 每步面积/反推区域/直接区域均已落盘（$((n - 1)) 刀）"
    else
        note "FAIL  $s 有步骤产物缺失"
        fails=$((fails + 1))
    fi
done

# 汇总里每个序列都必须 OK，且相对偏差在 1e-6 内
awk -F'\t' 'NR>1 { if ($9 != "OK" || $6+0 > 1e-6) { printf "FAIL  汇总行 %s 状态 %s 偏差 %s\n", $1, $9, $6; bad=1 } }
     END { exit bad ? 1 : 0 }' "$OUT/summary.tsv"
if [ $? -eq 0 ]; then
    note "PASS  所有序列状态 OK，反推区域与直接区域相对偏差 ≤ 1e-6"
else
    fails=$((fails + 1))
fi

note ""
note "==================== 拒绝用例 ===================="
check 2 "空窗口" "$LEDGER" plan tests/ledger/plan_rej_empty.txt "$OUT/rej_empty"
check 2 "顶点不足三" "$LEDGER" plan tests/ledger/plan_rej_two.txt "$OUT/rej_two"
check 2 "窗口号重复" "$LEDGER" plan tests/ledger/plan_rej_dup.txt "$OUT/rej_dup"
check 2 "非凸窗口" "$LEDGER" plan tests/ledger/plan_rej_nonconvex.txt "$OUT/rej_nonconvex"

note ""
note "==================== 故障注入（应停在第 1 步）===================="
"$FAULT" plan tests/ledger/plan_fault.txt "$OUT/fault" 2> "$OUT/fault.err"
rc=$?
cat "$OUT/fault.err"
if [ "$rc" -eq 3 ] && grep -q '第 1 步' "$OUT/fault.err"; then
    note "PASS  账目在第 1 步停下并报出步号"
else
    note "FAIL  故障注入未在预期步停下（退出码 $rc）"
    fails=$((fails + 1))
fi
awk -F'\t' 'NR>1 && $14 != "OK" { found=1 } END { exit found ? 0 : 1 }' "$OUT/fault/report.tsv" \
    && note "PASS  失败步骤已记入 report.tsv" \
    || { note "FAIL  report.tsv 未记录失败步骤"; fails=$((fails + 1)); }

note ""
if [ "$fails" -eq 0 ]; then
    note "全部通过。产物在 $OUT/"
    exit 0
fi
note "$fails 项失败。"
exit 1
