#!/bin/bash
# 两后端基准：三档画幅各报毫秒、内存峰值、差异字节。
# 产物落 artifacts/（.gitignore 已忽略 build/，产物目录单独管理）。
set -eu
cd "$(dirname "$0")/../render"
B=../artifacts/bench
mkdir -p "$B"
[ -f "$B/scene.txt" ] || ../tools/gen_scene.sh "$B/scene.txt"

# 三档画幅：小 / 中 / 大。
W1=128;  H1=96
W2=512;  H2=384
W3=1280; H3=960

make build/cli build/bench >/dev/null

echo "== 三档画幅基准（子进程内每档重复 3 次取最小毫秒，RSS 为该进程峰值） =="
./build/bench "$B/scene.txt" "$B" "$W1" "$H1" "$W2" "$H2" "$W3" "$H3" | tee "$B/bench.txt"
rc=${PIPESTATUS[0]}

echo
echo "== 逐字节复核（PGM 全文件，含 P5 头） =="
for wh in "${W1}x${H1}" "${W2}x${H2}" "${W3}x${H3}"; do
    ../tools/diff.sh "$B/bench_${wh}_b1.pgm" "$B/bench_${wh}_b2.pgm"
done

echo
echo "== 开关校验：0 与越界必须停机并指出，返回码非 0 =="
for bad in 0 3 -1 abc; do
    if ./build/cli "$B/scene.txt" "$B/should_not_exist.pgm" 64 64 1.0 "$bad" >"$B/err_$bad.txt" 2>&1; then
        echo "FAIL: switch '$bad' was accepted"; rc=1
    else
        echo "rejected '$bad' (exit $?): $(cat "$B/err_$bad.txt")"
    fi
done
[ ! -e "$B/should_not_exist.pgm" ] || { echo "FAIL: output written on bad switch"; rc=1; }

exit "$rc"
