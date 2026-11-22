# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点
- `ref pgm-gamma <in> <out> <gamma>` 逐级 long double 幂运算编码 PGM:
  `y = round(255*(x/255)^(1/gamma))`，gamma 非正/非有限时指名参数拒绝

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对。
