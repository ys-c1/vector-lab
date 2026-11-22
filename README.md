# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| fit | demo.txt | 最小面积旋转包围盒：旋转卡尺反解最优角，无差别场景标记 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## render 自适应旋转

`render/build/cli scene.txt out.pgm w h scale [--fit]`：

- 不带 `--fit`：原零旋转路径，输出与历史产物逐字节一致。
- 带 `--fit`：先由 fit 模块求最小包围盒角，再按该角旋转几何并以旋转后的包围盒作为渲染窗口。角度合法域为 `[0, pi/2)`，越界拒绝（返回 -5）。
