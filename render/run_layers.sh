#!/bin/bash
# 图层依赖 DAG 驱动：四组依赖图各报关键路径、渲染顺序、差异像素、耗时；
# 有环夹具必须拒绝并指出环上图层；手排顺序（关闭新逻辑）与自动结果逐字节一致。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CASES="$ROOT/render/cases"
OUT="$ROOT/render/build/layers"
W=40
H=30
GRAPHS="g1 g2 g3 g4"
CYCLE="g5-cycle"

mkdir -p "$OUT"
make -s -C "$ROOT/render" build/layercli || exit 1
make -s -C "$ROOT/gold" all || exit 1
CLI="$ROOT/render/build/layercli"
REF="$ROOT/gold/build/ref"

fail=0
echo "================ 图层依赖 DAG 报告 ================"
for g in $GRAPHS; do
    scene="$CASES/$g.txt"
    auto_pgm="$OUT/$g.auto.pgm"
    manual_pgm="$OUT/$g.manual.pgm"
    gold_pgm="$OUT/$g.gold.pgm"

    info=$("$CLI" auto "$scene" "$auto_pgm" "$W" "$H") || { echo "[$g] auto 渲染失败"; fail=1; continue; }
    "$REF" layers "$scene" "$W" "$H" > "$gold_pgm" || { echo "[$g] gold 渲染失败"; fail=1; continue; }
    "$CLI" manual "$scene" "$manual_pgm" "$W" "$H" >/dev/null || { echo "[$g] manual 渲染失败"; fail=1; continue; }

    crit=$(printf '%s\n' "$info" | sed -n 's/^critical [0-9]*: //p')
    order=$(printf '%s\n' "$info" | sed -n 's/^order: //p')
    iso=$(printf '%s\n' "$info" | sed -n 's/^isolated: //p')
    elapsed=$(printf '%s\n' "$info" | sed -n 's/^elapsed_us //p')
    diffline=$("$CLI" diff "$auto_pgm" "$gold_pgm") || true
    diffpix=${diffline#DIFFPIX }
    diffpix=${diffpix%% /*}

    if cmp -s "$manual_pgm" "$auto_pgm"; then manual_status="IDENTICAL"; else manual_status="DIFFER"; fail=1; fi

    echo "[$g]"
    echo "  关键路径 : $crit"
    echo "  渲染顺序 : $order"
    echo "  孤立图层 : $iso"
    echo "  差异像素 : $diffpix"
    echo "  耗时(us) : $elapsed"
    echo "  手排开关 : manual vs auto = $manual_status"
    "$CLI" dot "$scene" > "$OUT/$g.dot" 2>/dev/null || true

    [ "$diffpix" = "0" ] || { echo "  !! 差异像素必须为零"; fail=1; }
done

echo "================ 有环拒绝校验 ================"
cyc_out=$("$CLI" auto "$CASES/$CYCLE.txt" "$OUT/cycle.pgm" "$W" "$H" 2>&1); rc=$?
if [ $rc -eq 4 ] && printf '%s' "$cyc_out" | grep -q '^REJECT cycle:'; then
    echo "[$CYCLE] 已拒绝（退出码 4）：$cyc_out"
else
    echo "[$CYCLE] 环检测未生效！rc=$rc out=$cyc_out"; fail=1
fi
ref_out=$("$REF" layers "$CASES/$CYCLE.txt" "$W" "$H" 2>&1 >/dev/null); rrc=$?
[ $rrc -eq 4 ] && echo "[$CYCLE] gold 参照同样拒绝（退出码 4）：$ref_out" || { echo "gold 环检测失效"; fail=1; }
[ -e "$OUT/cycle.pgm" ] && { echo "[$CYCLE] 有环时不应产出 PGM"; fail=1; }

echo "================================================="
if [ $fail -eq 0 ]; then
    echo "全部通过：四图差异像素均为 0，手排/自动逐字节一致，有环必拒。"
else
    echo "存在失败项，见上。"
fi
exit $fail
