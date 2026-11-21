# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| isect | — | 线段/折线/自交求交；曲线-折线两阶段求交（细分粗交 + 局部精化，精化轨迹可导出 CSV），基准：`make -C isect bench` |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
