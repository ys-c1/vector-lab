#!/bin/bash
# 端到端：主实现与 gold/ref 出同尺寸 PGM，叠图比对并记一笔指标。
#   1) 单子路径场景：主实现 PGM 必须与 gold 逐字节相同（diff_pixels=0）
#   2) 多子路径（带洞）场景：照样出图、叠图，报告差异像素
#   3) 宽/高为零或负：两边都当场停，指名参数
# 产物在 svg/build/render/ 下：*.pgm / overlay-*.pgm / metrics.log
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$ROOT/svg/build/render
mkdir -p "$OUT"
METRICS=$OUT/metrics.log
: > "$METRICS"

make -C svg build/demo >/dev/null || { echo "build failed: svg demo"; exit 1; }
make -C gold build/ref >/dev/null || { echo "build failed: gold ref"; exit 1; }
make -C tools build/pgmdiff >/dev/null || { echo "build failed: pgmdiff"; exit 1; }
DEMO=$ROOT/svg/build/demo
REF=$ROOT/gold/build/ref
DIFF=$ROOT/tools/build/pgmdiff

fail=0
record() { echo "$*" | tee -a "$METRICS"; }

# 形状 1：三角形（单子路径，gold 可直接消费）
TRI="M2,3 L38,7 L21,43 Z"
# 形状 2：带洞嵌套矩形（多子路径，仅主实现支持）
HOLE="M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z"
# 形状 3：旋转四边形（非轴对齐，覆盖率才有灰度）
QUAD="M4,6 L40,2 L46,34 L10,40 Z"

render_pair() {  # $1=path-d $2=w $3=h $4=tag
    local d=$1 w=$2 h=$3 tag=$4
    record "[$tag] w=$w h=$h"
    "$DEMO" render "$d" "$w" "$h" "$OUT/$tag-main.pgm" 2>&1 | tee -a "$METRICS"
    "$DEMO" poly "$d" "$OUT/$tag.poly" || { record "FAIL poly dump $tag"; fail=1; return; }
    "$REF" pixels "$OUT/$tag.poly" "$w" "$h" > "$OUT/$tag-gold.pgm" \
        || { record "FAIL gold render $tag"; fail=1; return; }
    local diffout
    diffout=$("$DIFF" "$OUT/$tag-main.pgm" "$OUT/$tag-gold.pgm" \
        "$OUT/overlay-$tag.pgm")
    local rc=$?
    record "$diffout"
    if [ "$rc" -gt 1 ]; then
        record "FAIL pgmdiff error on $tag (rc=$rc)"
        fail=1
        return
    fi
    local n
    n=$(echo "$diffout" | sed -n 's/.*diff_pixels \([0-9]*\).*/\1/p')
    [ "$rc" -eq 0 ] && n=0
    record "[$tag] diff_pixels=${n}"
}

render_pair "$TRI"  64 64 tri
render_pair "$QUAD" 80 48 quad

# 带洞：gold 只收单个多边形，这里主实现单独出图，并与外框 gold 图叠图看差异
record "[hole] multi-subpath (gold has no hole semantics; main-only render)"
"$DEMO" render "$HOLE" 50 50 "$OUT/hole-main.pgm" 2>&1 | tee -a "$METRICS"
"$DEMO" poly "$HOLE" "$OUT/hole.poly"
"$REF" pixels "$OUT/hole.poly" 50 50 > "$OUT/hole-gold.pgm"
holeout=$("$DIFF" "$OUT/hole-main.pgm" "$OUT/hole-gold.pgm" \
    "$OUT/overlay-hole.pgm")
holerc=$?
record "$holeout"
if [ "$holerc" -gt 1 ]; then
    record "FAIL pgmdiff error on hole (rc=$holerc)"
    fail=1
else
    holen=$(echo "$holeout" | sed -n 's/.*diff_pixels \([0-9]*\).*/\1/p')
    [ "$holerc" -eq 0 ] && holen=0
    record "[hole] diff_pixels=${holen} (nonzero expected: hole winding differs)"
fi

# 尺寸非法：宽或高为零、负，当场停并指名哪个参数
expect_bad() {  # $1=w $2=h $3=期望出现的参数名
    local out rc
    out=$("$DEMO" render "$TRI" "$1" "$2" "$OUT/bad.pgm" 2>&1)
    rc=$?
    if [ "$rc" -ne 2 ] || ! echo "$out" | grep -q "param: $3"; then
        record "FAIL bad-dim w=$1 h=$2 want param:$3 rc=$rc got: $out"
        fail=1
    else
        record "ok reject w=$1 h=$2 (param: $3)"
    fi
    out=$("$REF" pixels "$OUT/tri.poly" "$1" "$2" 2>&1 >/dev/null)
    rc=$?
    if [ "$rc" -ne 2 ] || ! echo "$out" | grep -q "param: $3"; then
        record "FAIL gold bad-dim w=$1 h=$2 want param:$3 rc=$rc got: $out"
        fail=1
    else
        record "ok gold reject w=$1 h=$2 (param: $3)"
    fi
}
expect_bad 0  20 width
expect_bad 20 0  height
expect_bad -7 20 width
expect_bad 20 -7 height

# 单子路径场景必须逐字节一致
for tag in tri quad; do
    if ! cmp -s "$OUT/$tag-main.pgm" "$OUT/$tag-gold.pgm"; then
        record "FAIL $tag: main vs gold not byte-identical"
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    record "ALL CHECKS PASSED"
else
    record "CHECKS FAILED"
fi
exit "$fail"
