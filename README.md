# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| homog | demo.txt, *.pgm | 四点反解单应（DLT + 最小二乘精化），退化拒绝，可切回仿射 |

`homog`：四对贴图角点解 8 自由度透视单应，角点残差走最小二乘精化；
四点共线或角点重合时拒绝并指出点对。`homog_set_perspective(0)` 关闭
新逻辑，退回 6 参数最小二乘仿射（旧路径逐字节不变）。

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
