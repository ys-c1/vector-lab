# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## render：双光栅后端

同一场景有两条可切换的光栅后端，输出契约是**逐字节一致**：

| 开关 | 后端 | 路径 |
|---|---|---|
| 1（默认） | scanline | 逐像素逐采样点射线法（原实现，未改动） |
| 2 | tile-track | 32×32 分块，块内包围盒剔除 + 边界跟踪求跨 |

- CLI：`render/build/cli scene.txt out.pgm w h scale [backend]`；开关给 0、越界或非整数立即停机报错（退出码 8），不写输出文件。
- 瓦片后端初始化失败会释放已分配资源并返回 -5（`oom_test` 用 `--wrap` 逐次注入分配失败验证无泄漏）。
- 逐字节一致性：`make -C render test` 含 300 组随机场景 + 固定边界场景差分（1344 项检查）。
- 基准：`tools/bench.sh` 对三档画幅报两后端的毫秒（3 次取最小）、峰值 RSS、差异字节；任一差异字节非 0 即失败。
- 回归：`tools/gold_regress.sh` 取 HEAD 源码构建旧 CLI，对拍确认默认扫描线路径逐字节不变。

