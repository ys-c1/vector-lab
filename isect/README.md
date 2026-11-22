# isect

线段求交与折线自交，外加自交点的去重（聚类）。

## 接口

`src/isect.h` 是原有接口，行为未变：

- `isect_seg` 单对线段求交，给出 `t/u` 与交点
- `isect_polyline` 只数两条折线之间的交点个数
- `isect_self` 返回第一对自交线段的编号

`src/cluster.h` 是新增的去重层：

- `isect_polyline_hits` / `isect_hits_self` 按原有遍历顺序（`i` 外层、`j` 内层）
  收集**每一个**交点，数值与 `isect_seg` 完全一致
- `isect_radius_for_missed` 由「最大允许漏合并数」反推半径
- `isect_cluster_by_radius` 按半径做单链接聚类，一簇一个簇心
- `isect_cluster_by_labels` 按显式标签聚类，用于校验
- `isect_missed_merges` 统计距离不超过容差、却落在不同簇的点对数
- `isect_cluster_centers` 每簇一个簇心（`count` 为该簇原始点数）
- `isect_resolve` / `isect_resolve_hits` 一站式入口，`mode = ISECT_MODE_RAW`
  时**只**产出原始交点列表，逐字节与改动前一致

## 半像素与漏合并

`ISECT_MERGE_TOL = 0.5`，一个常量同时承担两件事：

1. 距离不超过它的一对交点**必须**在同一簇，否则算一次漏合并
2. 簇心到簇内任一点的距离不得超过它，否则调用被拒绝

半径不是猜的：`isect_radius_for_missed` 把所有「可能改变漏合并计数的」
距离（即容差内的点到点距离）排序，用二分搜出**满足预算的最小半径**。
`max_missed = 0` 时该半径保证零漏合并。复现交点位置完全重合（距离 0）
的重复会在任何半径下合并，因此先按坐标归组，后续代价只随「不同位置数」
增长，而不随报告次数平方增长。

## 拒绝条件

| 情况 | 返回值 | 说明 |
|---|---|---|
| 半径为负 / NaN / 无穷 | `ISECT_ERADIUS` (-3) | 不动输出 |
| 簇为空 | `ISECT_ECLUSTER` (-4) | `err_cluster` 给出簇号 |
| 标签越界 | `ISECT_ECLUSTER` (-4) | `err_cluster` 给出该标签 |
| 漏合并超出预算 | `ISECT_EMISSED` (-5) | 单链接被迫串联超出容差 |
| 簇心偏差超过半像素 | `ISECT_EDEVIATION` (-7) | `err_cluster` 给出簇号 |

`ISECT_EINVAL` (-1) 与模块其余部分保持一致；`ISECT_ENOMEM` (-2) 表示分配失败。

## 测试与基准

```
make -C isect test     # 136 项检查，含三档规模的端到端与字节不变性
make -C isect bench    # 100 / 1000 / 10000 个交点：簇数、漏合并、偏差、耗时
make -C isect demo
```

`golden/raw_v.txt` 与 `golden/raw_square.txt` 是用改动前的代码生成的原始交点
列表；测试要求新实现（关掉聚类时）与它们逐字节相同。基准几何是一条直线
主干加一条沿主干来回的折线，每个低点让相邻两段各报一次交点——正是「同一
处吐出好几个交点」的形态。
