#!/bin/bash
# 小曲线、大曲线各跑一遍：与 gold/ref 逐点比对（字节 diff + 累计偏差），
# 并报告峰值内存（KB）与耗时（毫秒）。三种拒绝情形也各演示一遍。
set -u
cd "$(dirname "$0")/.."
ROOT=$(cd .. && pwd)
OUT=bench/out
mkdir -p "$OUT"

make -C "$ROOT/gold" >/dev/null || exit 1
make -C "$ROOT/tools" >/dev/null || exit 1
make points >/dev/null || exit 1

REF="$ROOT/gold/build/ref"
CMP="$ROOT/tools/build/cmp_points"
DIFF="$ROOT/tools/diff.sh"
TIME=/usr/bin/time

# 小曲线：三次贝塞尔取 12 个型值点，等分 64 点
awk 'BEGIN{for(i=0;i<12;i++){t=i/11;u=1-t;
  x=3*u*u*t*30+3*u*t*t*90+t*t*t*120;
  y=3*u*u*t*80+3*u*t*t*(-40)+t*t*t*50;
  printf "%.6f %.6f\n",x,y}}' > "$OUT/curve_small.txt"
# 大曲线：512 个型值点的波形折线，等分 50000 点
awk 'BEGIN{for(i=0;i<512;i++){x=i*4.0;
  y=250*sin(i*0.13)+120*cos(i*0.031);
  printf "%.6f %.6f\n",x,y}}' > "$OUT/curve_large.txt"

run_case() {
  name=$1; curve=$2; n=$3
  t0=$(date +%s%N)
  $TIME -f "%M" -o "$OUT/$name.ours.rss" ./build/points "$curve" "$n" > "$OUT/$name.ours.txt"
  t1=$(date +%s%N)
  $TIME -f "%M" -o "$OUT/$name.ref.rss" "$REF" points "$curve" "$n" > "$OUT/$name.ref.txt"
  t2=$(date +%s%N)
  echo "== $name (n=$n) =="
  $DIFF "$OUT/$name.ours.txt" "$OUT/$name.ref.txt" || return 1
  $CMP "$OUT/$name.ours.txt" "$OUT/$name.ref.txt" 1e-6 || return 1
  awk -v t0="$t0" -v t1="$t1" -v t2="$t2" \
      -v r1="$(cat "$OUT/$name.ours.rss")" -v r2="$(cat "$OUT/$name.ref.rss")" \
      'BEGIN{printf "  ours  %9.3f ms  peak %d KB\n",(t1-t0)/1e6,r1;
             printf "  ref   %9.3f ms  peak %d KB\n",(t2-t1)/1e6,r2}'
}

fail=0
run_case small "$OUT/curve_small.txt" 64 || fail=1
run_case large "$OUT/curve_large.txt" 50000 || fail=1

echo "== reject cases =="
printf "3 3\n3 3\n3 3\n" > "$OUT/curve_zero.txt"
./build/points "$OUT/curve_zero.txt" 10 2>&1; echo "  exit=$?"
./build/points "$OUT/curve_small.txt" 2 2>&1; echo "  exit=$?"
./build/points "$OUT/curve_small.txt" --spacing 100000 2>&1; echo "  exit=$?"

exit $fail
