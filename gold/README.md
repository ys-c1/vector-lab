# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点
- `ref xform <pts> <a b c d e f>` 仿射变换点文件，long double 逐点直算，行数不限

点文件格式：首行点数 n，随后 n 行 "x y"。

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对；
浮点逐点比对（含差异上界）用 `tools/ptdiff.c`，见 `xform` 模块的 `make bench`。
