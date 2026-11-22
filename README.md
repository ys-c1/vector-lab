# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| hull | test / test_simplify | 凸包、最小包围盒、面积保持型顶点精简 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## 凸包顶点精简（hull/src/simplify.[ch]）

下游按顶点数分档，全顶点凸包会全部挤入最高档。`hull_simplify(pts, n, ratio, out)`
按目标**面积保留比例**反解保留点数：

1. **共线点单独剔除并计数**：落在任意两点线段上的点（含重合点）先移除，计入
   `HSimp.collinear`，不参与后续面积计算。
2. **解析式估计**：正 n 边形面积比展开为 `A(n)/(πR²) = 1 − 2π²/(3n²) + O(n⁻⁴)`，
   反解 `n = π·√(2/(3(1−ratio)))`，夹在 `[3, 凸包顶点数]`，结果在 `est_count`。
3. **逐点剔除验证**：链表维护活动顶点，每步删除"耳朵三角形"面积最小的顶点，
   删除前用鞋带面积验证不跌破 `ratio·A₀`；破线即停。最终实际比例只会 ≥ 目标。
4. **拒并规则**：严格凸包顶点少于 3（全共线/点/线段）返回 `-4`；输入少于 3
   返回 `-3`；比例不在 `(0,1]` 返回 `-2`。

演示与基准（四组点集各报保留点数、面积比、单次耗时，外加一组全共线拒并）：

```
make -C hull simp
```

渲染管线的精简入口默认**关闭**：`pipeline_render` 与原实现逐字节一致；
显式调用 `pipeline_render_simplified` 或给 render CLI 传第 6 个参数才开启：

```
render/build/cli scene.txt out.pgm 64 64 1.0          # 老路径，全顶点
render/build/cli scene.txt out.pgm 64 64 1.0 0.95     # 新逻辑，面积比 >= 0.95
```

字节稳定性回归（从 HEAD 重建旧 CLI 逐字节比对四组场景）：

```
tools/verify_byte_stable.sh
```

面积结果可用独立参照交叉验证：`gold/build/ref area <poly>`（256×256 网格采样，
与主代码的鞋带公式不同算法）。
