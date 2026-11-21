# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| hull | demo.txt | 凸包与最小外接矩形 |
| arclen | demo.txt | 折线弧长表与定点采样 |
| isect | demo.txt | 线段/折线相交与自交检测 |
| clip | demo.txt | 凸多边形裁剪，支持带洞窗口（外环裁剪 + 逐洞反向裁剪 + 洞边界并入） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
