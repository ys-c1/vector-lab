# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积（采样误差 1e-3 量级）
- `ref trap <poly>` 按顶点 y 切条做梯形积分求面积（简单多边形上精确）
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对。
