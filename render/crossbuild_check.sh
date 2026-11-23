#!/bin/bash
# 跨构建稳定性：同一套确定性源码，用不同优化级别与浮点契约分别编译，
# 固定路径(fp-on)产物必须逐字节一致——即便编译器开启 FMA 融合也不能改变结果。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
VD=${TMPDIR:-$ROOT/artifacts}/fp_crossbuild
rm -rf "$VD"; mkdir -p "$VD"
CONF=$HERE/configs/fp-on.conf
W=256; H=256
declare -A ROT=(
  [s1_square]=0.0
  [s2_rotated_square]=0.5235987755982988
  [s3_fractional_triangle]=0.19739555984988078
  [s4_multi_poly]=0.7853981633974483
)

# 1) 多种确定性构建（契约关闭，优化级别变化）
for opt in O0 O1 O2 O3; do
  gcc -Isrc -$opt -Wall -Wextra -std=c11 -ffp-contract=off -fno-fast-math \
      "$HERE"/src/*.c "$HERE"/cli.c -lm -o "$VD/det-$opt" || exit 2
done
# 2) 对抗构建：开启原生 FMA 与快速契约，验证固定路径仍不被融合影响
gcc -Isrc -O2 -Wall -Wextra -std=c11 -march=native -ffp-contract=fast \
    "$HERE"/src/*.c "$HERE"/cli.c -lm -o "$VD/fma-fast" || exit 2

fma_n=$(objdump -d "$VD/fma-fast" | grep -c vfmadd || true)
echo "fma-fast 二进制内 FMA 指令数(落在基线路径): $fma_n"

fail=0
for s in "${!ROT[@]}"; do
  src=$HERE/scenes/$s.txt; rot=${ROT[$s]}
  for b in det-O0 det-O1 det-O2 det-O3 fma-fast; do
    "$VD/$b" "$src" "$VD/$s.$b.pgm" "$W" "$H" 1.0 --rot "$rot" --conf "$CONF" >/dev/null
  done
  ref=$VD/$s.det-O2.pgm
  for b in det-O0 det-O1 det-O3 fma-fast; do
    if cmp -s "$ref" "$VD/$s.$b.pgm"; then
      echo "PASS $s vs $b 逐字节一致"
    else
      echo "FAIL $s vs $b 差异 $(cmp -l "$ref" "$VD/$s.$b.pgm" | wc -l) 字节"; fail=1
    fi
  done
done
exit $fail
