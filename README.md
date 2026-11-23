# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## render 并发渲染

`render` 支持把图像按行切块、分给 1..4 个线程渲染。每个块只写自己的行，
像素采样点只由坐标决定，所以并发产物与单线程逐字节相同。线程数越界或块行数
不能整除高度时直接拒绝并说明原因。

- 串行路径：`make -C render build/cli`，用法不变。
- 并发路径：`make -C render build/pcli`，
  `build/pcli <scene> <out.pgm> <w> <h> <scale> <threads> <block>`，
  线程数 1..4，块行数须整除高度。
- 验收：`make -C render verify`（线程数 1..4 各 10 次，检查十次字节一致、
  与串行逐像素相同，并检查非法配置会被拒绝）。
