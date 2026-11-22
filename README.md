# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| snap | bench_out/ | 反解等分总长，让等分点落在整点（取整误差 <= 半像素） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

反解等分：`make -C snap bench` —— 四组路径各报目标总长、取整误差、耗时；
`SNAP_DISABLE=1` 关闭新逻辑时输出与 `gold/ref points` 逐字节相同。详见 `snap/README.md`。
