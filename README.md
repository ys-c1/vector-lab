# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值、固定 8 段细分与按二阶差分阈值的自适应细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

bezier 自适应细分（`bezier_adaptive_flatten` / `bezier_adaptive_segments`）：
在均匀底格上计算归一化二阶差分（除以 h²，与底格密度无关），弯曲度达到阈值
处拆段，再把短于 `min_len` 的相邻段并回。阈值非正或控制点退化（非有限坐标、
相邻控制点重合、端点重合）在入口报错并指出位置；返回码见 `src/bezier.h`。
不使用自适应接口时，`bezier_flatten` 的输出与原先完全一致。
