#!/bin/bash
# 可复现地构建“改动前基线”CLI：
# 从基线提交导出 render/ 源码（不含本次浮点确定性改动），用历史默认标志编译。
# 产物: artifacts/baseline/cli-head
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
# 基线提交（改动前 render 模块所在提交），可用环境变量覆盖。
BASE_REF=${BASE_REF:-56501e411f2c0dfa3b5acbb494f59ce920af4c36}
OUT=$ROOT/artifacts/baseline
WORK=${TMPDIR:-$ROOT/artifacts/tmp}/baseline_src
rm -rf "$WORK"; mkdir -p "$WORK" "$OUT"

git -C "$ROOT" archive "$BASE_REF" render | tar -x -C "$WORK"
cc -I"$WORK/render/src" -O2 -Wall -Wextra -std=c11 \
   "$WORK"/render/src/*.c "$WORK/render/cli.c" -lm -o "$OUT/cli-head"
rm -rf "$WORK"
echo "baseline built: $OUT/cli-head (from $BASE_REF)"
