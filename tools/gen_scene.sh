#!/bin/bash
# 生成确定性基准场景：24 个多边形、约 120 顶点，含重叠、内框、越界多边形。
# 用法：gen_scene.sh out.txt
set -eu
out=${1:?usage: gen_scene.sh out.txt}
awk 'BEGIN {
    srand(20261123);
    for (k = 0; k < 24; ++k) {
        n = 4 + int(rand() * 6);
        cx = rand() * 200 - 50;
        cy = rand() * 200 - 50;
        rad = 8 + rand() * 22;
        printf "p";
        for (i = 0; i < n; ++i)
            printf " %.3f %.3f", cx + (rand() * 2 - 1) * rad,
                                cy + (rand() * 2 - 1) * rad;
        printf "\n";
    }
}' > "$out"
