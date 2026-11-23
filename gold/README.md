# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点
- `ref jitter <seed> <n>` 输出确定性抖动序列（SplitMix64，头 `jitter1 <seed> <n>` 加 n 行 `ux uy`）；seed 为 0 或 n 非正即拒绝

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对。
抖动序列改用 `tools/jitter-check.sh` 做**逐项**比对：任一 ux/uy、行数或表头不同即判失败；篡改一项即未通过。
