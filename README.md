# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| atlas | build/*.txt | 图集货架打包（FFDH），面积/长宽比优先级重排与浪费率上界 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
