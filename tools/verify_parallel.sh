#!/bin/bash
# 并发可复现性验收：
#   以改动前的串行实现产物为基准（若提供 RP_BASELINE），否则用当前串行路径产物；
#   线程数 1..4 各跑 10 次，检查 10 次产物字节一致、且与单线程逐像素相同；
#   线程数越界或块行数不能整除高度时必须拒绝并说明原因。
# 用法：RP_BASELINE=旧产物.pgm tools/verify_parallel.sh [场景] [宽] [高] [缩放] [块行数]
set -u
cd "$(dirname "$0")/.." || exit 2

SCENE=${1:-render/examples/scene.txt}
W=${2:-256}
H=${3:-256}
SCALE=${4:-1.0}
BLOCK=${5:-16}
RUNS=${RP_RUNS:-10}
BASE=render/build/serial.pgm
OUT=render/build/parallel
PY="python3 -I"

make -s -C render build/cli build/pcli || exit 2
mkdir -p "$OUT"

if [ $((H % BLOCK)) -ne 0 ]; then
  echo "failed: 块行数 $BLOCK 不能整除高度 $H，无法均分，已停止"
  exit 2
fi

echo "== 基准：单线程串行路径 =="
t0=$(date +%s%N)
./render/build/cli "$SCENE" "$BASE" "$W" "$H" "$SCALE" >/dev/null || { echo "failed: 串行渲染失败"; exit 2; }
t1=$(date +%s%N)
SERIAL_MS=$(( (t1 - t0) / 1000000 ))
SERIAL_HASH=$(sha256sum "$BASE" | awk '{print $1}')
echo "serial: sha256 $SERIAL_HASH  time ${SERIAL_MS}ms"

if [ -n "${RP_BASELINE:-}" ]; then
  if cmp -s "$BASE" "$RP_BASELINE"; then
    echo "baseline: 串行路径产物与改动前逐字节一致"
  else
    echo "failed: 串行路径产物与改动前不一致（$RP_BASELINE）"
    exit 2
  fi
fi

fail=0
printf '\n%-8s %-6s %-10s %-14s %-8s %-10s %s\n' 线程数 块行数 差异像素 产物哈希 中位ms 十次一致 与串行一致
for T in 1 2 3 4; do
  hashes=""
  diffpx=-1
  times=""
  same10=yes
  same_serial=yes
  for r in $(seq 1 "$RUNS"); do
    f="$OUT/t${T}_r${r}.pgm"
    t0=$(date +%s%N)
    if ! ./render/build/pcli "$SCENE" "$f" "$W" "$H" "$SCALE" "$T" "$BLOCK" >/dev/null; then
      echo "failed: 线程数 $T 第 $r 次运行失败"; exit 2
    fi
    t1=$(date +%s%N)
    times="$times $(( (t1 - t0) / 1000000 ))"
    h=$(sha256sum "$f" | awk '{print $1}')
    if [ -z "$hashes" ]; then
      hashes=$h
    else
      [ "$h" != "$hashes" ] && same10=no
    fi
    d=$($PY tools/pgmdiff.py "$BASE" "$f")
    if [ "$d" = "identical" ]; then
      [ "$diffpx" -lt 0 ] && diffpx=0
    else
      same_serial=no
      case "$d" in
        pixels\ *) p=${d#pixels }; diffpx=${p%% *} ;;
      esac
    fi
  done
  med=$(echo "$times" | tr ' ' '\n' | grep -v '^$' | sort -n | sed -n "5p")
  [ "$same10" != yes ] && fail=1
  [ "$same_serial" != yes ] && fail=1
  printf '%-8s %-6s %-10s %-14s %-8s %-10s %s\n' \
    "$T" "$BLOCK" "$diffpx" "${hashes:0:12}" "$med" "$same10" "$same_serial"
done

echo
echo "== 停机检查：非法配置必须拒绝 =="
expect_fail() {
  desc=$1; th=$2; bl=$3
  out=$(./render/build/pcli "$SCENE" "$OUT/bad.pgm" "$W" "$H" "$SCALE" "$th" "$bl" 2>&1)
  rc=$?
  if [ $rc -ne 0 ] && [ ! -f "$OUT/bad.pgm" ]; then
    echo "ok   $desc -> rc=$rc: $out"
  else
    echo "failed $desc：应当拒绝（rc=$rc）"
    fail=1
  fi
}
rm -f "$OUT/bad.pgm"
expect_fail "线程数 0" 0 "$BLOCK"
expect_fail "线程数 5" 5 "$BLOCK"
expect_fail "线程数 -1" -1 "$BLOCK"
BL=$(( BLOCK + 1 ))
if [ $((H % BL)) -eq 0 ]; then BL=$(( BL + 1 )); fi
if [ $((H % BL)) -ne 0 ]; then
  expect_fail "块行数 $BL 不整除 $H" 2 "$BL"
fi
expect_fail "块行数 0" 2 0

echo
if [ $fail -ne 0 ]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS（四档线程数各 $RUNS 次，产物字节一致且与串行逐像素相同）"
exit 0
