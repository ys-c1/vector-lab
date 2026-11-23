#!/bin/bash
# 双优化档（OPT=high / OPT=off）渲染产物逐像素比对。
# 对 scenes/ 下每组场景报告：像素差、两档各自耗时、两档各自编译告警数。
# 任一场景存在像素差则以非零退出。
set -euo pipefail
cd "$(dirname "$0")"

OPTS="high off"
SCENES="s1 s2 s3"
W=128; H=128; SCALE=1.0
REPS=3

mkdir -p build

# --- 构建两档，统计告警数 ---
declare -A WARN
for o in $OPTS; do
  rm -f build/cli
  log=$(make --no-print-directory build/cli OPT=$o 2>&1)
  echo "$log" | grep -E "error|warning" >&2 || true
  WARN[$o]=$(echo "$log" | grep -c "warning:" || true)
  mkdir -p build/opt-$o
  cp build/cli build/opt-$o/cli
done

# --- 渲染 + 逐像素比对 ---
printf "%-6s %10s %12s %12s %10s %10s\n" 场景 像素差 "high耗时ms" "off耗时ms" "high告警" "off告警"
total_diff=0
for s in $SCENES; do
  declare -A BEST
  for o in $OPTS; do
    out=build/opt-$o/$s.pgm
    best=999999999
    for r in $(seq $REPS); do
      t0=$(date +%s%N)
      build/opt-$o/cli scenes/$s.txt $out $W $H $SCALE >/dev/null
      t1=$(date +%s%N)
      ms=$(( (t1 - t0) / 1000000 ))
      [ $ms -lt $best ] && best=$ms
    done
    BEST[$o]=$best
  done
  # 头部一致前提下 PGM 每字节即每像素
  ha=$(head -c 32 build/opt-high/$s.pgm | head -3)
  hb=$(head -c 32 build/opt-off/$s.pgm | head -3)
  if [ "$ha" != "$hb" ]; then
    diff=-1
  else
    diff=$(cmp -l build/opt-high/$s.pgm build/opt-off/$s.pgm 2>/dev/null | wc -l)
  fi
  total_diff=$((total_diff + diff))
  printf "%-6s %10s %12s %12s %10s %10s\n" "$s" "$diff" "${BEST[high]}" "${BEST[off]}" "${WARN[high]}" "${WARN[off]}"
done

if [ "$total_diff" -ne 0 ]; then
  echo "FAIL: 存在像素差（总计 $total_diff）" >&2
  exit 1
fi
echo "OK: 三组场景两档产物逐像素一致（差异为 0）"
