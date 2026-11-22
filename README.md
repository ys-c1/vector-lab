# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| sweep | sweep_groups.csv 等 | 扫描线候选对：逐事件处理 vs 合并窗口，反解窗口宽度 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## sweep 模块

逐事件扫描线处理完每个事件就枚举一次活动段对，同一时刻的多个事件会把候选对
重复放大。`sweep` 把相邻事件时刻按间隔 <= W 链式合并，每组取活动段最多的时刻
做一次快照枚举，并按事件时间差分布反解最优 W：

- `sweep_legacy`：逐事件处理。关掉新逻辑（`flags` 不含 `SW_F_MERGE`）时走这条，
  交点表与独立参照实现逐字节相同（`make -C sweep compat` 链接 `isect` 真实源码比对）。
- `sweep_run`：合并模式。每组只在代表时刻枚举；超出一个时刻的窗口会漏掉“只在
  窗口内部短暂共存”的段对，`SW_F_AUDIT` 会把窗口并集里未被快照覆盖的段对逐对
  做真实相交测试，发现漏交即标 `SW_UNSAFE` 并给出段对、交点与组区间
  （`sweep_format_reject`）；这类窗口在 `sweep_choose` 里一律拒收。
- `sweep_gap_dist` / `sweep_estimate_pairs`：只读事件时间差分布与各时刻活动计数，
  估出窗口下的候选对数，不枚举段对；`sweep_choose` 以 {0} ∪ 全部正间隔为候选，
  在审计安全的窗口里取总代价最小且候选对至少降两成者。
- 事件时间差为零的段（`x0 == x1` 的竖直段）不进代表时刻快照，按各自唯一时刻
  单独成桶，其候选对与交点数在统计里单列（`zero_segs` / `cand_zero` / `zero_buckets`）。
- `make -C sweep demo`：四组线段集分别报时间差分布、逐档窗口表、选定窗口、
  候选对数、耗时；`make -C sweep compat` 复核逐事件交点表逐字节不变，
  `make -C sweep check-baseline` 复核基线文件。

