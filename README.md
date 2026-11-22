# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| render | PGM | 场景光栅化与 PGM 导出 |
| codec | .sbc | 子带能量反解量化的图像编解码（独立路径，不动 render 导出） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
codec 基准：`make -C codec demo`（四质量档的体积/PSNR/耗时与 PASS/FAIL 判定）。
