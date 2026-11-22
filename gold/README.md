# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点

主模块的实现必须与之产出**逐字节相同**的文件，`tools/diff.sh` 比对；
像素级比对用 `tools/pgmdiff`（差异像素计数 + 叠图），端到端跑 `tools/render-check.sh`。

`ref pixels` 的 `<w>/<h>` 为零或负时立即退出（码 2）并指名 `param: width/height`。

