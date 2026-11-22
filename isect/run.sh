#!/bin/bash
# 全流程：数据 -> gold 精确真值 -> 双实现交叉验证 -> 容差反解 -> legacy 逐字节比对
set -u
cd "$(dirname "$0")"
DATASETS="A_general B_grazing C_endpoint D_snapbig"
mkdir -p build data
: > report.txt

echo "== 1. 生成四组线段集 =="
./build/gen data

echo "== 2. gold 精确整数参照 + Fraction 交叉验证（吸附口径见 snap.txt）=="
for d in $DATASETS; do
    snap=$(cat data/$d/snap.txt)
    ./build/ref data/$d/master.txt "$snap" > build/$d.gold
    head -1 build/$d.gold
    if command -v python3 >/dev/null; then
        python3 -I gold/verify.py data/$d/master.txt build/$d.gold "$snap" || exit 1
    fi
done

echo "== 3. 容差反解（漏判必须为 0；超线长一成拒绝；退化单独统计）=="
for d in $DATASETS; do
    {
        echo "### $d"
        ./build/sweep data/$d/segs.txt build/$d.gold
        echo
    } | tee -a report.txt
done

echo "== 4. 新逻辑关闭时 legacy 交点表逐字节不变 =="
for d in $DATASETS; do
    ./build/ltable data/$d/segs.txt > build/$d.legacy_tbl
    ./build/table legacy data/$d/segs.txt > build/$d.table_legacy
    if cmp -s build/$d.legacy_tbl build/$d.table_legacy; then
        echo "IDENTICAL $d legacy table ($(wc -c < build/$d.legacy_tbl) bytes)"
    else
        off=$(cmp -l build/$d.legacy_tbl build/$d.table_legacy 2>/dev/null | head -1 | awk '{print $1}')
        echo "DIFF $d at byte ${off:-?}"; exit 1
    fi
done

echo "== 5. 反解容差下的新路径交点表（gap 模式）=="
for d in $DATASETS; do
    star=$(grep '^eps_star=' data/$d/sweep_result.txt | head -1 | sed -E 's/^eps_star=//')
    sc=$(grep '^candidates=' data/$d/sweep_result.txt | head -1 | sed -E 's/^candidates=//')
    if [ -z "$star" ]; then echo "无法解析 $d 的 eps_star"; exit 1; fi
    ./build/table gap data/$d/segs.txt "$star" > build/$d.gap_tbl
    tcount=$(tail -1 build/$d.gap_tbl | grep -oE 'pairs=[0-9]+' | head -1 | cut -d= -f2)
    if [ "$tcount" = "$sc" ]; then ok=YES; else ok=NO; fi
    echo "$d gap table: $(tail -1 build/$d.gap_tbl) [sweep candidates=$sc 一致=$ok]"
done

echo
echo "报告已写入 report.txt"
