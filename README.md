# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| hull | demo.pgm / *.pgm | 单层凸包与凸层剥解（onion peeling） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## hull 凸层剥解

不再靠试层数：逐层剥凸包，把每层轮廓画进灰度 PGM，层 `i` 灰度
`255-16i`（下限 16），剥到本层面积相对最外层的占比首次低于目标为止。

- `hull_layers` 凸层分解（monotone-chain，仅剥边界点，内部点留下层）
- `hull_solve_layer` 反解层号；总层数用尽仍未达标返回 `-3`，调用方须拒绝
  并说明层号上限即总层数
- `hull_layers_pgm` 分层灰度 PGM
- 剥完剩余不足 3 点时由 `LResult.nrem` 报告，demo 打印 `NOTE`
- 全共线等退化点集构不成任何凸层，返回 `nlayers=0`，demo 拒绝并说明

演示：

```
make -C hull build/demo
./hull/build/demo gen artifacts/hull          # 随机/聚集/共线/双簇四组点集
./hull/build/demo run artifacts/hull/random.txt 0.25 out.pgm out.layers 240 240
```

一键全流程（四组反解报告、与 gold 参照逐字节比对、拒绝路径、旧逻辑稳定性）：

```
tools/hull_check.sh
```

旧逻辑（单层凸包，新逻辑关闭）由独立入口 `make -C hull legacy-demo`
产出 `demo.pgm`，其字节内容在新旧实现间保持稳定（脚本内置基线比对）。
