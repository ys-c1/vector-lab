# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| render | demo.txt (gamma) | 光栅化导出；按参照直方图反解伽马 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## render：灰度伽马反解

导出的灰度与参照“差一截”通常是一条没定标的伽马。`render/src/gamma.*`
按参照图的灰度直方图做非线性最小二乘，反解编码伽马：

    y = round(255 * (x/255)^(1/gamma))

残差定义为两条累积分布（源图按 gamma 映射后 vs 参照）的 RMS 距离，
上限 1e-3。在 log(gamma) 上全局粗扫 + 逐级加密；若残差为 0，则存在一段
零残差平台（区间内每个 gamma 都逐级映射出与参照相同的直方图），取平台
几何中心为解 —— 该点的重编码与参照**逐字节相同**（证明见 `gamma.h`）。
耗时只覆盖拟合（直方图 + 扫点），不含渲染。

线性导出是默认通路：`RExportGamma.enabled=0` 时 `pipeline_render_gamma`
与改动前的 `pipeline_render` 是一条代码，`baseline/*.pgm` 是改动前
生成的三组产物，`make demo` 会逐字节比对；显式 `gamma=1` 也走恒等表。

| 情形 | 结果 |
|---|---|
| gamma 参数 = 0 / 负数 / NaN / Inf | `GAM_E_GAMMA`，`field="gamma"` |
| 全黑 / 全白（源或参照单级） | `GAM_E_FLAT`，`flat_level=0/255`，`field="src"/"ref"`，明确“无法拟合” |
| 残差 > 1e-3 | 拟合照报，编码/写盘前拒绝 |

    make -C render test      # 46 项单元检查
    make -C render demo      # 四类图反解报告 + 拒绝路径 + 线性回归比对
    ./render/build/cli fit src.pgm ref.pgm out.pgm
    ./render/build/cli scene.txt out.pgm W H scale [--gamma G | --linear]

`gold/ref pgm-gamma <in> <out> <g>` 是独立的参照编码器（逐级 long double
幂运算），demo 用它生成参照图，缺失时退回 demo 内另一份独立编码。
