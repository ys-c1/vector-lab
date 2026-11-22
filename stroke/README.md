# stroke

折线描边到多边形轮廓。

## 固定宽度（默认，旧路径）

```sh
make test     # 6 项单元检查
make demo     # demo.txt：每顶点一对偏移点
make figure   # figure.pgm：固定宽度 3.0 的位图产物
make regress  # demo.txt / figure.pgm 与 baseline/ 逐字节比对
```

`stroke_polyline()` 的行为与历史版本完全一致：`stroke.c`、`demo.c`、
`test.c` 一字未改，`baseline/` 里的产物由 HEAD 的源码直接编出，`regress`
逐字节核对。

## 按目标面积反解宽度（新逻辑，编译开关 `STROKE_AREA_FIT`）

描边“宽了压画面、窄了露底”，这里把宽度当未知数，反解使**覆盖面积**
达到给定目标的宽度。

```sh
make test-area   # 57 项单元检查（正常/拒绝/自交/单调性）
make demo-area   # 四组路径各报宽度、覆盖面积、误差、耗时
make verify      # 用逐像素光栅计数独立核验四组路径的覆盖面积
```

新逻辑默认不参与 `test` / `demo`。`stroke_area.c` 在开关关闭时是空编译
单元；`stroke.h` / `stroke.c` 未改动，所以固定宽度下的产物逐字节不变
（`make regress` 验证）。

### 面积模型（对宽度积分）

覆盖面积取“中心线 ⊕ 半径 h=width/2 圆盘”的并集面积。对不自交的中心线，
把面积对宽度从 0 积到 w：

```
A(w) = ∫₀ʷ [ L + (cap − C)·t/2 ] dt = L·w + (cap − C)·w²/4
```

| 符号 | 含义 |
|---|---|
| `L` | 中心线总长 |
| `cap` | 开放路径 π（两端各一个半圆）；闭合路径 0 |
| `C` | Σ (tan(φ/2) − φ/2)，φ 为无符号转角，扣掉拐角处重复计入的圆盘扇形 |

反解就是解 `L·w + k·w² = target`（`k=(cap−C)/4`）的一元二次方程，取面积
随宽度递增的那支正根。模型完全解析，不需要迭代。

模型的适用边界：带宽彼此重叠、中心线自交、或路径接近 180° 折返时，
“并集面积 = 各胶囊面积之和减拐角重叠”不再成立。程序在反解后用逐行
胶囊并集实测复核，偏差超过 0.5% 就自动改走实测分支。

### 实测（独立算法）

`stroke_area_measure()` 逐水平线求每个胶囊体（矩形 + 两端半圆）的交叉
区间，整行求并得到覆盖长度。积分在每段圆弧端头的切点 y=c.y±h 处切分，
再对每段做自适应 Simpson（递归到相对 1e-9），所以宽度从 1e-4 到几十都
稳定。与二次模型是两套完全不同的推导。求解宽度时对实测面积做二分
（面积随宽度严格递增）。

`demo-area` 里库内面积与蒙特卡洛采样的相对差在 0.01%–0.07%；
`verify` 另用逐像素光栅计数交叉核对，带宽足够网格分辨时相对差 < 0.03%。
四套算法（二次模型、逐行并集、蒙特卡洛、光栅计数）互差远低于目标的 1%。

> 只在几何进入模型失效区（细长闭合环、带宽大面积重叠）时，二次模型解
> 才会偏离；此时程序自动改走实测分支，demo 的“宽度（实测反解）”一栏
> 即表示走了实测。

### 自交路径

中心线段两两求交（端点接触不算），命中时在 `SAreaFit` 里回填
`self_intersecting=1` 与首个自交的段号 `self_seg_a/b`、交点坐标，
并**一律走实测分支**（模型假设不自交）。报告里单独标出。

### 拒绝规则

| 情况 | 返回码 |
|---|---|
| 宽度为负（`w_min<0`）或上下界非法 | `STROKE_AREA_E_WIDTH` |
| 路径长度为零（全部点重合） | `STROKE_AREA_E_ZERO_LEN` |
| 目标面积非正/非有限 | `STROKE_AREA_E_TARGET` |
| 目标面积超出 `[w_min,w_max]` 可达范围 | `STROKE_AREA_E_NO_SOLUTION` |
| 实测二分未收敛 | `STROKE_AREA_E_CONVERGE` |
| 指针为空 / 非有限坐标 / 点数越界 | `STROKE_AREA_E_ARG` |

错误码文字（含参数名）由 `stroke_area_strerror()` 给出。

### 接口

```c
int stroke_area_fit(const SPoint *pts, int n, int closed, double target_area,
                    double w_min, double w_max, SAreaFit *out);
double stroke_area_measure(const SPoint *pts, int n, int closed, double width, int rows);
double stroke_area_model(const SPoint *pts, int n, int closed, double width,
                         double *length, double *quad);
```

`SAreaFit` 同时回填宽度、覆盖面积、相对误差、耗时（`ms`）、中心线长、
二次项、模型宽度/面积、实测迭代次数与自交信息。
