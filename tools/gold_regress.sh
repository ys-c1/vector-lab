#!/bin/bash
# 回归：原有扫描线后端在新后端关闭（不选择）时输出逐字节不变。
# 用 HEAD 版本的 render 源码在隔离目录构建旧 CLI，与当前默认 CLI 对拍。
set -eu
cd "$(dirname "$0")/.."
B=artifacts/bench
mkdir -p "$B/old"
SCENE=${1:-$B/scene.txt}

# 取 HEAD 的 render 模块到隔离目录。
git archive HEAD render | tar -x -C "$B/old"
make -C "$B/old/render" build/cli >/dev/null

make -C render build/cli >/dev/null

fail=0
for wh in "64x48" "200x150" "777x513"; do
    w=${wh%x*}; h=${wh#*x}
    "$B/old/render/build/cli" "$SCENE" "$B/old_${wh}.pgm" "$w" "$h" 1.0
    # 新代码默认（不传 backend）必须走原扫描线路径。
    ./render/build/cli "$SCENE" "$B/new_${wh}.pgm" "$w" "$h" 1.0
    tools/diff.sh "$B/old_${wh}.pgm" "$B/new_${wh}.pgm" || fail=1
    # 显式选择扫描线档位同样逐字节不变。
    ./render/build/cli "$SCENE" "$B/new1_${wh}.pgm" "$w" "$h" 1.0 1
    tools/diff.sh "$B/old_${wh}.pgm" "$B/new1_${wh}.pgm" || fail=1
done

rm -rf "$B/old"
if [ "$fail" -eq 0 ]; then
    echo "REGRESSION OK: scanline output byte-identical to HEAD"
else
    echo "REGRESSION FAILED"
fi
exit "$fail"
