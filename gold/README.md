# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点
- `ref layers <scene.txt> <w> <h>` 图层依赖场景：显式栈迭代 DFS 环检测、多趟松弛求
  最长层级、位掩码就绪集定渲染顺序（与主模块的递归 DFS/拓扑单趟松弛/计数数组 Kahn
  是两套实现），输出 P5 PGM

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对。
