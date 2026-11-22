#!/bin/bash
# 四组 × 两模式的墙钟耗时（每组重复 REPS 次，输出总耗时与单次均值）。
# 注：数据集为小图块集合，耗时以进程启动开销为主，仅作横向对比参考；
# 两模式走同一放置器，smart 仅多一次 qsort。
set -u
cd "$(dirname "$0")/.."
REPS=${REPS:-200}
make -s build/cli >/dev/null
printf "%-6s %-6s %10s %12s\n" group mode total_ms per_run_us
for g in A B C D; do
    for mode in input smart; do
        t0=$(date +%s%N)
        for _ in $(seq "$REPS"); do
            ./build/cli "data/group$g.txt" 128 128 "$mode" > /dev/null
        done
        t1=$(date +%s%N)
        ms=$(( (t1 - t0) / 1000000 ))
        us=$(( (t1 - t0) / REPS / 1000 ))
        printf "%-6s %-6s %10d %12d\n" "$g" "$mode" "$ms" "$us"
    done
done
