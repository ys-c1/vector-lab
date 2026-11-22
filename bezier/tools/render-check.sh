#!/bin/bash
# 关掉新逻辑后，整条产图管线必须与基线逐字节一致：
#   1) 旧固定 8 段接口导出的点序列 == baseline/fixed8_demo.txt
#   2) gold/ref.c 栅格化的 PGM        == baseline/fixed8_demo.pgm
#   3) 显式模式开关的 LEGACY_FIXED 输出 == 旧接口输出（点序列逐字节）
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
base="$here/baseline"
out="${1:-$here/build}"
mkdir -p "$out"

fail=0

# 1) 旧接口点序列
"$here/build/gen_baseline" > "$out/fixed8.txt" || { echo "gen_baseline failed"; exit 2; }
if cmp -s "$out/fixed8.txt" "$base/fixed8_demo.txt"; then
  echo "IDENTICAL legacy points $out/fixed8.txt $base/fixed8_demo.txt"
else
  echo "DIFF legacy points $out/fixed8.txt $base/fixed8_demo.txt"
  fail=1
fi

# 2) gold 栅格化后的 PGM
if [ -x "$here/../gold/build/ref" ]; then
  "$here/../gold/build/ref" pixels "$out/fixed8.txt" 64 64 > "$out/fixed8.pgm" || {
    echo "gold ref failed"; exit 2; }
  if "$here/build/pgmdiff" "$out/fixed8.pgm" "$base/fixed8_demo.pgm"; then :; else fail=1; fi
else
  echo "SKIP pgm compare: build gold first (make -C gold)"
  fail=1
fi

# 3) 模式开关：LEGACY_FIXED 与旧接口点序列逐字节一致
"$here/build/dump" legacy 0.5 "$out/fixed8_mode.txt" > /dev/null || {
  echo "dump legacy failed"; exit 2; }
if cmp -s "$out/fixed8_mode.txt" "$base/fixed8_demo.txt"; then
  echo "IDENTICAL mode-switch legacy $out/fixed8_mode.txt $base/fixed8_demo.txt"
else
  echo "DIFF mode-switch legacy $out/fixed8_mode.txt $base/fixed8_demo.txt"
  fail=1
fi

if [ $fail -eq 0 ]; then echo "render-check OK"; else echo "render-check FAIL"; fi
exit $fail
