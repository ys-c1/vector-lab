# gold

独立参照实现，算法与主模块不同：

- `ref area <poly>` 用 256x256 超采样数格子求面积
- `ref pixels <poly> <w> <h>` 用 8x8 超采样出 PGM
- `ref points <line> <n>` 按累计弧长等分取点
- `ref layers <points> <target>` 凸层剥解：gift wrapping（Jarvis march）逐圈取凸包、边点检测剥层，输出到达标层的层文件
- `ref lpgm <points> <target> <w> <h>` 同一参照算法产出凸层灰度 PGM

主模块的凸层用 monotone-chain + 边界点剔除实现，与参照算法不同；两者产出的
层文件与 PGM 必须**逐字节相同**，由 `tools/diff.sh` 比对，
全流程见 `tools/hull_check.sh`。
