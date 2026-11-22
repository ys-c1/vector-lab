# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| render | — | 场景、栅格化、管线与 PGM 导出（cli 可出图） |
| winfit | demo.txt | 按目标占屏比例区间反解窗口四边，倒置/零面积拒绝并点名涉事边 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## winfit 窗口反解

老问题：固定窗口要么贴边（占屏 100%），要么留白过大。winfit 从内容包围盒与
目标占屏区间 `[lo, hi]` 反推窗口四边：取扩张系数 `k = 1/sqrt(rho_t)`，令
`W = k·bw, H = k·bh`，则 `rho = ba/(W·H) = rho_t`，再由锚点 `(ux, uy)`
分配两侧留白（0.5 居中，趋 0 内容靠左/下，趋 1 靠右/上）。

render CLI 两种用法：

```
固定窗口（老逻辑，产物逐字节不变）: cli scene.txt out.pgm w h scale
反解窗口（新逻辑）:                 cli scene.txt out.pgm w h scale lo hi rho_t ux uy
```

窗口倒置或面积为零时拒绝返回 `-6..-9`，并指出涉事边（左/右/下/上）。
