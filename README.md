# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| fit | build/demo-*.txt | 角点落点的刚性变换拟合（线性方程组一次解出） |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## fit：角点落点拟合

把角点对不上的问题写成线性方程组一次解出，而不是逐个试、改一个偏另一个。

模型为刚性相似变换（单位缩放），三个未知量 `(rot, tx, ty)`：

```
X = cos(rot)·x - sin(rot)·y + tx
Y = sin(rot)·x + cos(rot)·y + ty
```

令 `a=cos(rot), b=sin(rot)` 线性化为 `(a,b,tx,ty)`，每个角点给出两条
线性方程，四角点共 8 条方程、一张 4 列设计矩阵，组法方程后选主元高斯
消元一次解出；再单位化 `(a,b)` 施加刚性约束反算转角，回代算每个角点残差。

- 残差在像素坐标系下，半像素容差即 `0.5 px`，超差返回 `FIT_E_RESIDUAL`。
- 方程退化（源角点全部重合、转角不可观测，设计矩阵秩=2）返回
  `FIT_E_DEGENERATE`，并指出互相冲突的两条方程（角点下标 + x/y 行）。
- 未知量越界（隐含缩放、转角、平移）返回 `FIT_E_BOUNDS` 并指出越界项。
- 源角点共线在该模型下**不**秩亏；数据不相容时体现为残差超差。

命令：

```
make -C fit test            # 40 项单元检查
make -C fit demo            # 新逻辑（线性方程组），六组角点各报未知量/残差/耗时
make -C fit demo-trial      # 旧逻辑（逐个试网格搜索，-DFIT_NO_LINEAR 关闭新逻辑）
make -C fit bytecheck       # 两模式比对：状态/冲突逐字节一致，数值按分级容差一致
make -C fit render-regress  # render/ 源码零改动、渲染产物逐字节可复现
```

新逻辑仅存在于新增的 `fit/` 模块内，不触碰任何既有模块；用
`-DFIT_NO_LINEAR` 编译即整体回退到旧的逐个试逻辑，既有渲染产物逐字节不变。
