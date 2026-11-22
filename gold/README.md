# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点
- `ref fill <path.d> <w> <h> [ss] [auto|nonzero|evenodd] [out.pgm]`
  解析 SVG 路径并填充。与 svg 模块的逐点射线计数不同，这里按扫描线
  收集整行交点、排序，再按区间累加绕数；规则 `auto` 时独立重算符号面积
  推导（同向非零、混合或抵消则奇偶）。

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对。
