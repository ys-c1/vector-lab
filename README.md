# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## 可复现构建

所有模块（arclen bbox bezier clip dash gold hull isect render stroke svg xform）
共用同一组构建开关，只依赖 C 编译器和 libm，无需安装任何额外库：

- `OPT`：优化档位，只接受 `0` 和 `2`（默认 `2`）。写错（如 `OPT=3`）会立即
  停止并指明写错的值。
- `STRICT`：默认 `1`，追加 `-Werror -ffp-contract=off`——零告警，且禁止
  乘加融合（FMA contraction），消除换机器后 `a*b+c` 舍入方式不同导致的
  像素漂移。`STRICT=0` 回到旧版选项 `-O2 -Wall -Wextra -std=c11`，
  产物与旧版字节相同。

渲染可复现性验证（render 模块）：

```sh
make -C render repro   # OPT=0 与 OPT=2 各编一份 CLI，渲染 render/scene.txt，
                       # 逐字节比较两份 PGM，零差异才通过
```
