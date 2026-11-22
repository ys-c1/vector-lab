#!/bin/bash
# 验证关闭精简逻辑（CLI 不传 area_ratio）时，渲染输出与 HEAD 基线逐字节一致。
# 用法：tools/verify_byte_stable.sh [scene_dir]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
scene_dir=${1:-$root/hull/scenes}
work=$root/artifacts/bytecheck
rm -rf "$work"
mkdir -p "$work/base" "$work/new"

git -C "$root" archive HEAD render | tar -x -C "$work/base"
make -C "$work/base/render" build/cli >/dev/null
make -C "$root/render" build/cli >/dev/null

rc=0
for scene in "$scene_dir"/*.scene; do
    name=$(basename "$scene" .scene)
    "$work/base/render/build/cli" "$scene" "$work/base/$name.pgm" 64 64 1.0 >/dev/null
    "$root/render/build/cli" "$scene" "$work/new/$name.pgm" 64 64 1.0 >/dev/null
    if cmp -s "$work/base/$name.pgm" "$work/new/$name.pgm"; then
        echo "IDENTICAL $name"
    else
        echo "DIFF $name"; rc=1
    fi
done
exit $rc
