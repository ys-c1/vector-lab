# render — 浮点确定性渲染

把场景（多边形 + 仿射放置）光栅化为 PGM。本模块在原有基线路径之外，新增一条
**浮点确定性路径**，解决跨编译配置时末位舍入不同导致的边缘像素抖动。

## 用法

```
cli scene.txt out.pgm w h scale [--rot R] [--conf fp.conf]
```

- 前 5 个位置参数与旧版完全一致；不传 `--conf` 时走基线路径，行为逐字节不变。
- `--rot R`：旋转角（弧度），缺省 0。
- `--conf`：浮点配置文件，见下。

## 配置项（三项缺一不可，漏写/非法/未知/重复一律拒绝并指出名字）

| 键 | 取值 | 含义 |
|---|---|---|
| `fp_deterministic` | `on`/`off` | 总开关 |
| `fma` | `on`/`off` | 乘加（FMA）合并；确定性开时必须 `off` |
| `eval_order` | `fixed`/`default` | 求值顺序；确定性开时必须 `fixed`，关时必须 `default` |

合法组合只有两组：

```
# 确定性（configs/fp-on.conf）
fp_deterministic=on
fma=off
eval_order=fixed

# 回落基线（configs/fp-off.conf）
fp_deterministic=off
fma=on
eval_order=default
```

拒绝时退出码 8，stderr 形如 `REJECT config 'fma': must be off when ...`，
明确点名出问题的配置项。`configs/bad-*.conf` 是各类非法样例。

## 确定性如何保证

1. **禁用乘加合并**：确定性二进制以 `-ffp-contract=off -fno-fast-math` 构建
   （见 Makefile `DET_CFLAGS`），`a*b+c` 不会被融合成单条 FMA。
2. **固定求值顺序**：`move_fixed` / `inside_fixed` / `sample_fixed` 把每个复合
   表达式拆成具名临时量、按书写顺序串行求值，不依赖编译器对同级运算的结合/重排。
   即使以 `-ffp-contract=fast -march=native` 重新编译，固定路径也无法跨语句融合，
   产物保持逐字节一致（见 `crossbuild_check.sh`）。

## 漂移根因（为什么需要它）

同一交叉点表达式 `a.x + (b.x-a.x)*t`，在 192000 次采样中，FMA 融合与分步舍入
有 53399 次（27.8%）产生不同的 double 末位；在构造的临界几何上，同一点的
`inside` 判定会随构建标志在 0/1 间翻转——即“边缘一会儿凑一像素一会儿差一像素”。
缝隙仅约 1 ULP（~1e-16），常规网格不易恰好命中，但跨编译器/优化级别/架构时
结果不可复现。固定路径从求值方式上根除该差异。

## 验证

```
make test              # 单元测试 + 配置校验测试（27 项拒绝/接受用例）
./crossbuild_check.sh  # 固定路径跨 O0/O1/O2/O3 与 FMA-fast 构建逐字节一致
./det_check.sh         # 四组场景双跑字节比对 + 关配置回落基线逐字节一致
./build_baseline.sh    # 从基线提交可复现地构建改动前 CLI
```

`det_check.sh` 对 `scenes/` 四组场景各渲染两次：两次结果有任何字节不同即判 FAIL；
并验证关闭确定性后，新二进制与改动前基线二进制（从提交 `56501e4` 导出源码编译）
产物逐字节相同。报告写到 `artifacts/fpdet/report.txt`，含每场景差异字节数、
两次耗时与配置项快照。
