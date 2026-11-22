#!/bin/bash
# 端到端：单元测试 + 参照自检 + 四个场景 + 两条拒绝路径。
set -u
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
REF="$ROOT/gold/build/ref"
rc=0

echo "== 构建 =="
make -C "$ROOT/gold" >/dev/null || { echo "gold 构建失败"; exit 1; }
make -s build/test build/run || { echo "peel 构建失败"; exit 1; }

echo
echo "== 单元测试 =="
./build/test || rc=1

echo
echo "== 参照自检：trap 与鞋带公式在若干形状上的一致性与 256x256 数格子对照 =="
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/tri.txt" <<EOF
0 0
1000 0
0 1000
EOF
cat > "$tmp/ngon.txt" <<EOF
EOF
python3 -I - "$tmp/ngon.txt" <<'PY'
import math, sys
with open(sys.argv[1], "w") as f:
    for i in range(24):
        th = 2 * math.pi * i / 24
        f.write(f"{50 + 40*math.cos(th):.9f} {50 + 40*math.sin(th):.9f}\n")
PY
for f in "$tmp/tri.txt" "$tmp/ngon.txt"; do
  t=$("$REF" trap "$f")
  c=$("$REF" area "$f")
  printf '  %-9s trap %14.6f  cellcount %14.6f  差 %.2e\n' \
    "$(basename "$f")" "$t" "$c" "$(awk -v a="$t" -v b="$c" 'BEGIN{d=a-b;if(d<0)d=-d;printf "%.2e",d/(a>0?a:1)}')"
done

echo
echo "== 四个场景 =="
./build/run all "$REF" || rc=1

echo
echo "== 汇总 =="
cat out/summary.tsv

echo
echo "== 拒绝路径 =="
printf '0 0\n1 1\n2 2\n3 3\n' > "$tmp/line.txt"
./build/run one "$tmp/line.txt" "$tmp/line_out" "$REF"; r1=$?
[ "$r1" -eq 2 ] && echo "  共线：按预期拒绝（rc=2）" || { echo "  共线：期望 rc=2，实际 $r1"; rc=1; }

printf '0 0\n10 0\n10 10\n0 10\n' > "$tmp/sq.txt"
printf '5 5\n' > "$tmp/one.txt"
{ cat "$tmp/sq.txt"; cat "$tmp/one.txt"; } > "$tmp/sq_plus.txt"
./build/run one "$tmp/sq_plus.txt" "$tmp/sq_out" "$REF"; r2=$?
[ "$r2" -eq 0 ] && echo "  剥后剩一点：接受，并报告第 2 层不足三点" || { echo "  剥后剩一点：期望 rc=0，实际 $r2"; rc=1; }

echo
[ "$rc" -eq 0 ] && echo "全部通过" || echo "有失败"
exit "$rc"
