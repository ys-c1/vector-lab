# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓；自交检测、裁环与交点表 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| hull | demo.txt | 凸包与最小外接矩形 |
| arclen | demo.txt | 折线弧长表与定点采样 |
| isect | demo.txt | 线段/折线相交与自交检测 |
| clip | demo.txt | 凸窗口多边形裁剪 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## stroke 模块

- `stroke_polyline`：旧接口，逐点行为保持不变。输出是每个顶点一对偏移点
  直接相连的“交叉梯子”，拐角处会自交。
- `stroke_resolve_polygon` / `stroke_resolve`：先按段两两求交（交点表按
  `(i,j)` 字典序编号为 `pair`），再把交点插入边序列、用栈式分解把闭环拆成
  简单环；与主环同向的环保留，反向绕入的环逐个裁掉并从 1 编号。
  分解满足 `原始面积 = 保留面积 + 裁掉面积`。
- `cut = 0`：不裁环，输出与 `stroke_polyline` 逐点一致（测试逐点核对）。
- 退化交点：交点为端点返回 `STROKE_E_ENDPOINT`，两段共线重叠返回
  `STROKE_E_COLLINEAR`，`bad_pair` 给出交点对号；`cut = 0` 时只记录不报错。
- 裁掉环可追溯：`loops[k].index` 为环号，`pair/ea/eb/at` 为代表切点，
  `hit_pairs` 列出界定该环的全部交点对号；`kept` 记录保留环。
- `make -C stroke bench`：4 与 32 拐点两组阶梯形（宽 1）统计交点/保留环/
  裁掉环数、面积误差与单次耗时；保留面积与扫描线精确参考核对，并把
  `build/hits.csv`（交点表）与 `build/cut_loops.csv`（裁环表）导出，
  可与手算逐行比对。
