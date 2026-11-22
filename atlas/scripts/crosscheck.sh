#!/bin/bash
# 主实现与独立参照的逐字节交叉验证：
#   四组固定数据 + 随机实例，input/smart 两模式全部要求 IDENTICAL。
set -u
cd "$(dirname "$0")/.."
DIFF=../tools/diff.sh
TMP=build/xfuzz
mkdir -p "$TMP"
fails=0; total=0

run_pair() {
    local data=$1 W=$2 H=$3
    for mode in input smart; do
        total=$((total + 1))
        ./build/cli "$data" "$W" "$H" "$mode" > "$TMP/a.txt" 2>/dev/null
        ./build/ref "$data" "$W" "$H" "$mode" > "$TMP/b.txt" 2>/dev/null
        local res; res=$(bash "$DIFF" "$TMP/a.txt" "$TMP/b.txt")
        if [[ $res != IDENTICAL* ]]; then
            echo "MISMATCH $data $W $H $mode: $res"; fails=$((fails + 1))
        fi
    done
}

for g in A B C D; do run_pair "data/group$g.txt" 128 128; done

# 随机实例：保证可重复的独立生成器，尺寸不超过 100，确保不会触发拒判
python3 -I - <<'PY'
import random, os
r = random.Random(20261123)
os.makedirs("build/xfuzz", exist_ok=True)
for case in range(60):
    n = r.randint(1, 24)
    with open(f"build/xfuzz/c{case}.txt", "w") as f:
        for _ in range(n):
            f.write(f"1 {r.randint(4,100)} {r.randint(4,100)}\n")
PY

for c in "$TMP"/c*.txt; do run_pair "$c" 128 128; done

echo "cross-check: $total pairs, $fails failures"
exit $fails
