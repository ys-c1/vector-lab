# clip 验证流程

复现“一刀 vs 两刀对不上”的排查工具链：每一步的多边形和面积单独落盘，
逐份与 `gold/ref` 的面积比对，并记录耗时、累计偏差和单步内存。

## 跑一遍

```sh
./verify.sh
```

产物全部在 `run-out/`（已 gitignore）：

```
run-out/two/
├── steps/step00_poly.txt  step00_area.txt   # 输入基线
├── steps/step01_poly.txt  step01_area.txt   # 第一刀之后
├── steps/step02_poly.txt  step02_area.txt   # 第二刀之后
├── gold/step01_poly.txt   step01_area.txt   # gold/ref 对同一多边形的面积
└── report.tsv
```

`report.tsv` 每步一栏：`ms`（本步毫秒）与 `cum_abs_dev`（对 gold 的累计
绝对偏差）各占一列，另有 `step_work_bytes` / `input_bytes` /
`work_over_4x_input`，单步工作内存超过输入四倍直接拒绝（退出码 2）。

## 驱动用法

```sh
make run
./build/run clip <poly.txt> <manifest.txt> <outdir> [ref 二进制]
./build/run cmp  <poly1> <area1> <poly2> <area2>
```

manifest 每行 `<窗口号> <窗口文件>`，窗口文件路径相对 manifest 所在目录。
窗口号重复、窗口为空、顶点不足三个、窗口退化（相邻顶点重合）全部拒绝，
报错信息标明是第几个窗口。

面积与 gold 不一致时只打印**第一处**（步号 + 窗口号 + 双方面积 + 偏差 +
容差）并立即停止，后续步骤不再生成。gold 是 256x256 数格子，容差取
`1e-3 + 1% × gold`；轴对齐整数几何下两边逐字节相同（`tools/diff.sh` 验证）。

## 文件

| 文件 | 作用 |
|---|---|
| `run.c` | 验证驱动（逐刀裁剪、落盘、gold 比对、计时、内存预算） |
| `tests/gen_data.py` | 纯 Python 生成全部测试几何与 manifest |
| `tests/fault_clip.c` | 故障注入：接口相同但工作内存膨胀，验证四倍预算拒绝 |
| `tests/budget_probe.c` | 扫描 n/wn 空间，证明真实实现 work/input ≤ 2 |
| `verify.sh` | 六个场景的端到端脚本 |

## 修掉的旧实现问题

1. **顺时针窗口把内部裁掉**：旧代码假设窗口逆时针，CW 窗口直接产出空多边形，
   一刀/两刀结果因此对不上。现在按有符号面积自动归一化窗口方向。
2. **超过 62 个顶点静默截断**：旧 `CPoly pts[64]` 写满即丢点，无任何错误码。
   现在按 `n+1` 紧凑分配，超容量返回 `-3`，不再静默。
3. **退化窗口无检测**：相邻顶点重合现在返回 `-2`。
4. **无内存核算**：新增 `clip_working_bytes()`，驱动按四倍输入预算守门。
