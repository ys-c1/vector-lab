# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| render | out.pgm | 场景渲染、光栅化、PGM 导出，以及反解容差的路径抽稀 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## render：按偏差上界反解抽稀容差

`render/src/simplify.c` 用 Douglas–Peucker 抽稀折线 / 闭合环，并对容差做二分：
在“实测最大偏差 ≤ 上界”的前提下，取能保住上界的最大容差，即保留点数最少。
保留点始终是原点的有序子序列（不改顺序、不插新点）。

- 直线、圆弧、锯齿、闭合环四类基准：`make -C render bench`（输出保留点数、
  最大偏差、耗时；任一类超界即非零退出）。
- 端到端校验（CLI 抽稀 + 独立 awk 复测 + 负容差/单点拒绝 + 旧全量导出逐字节比对）：
  `make -C render test`，或 `./tools/simplify-check.sh render/build/cli`。
- 命令行：

      render/build/cli scene.txt out.pgm W H SCALE                      # 旧全量导出
      render/build/cli scene.txt out.pgm W H SCALE --simplify 0.5 \
                       --dump kept.pts.txt                             # 抽稀导出

  `--simplify` 关闭时走原路径，输出与改动前逐字节一致
  （`render/fixtures/*.pgm` 为旧二进制冻结的基准）。
  容差为负、或某条路径点数少于二（闭合环少于三）时拒绝并打印原因。
