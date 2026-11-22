#!/bin/bash
# 产物与参照逐字节比对；不一致时打印首个差异偏移。
set -u
a=$1; b=$2
if [ ! -f "$a" ] || [ ! -f "$b" ]; then
  echo "MISSING $a $b"; exit 2
fi
if cmp -s "$a" "$b"; then
  echo "IDENTICAL $a $b"; exit 0
fi
off=$(cmp -l "$a" "$b" 2>/dev/null | head -1 | awk '{print $1}')
echo "DIFF at byte ${off:-?}"; exit 1
