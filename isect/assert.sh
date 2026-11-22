#!/bin/bash
# 硬断言：需求中的硬性条件，任一不满足即非零退出。
#   1. 每组漏判必须为 0
#   2. 边界证明：绑定对在 eps_req 纳入、下调一个 ULP 后漏判
#   3. legacy 交点表与冻结源码产物逐字节相同
#   4. gold 双精确实现（C __int128 与 Python Fraction）结论一致
set -u
cd "$(dirname "$0")"
DATASETS="A_general B_grazing C_endpoint D_snapbig"
fail=0

echo "[1] 漏判必须为 0"
for d in $DATASETS; do
    m=$(awk '/^### /{name=$2} name=="'$d'" && /^misses=/{print $1}' report.txt | sed 's/misses=//')
    if [ "$m" != "0" ]; then echo "  FAIL $d misses=$m"; fail=1;
    else echo "  ok $d misses=0"; fi
done

echo "[2] 「再小就漏」边界证明"
for d in $DATASETS; do
    line=$(awk '/^### /{name=$2} name=="'$d'" && /^proof_below:/' report.txt)
    echo "$line" | grep -q 'bind_included_at_req=1' \
        && echo "$line" | grep -q 'bind_missed_below=1' \
        && echo "$line" | grep -qE 'misses_below=[1-9]' \
        && echo "  ok $d" || { echo "  FAIL $d: $line"; fail=1; }
done

echo "[3] legacy 交点表逐字节"
for d in $DATASETS; do
    if cmp -s build/$d.legacy_tbl build/$d.table_legacy; then
        echo "  ok $d IDENTICAL $(wc -c < build/$d.legacy_tbl) bytes"
    else echo "  FAIL $d legacy byte diff"; fail=1; fi
done

echo "[4] gold 双精确实现一致"
for d in $DATASETS; do
    if grep -q "^AGREE n=" <(python3 -I gold/verify.py data/$d/master.txt build/$d.gold "$(cat data/$d/snap.txt)" 2>&1); then
        echo "  ok $d"
    else echo "  FAIL $d gold mismatch"; fail=1; fi
done

echo
if [ "$fail" = 0 ]; then echo "ALL ASSERTIONS PASS"; else echo "ASSERTION FAILURE"; fi
exit $fail
