# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| adaptive | adaptive-eps*.txt, uniform-eps*.txt | 按曲率分配切段：给定偏差上界求最小总段数 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
另有两个共用工具：`gold/` 独立参照实现与 `tools/` 下的逐字节/逐点比对工具。

## adaptive 模块

“标记点间距靠估、弯道直道同段数”的修正：给定总偏差上界 `eps`，按每段曲率
分配切刀数，逐段等分落盘，再与独立参照点对齐。入口见 `adaptive/src/adaptive.h`。

- 分配：`n_i = max(1, ceil(len_i * sqrt(k_i/(8*eps))))`，`k_i = 1/R_i`。
  由等分模型偏差 `k*h^2/8 <= eps` 得连续下界，偏差相等即步长平方与曲率
  成反比、`n ∝ len*sqrt(k)`；拉格朗日乘子解 `n_i = len_i*sqrt(k_i/(8*eps*mu))`
  与逐段取整同形，总段数 `sum ceil(c_i)` 是“每段都不超上界”的最小整数解。
  直线段曲率为 0，固定 1 段。
- 上界复核：模型只求下界，最终每段用精确弓高 `R*(1-cos(len/(2nR)))` 复核并
  用加密采样实测输出折线，任何一段超上界即判 FAIL。
- 对比基线：均匀分配取“每段同样份数、能同时满足所有段的最小值”
  （`max_i n_i^exact * 段数`，对均匀分配最有利），自适应必须比它少两成以上。
- `make -C adaptive test`：146 项检查（下界/取整最优、拉格朗日比例、
  落点、拒绝路径）。
- `make -C adaptive check`：两档上界 `5e-4`（万分之五）与 `2e-5`（十万分之二），
  输出总段数、每段实际偏差、弯道偏差单表、耗时；每段落盘点再交给
  `gold/ref points` 对同一段加密折线重新等分，用 `tools/cmp_points` 逐点比，
  结果写进 `adaptive/out/`。

