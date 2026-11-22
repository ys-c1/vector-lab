# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| svg | demo.pgm | SVG 路径解析与 PGM 填充；`make regress` 与基线逐字节比对 |
| contour | layers.txt | 多层嵌套轮廓逐层核验（顶点+面积同文件、gold 量面积、单调性、内存 ≤6×） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

svg 的多层新逻辑由编译开关 `SVGPATH_MULTILAYER` 控制（`make -C svg test-layers`
/ `demo-layers`），默认关闭；关闭时 `svg/demo.pgm` 与 `svg/baseline/demo.pgm`
逐字节相同。
