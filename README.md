# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| svg | demo.pgm / demo_*.pgm | 路径解析与奇偶填充；椭圆弧参数化 |
| xform | — | 仿射变换 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## svg 模块

旧行为：`svg_parse` 只认 `M/m L/l Z/z`，`svg_fill` 对纯线段路径逐行
交叉奇偶填充。这两条路径的输出与改动前逐字节一致，`make -C svg compat`
把 HEAD 版本源码原样编进来同进程比对（解析段表、像素缓冲 `memcmp`，
以及 A/C/不以 M 开头等旧错误码）。

### 椭圆弧解析

`svg_parse_flags(d, &p, SVG_F_ARCS)` 打开 `A/a`：

```
A rx ry xrot large-arc-flag sweep-flag x y
```

`xrot` 单位为度，相对命令 `a` 的 x/y 相对当前点。解析期只检查数字与
有限性（非有限值返回 `SVG_E_NONFINITE`，字段名指出是哪个参数）；几何
合法性留到参数化时检查，因为兼容整圆模式不看 `ry/xrot/large/sweep`。

### 参数化

`svg_arc_frame` 按 SVG 1.1 附录 F.6.5 解出

- 圆心 `center`
- 生效半径 `rx/ry`（负值按 F.6.2 取绝对值）
- 起始参数角 `theta1`、有符号扫过角 `dtheta`（`sweep=1` 时为正）、
  `theta2 = theta1 + dtheta`（不折回，便于看方向）

`large` 与 `sweep` 的组合决定取两组候选圆心中的哪一个；大弧标志不再
额外做“|Δθ| ≥ π”的自检——参数退化时 `|Δθ| = π` 是合法极限。

### 折线与径向偏差

每段弦按参数角均分，`n = ceil(|Δθ| / hmax)`，`hmax = sqrt(8·tol / rmax)`，
`rmax = max(rx,ry)`。于是弦上任意点满足

```
| 到圆心距离 − 该方向椭圆半径 | <= rmax·(|Δθ|/n)² / 8 <= tol
```

用 `max(rx,ry)` 换掉逐点曲率半径，得到与角度无关的统一直观上界；
返回值里的 `bound` 就是这个上界的实际值。`svg_arc_chords` 同时给出两档：

- `nplus = ceil(q)`：余量档，实测偏差不超过上界；
- `nminus = max(1, floor(q))`：紧凑档，少一段，偏差约放大
  `(n/(n−1))²` 倍，可能略超上界。

`svg_arc_polyline` 输出 `n+1` 个端点，首末端精确落在弧的起止点上。

### 填充开关

弧段在兼容模式 `SVG_ARC_CIRCLE` 下“只会画整圆”：以弧起点为圆心、
`rx` 为半径画整圆，忽略 `ry/xrot/large/sweep` 与弧终点。这就是开关
为假时的画面，`svg_fill` 与 `svg_fill_ex(..., SVG_ARC_CIRCLE, ...)` 都走它，
两者像素缓冲逐字节一致（测试用 `memcmp` 核对）。

`SVG_ARC_ELLIPSE` / `SVG_ARC_ELLIPSE_MIN` 才按解出的圆心、起始角与扫过
方向细分。`svg_fill_ex` 返回 `SvgFillStats`（弧数、弦数、两档计数之和、
最大上界），`svg_arc_log()` 给出逐弧记录（圆心、起止角、半径、两档弦数、
上界），用于填表核对。

### 错误定位

所有错误都可用 `svg_last_error_detail()` 取到 `seg`（0 基段号，`M` 不占号）、
`field`（字段名）与 `value`：

| 情形 | 返回码 | field |
|---|---|---|
| 半径为零 | `SVG_E_RADIUS` | `rx` / `ry` |
| 半径不足以连接起止点 | `SVG_E_RADIUS` | `rx/ry`，value 为所需放大系数 |
| 起止点重合 | `SVG_E_COINCIDENT` | `x/y` |
| 旋转角 \|xrot\| > 90° | `SVG_E_XROT` | `xrot` |
| 参数非有限 | `SVG_E_NONFINITE` | 参数名 |

### 演示

`make -C svg demo` 输出：

- 原基准路径的 `demo.pgm`（与改动前逐字节相同）；
- 椭圆弧参数表：逐段圆心、生效半径、`θ1`、`Δθ`、两档弦数、径向偏差
  上界、与独立手算（把点代回椭圆方程）的实测偏差、端点方程残差；
- 两档对比：整圆兼容、椭圆余量档、椭圆紧凑档的像素数、相对差与耗时；
- 错误表：段号、返回码、字段与说明；
- `demo_circle.pgm` / `demo_ellipse_plus.pgm` / `demo_ellipse_min.pgm`。

`make -C svg test` 覆盖解析开关、参数化手算值（圆心/θ1/Δθ 与 Python
按 F.6.5 逐式算得的数值对照）、偏差上界、两档计数、三类错误与开关
为假时的逐字节一致性。`make -C svg compat` 把 HEAD 版本的 `svgpath.c`
原样编进来同进程对比，纯线段路径的像素缓冲与旧实现逐字节一致。
