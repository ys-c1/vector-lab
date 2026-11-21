# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## stroke 接合策略

`stroke_outline()` 在旧的每顶点偏移之外，按路径夹角在三种接法间选择并把结果写进闭合轮廓：

| 接法 | 触发（AUTO，按夹角 γ） | 轮廓处理 |
|---|---|---|
| 斜接 miter | γ ≥ 120° 且斜接长 ≤ 限值（默认 4×线宽） | 两偏移线交点 |
| 斜切 bevel | 60° ≤ γ < 120° | 入/出边两个偏移端点相连 |
| 圆角 round | γ < 60° | 绕拐点、半径半线宽的短弧（约 8°/段） |

- 旧接口 `stroke_polyline()` 与 `SJ_LEGACY` 模式逐点保持历史行为不变。
- 夹角为零（同向折返）返回 `-3`；斜接长度超限返回 `-4`，并经 `bad_index` 给出拐点号（0 基）。
- 每个拐点的夹角、选中接法、斜接比率、拒绝原因记录在 `SStrokeOpt.info` 选择表中。
- `make -C stroke bench`：8 / 64 拐点两组 × 四策略的面积、点数、耗时，选择表导出到 `bench/build/selection.csv`。
- `make -C stroke verify`：独立 Python 参考实现（`bench/ref_check.py`，`python3 -I` 运行）对全部组合逐点核对，并与 L 形手算的三种接法及导出的选择表比对。
