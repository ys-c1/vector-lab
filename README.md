# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓；`demo-area` 按目标覆盖面积反解宽度 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

stroke 的面积反解宽度由编译开关 `STROKE_AREA_FIT` 控制
（`make -C stroke test-area` / `demo-area`），默认关闭；关闭时
`demo.txt` 与 `figure.pgm` 和 `stroke/baseline/` 逐字节相同
（`make -C stroke regress` 核对）。
