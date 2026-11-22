# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| keyframe | keyframe_spans.csv | 关键帧最小作用量中间点（加速度平方积分为目标） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## keyframe 模块

关键帧之间按最小作用量补中间点：固定各帧的时刻与位置，把加速度平方
沿时间积分 `S = ∫|q''(t)|^2 dt` 当目标，解出让整段最平滑的中间节点
（对称正定二次型，逐轴 Cholesky）。`make -C keyframe demo` 给四个动作
逐段的中间点、作用量、线性插值对照与耗时；四段降幅都超过三成。
时间倒置/相等、端点缺失分别拒绝并给出帧号；只有两帧时标记退化，
中点退回线性中点。关闭新逻辑（`-DKEYFRAME_MIN_ACTION=0`）走纯线性
插值，`make -C keyframe compat` 与独立旧实现逐字节比对。详见
`keyframe/README.md`。
