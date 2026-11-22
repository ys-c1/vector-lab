# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| svg | report.txt | SVG 路径解析、按子路径绕数填充、奇偶/非零规则推导 |
| render | — | 场景、光栅、变换管线与 PGM 导出 |
| gold | — | 独立参照实现（面积、像素、弧长取点、SVG 填充） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

svg 模块另外两组目标：

- `make -C svg report`：跑单环、双环相离、双环嵌套、自交四组场景与一组
  同向嵌套对照，输出绕数分布、两种规则下的面积、错判像素与耗时；拒收
  样例打印子路径号。
- `make -C svg diff`：把主模块 PGM 与 gold 参照逐字节比对。
