# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| render | build/rvec | 版本化场景文件 + parse/render/export/diagnose 子命令，详见 render/README.md |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
render 模块端到端测试：`make -C render check-cli`（六类错误矩阵、帮助快照、新旧入口字节级一致性）。
