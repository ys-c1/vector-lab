# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| hull | demo.txt | 凸包与最小外接矩形 |
| isect | demo.txt | 线段/折线相交与自交检测 |
| arclen | demo.txt | 曲线弧长 |
| clip | demo.txt | 凸/凹窗口多边形裁剪与凸拆分 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## clip 模块

- `clip_convex`：凸窗口 Sutherland–Hodgman 裁剪（窗口必须为凸多边形）。
- `clip_concave`：凹窗口裁剪。先把窗口凸拆分成互不重叠的凸块，逐块凸裁剪，
  再按面积求和合并（`split=0` 时跳过拆分，走旧路径，输出与原先逐顶点相同）。
- `clip_decompose`：耳切三角化 + Hertel–Mehlhorn 凸块合并。
  失败时返回负错误码并通过 `err_piece` 指出问题块号：
  `CLIP_E_PIECE_VERTS`（块顶点不足/退化）、`CLIP_E_NO_PIECES`（块数为零，如自交多边形）。
- `clip_pieces_csv`：块表导出 CSV（列：piece, vertex, x, y, piece_area），可写到任意 `FILE*`。
- `make -C clip bench`：8 顶点 U 形与 64 顶点梳齿两组基准，输出面积误差、块数、
  单次耗时，并把块表导出到 `build/pieces_*.csv`；面积与按凸块手算的并集核对。
