# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| arclen | points.txt | 累计弧长等分取点，与 gold 逐点比对 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

等分点基准：`make -C arclen bench` —— 小曲线、大曲线各跑一遍，
与 `gold/ref points` 逐字节 diff 并用 `tools/cmp_points` 校验累计偏差（容差 1e-6），
同时报告峰值内存与毫秒。
