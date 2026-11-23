#!/bin/bash
# 抖动序列逐项比对：主实现导出的序列与 gold 独立参照逐字段比较。
# 用法: jitter-check.sh <cli> <ref> <seed> <n> <workdir>
# 任一项不一致（含行数、表头、单个 ux/uy）即判未完成，退出 1。
set -u
cli=$1; ref=$2; seed=$3; n=$4; wd=$5
main_f=$wd/main-seq.txt
gold_f=$wd/gold-seq.txt
mkdir -p "$wd"

# 拒绝规则：零种子或零长度必须先被拒，不允许进入比对。
# 用字符串比较，避免 bash 整数溢出（seed 是 uint64）。
if [ "$seed" = "0" ] || [ "$n" = "0" ] || [ "$n" -le 0 ] 2>/dev/null; then
  echo "FAIL: zero seed or non-positive length must be rejected, not compared"; exit 1
fi
"$cli" sequence "$main_f" "$seed" "$n" >/dev/null || { echo "FAIL main export"; exit 1; }
"$ref" jitter "$seed" "$n" > "$gold_f" || { echo "FAIL gold export"; exit 1; }

# 逐项比对：paste 后表头 6 字段，数据行 4 字段 = 主(ux uy) + 金(ux uy)
paste "$main_f" "$gold_f" | awk -v n="$n" -v seed="$seed" '
  NR == 1 {
    if ($1 != "jitter1" || $2 != seed || $3 != n || $4 != "jitter1") {
      printf("FAIL header: main=[%s %s %s] gold=[%s ...]\n", $1,$2,$3,$4); exit 1
    }
    next
  }
  {
    if (NF != 4) { printf("FAIL line %d malformed (NF=%d)\n", NR, NF); exit 1 }
    if ($1 != $3) { printf("FAIL item %d ux: main=%s gold=%s\n", NR-1, $1, $3); exit 1 }
    if ($2 != $4) { printf("FAIL item %d uy: main=%s gold=%s\n", NR-1, $2, $4); exit 1 }
  }
  END { if (NR != n + 1) { printf("FAIL length: got %d items want %d\n", NR-1, n); exit 1 } }
' || exit 1

echo "ITEM-IDENTICAL seed=$seed n=$n ($n pairs, $((2*n)) floats, all equal)"
