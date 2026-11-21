# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| isect | demo.txt | 线段/折线相交与自交检测 |
| contact | demo.txt, contact_rows_*.csv | 曲线×线段的穿过/擦边接触分类、接触分类表导出 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## contact 模块

分清“穿过”（cross）与“擦边”（graze）两种接触，接口见 `src/contact.h`。

- 做法：每个采样区间先算区间弦方向与线段方向的叉积 `chord_cross`，再取区间两端与
  中点相对线段所在直线的有符号距离 `h0/hm/h1` 拟合二次式 `h(τ)=Aτ²+Bτ+C`，
  按判别式 `D=B²−4AC` 分辨：
  - `D>带宽`：两个实根，根两侧曲线换边判穿过，只贴近不换边判擦边；
  - `|D|≤带宽`：重根，相切（擦边），切点参数 `t` / `u` 一并给出；
  - `D<−带宽`：该区间无实根，候选被拒绝，分类表行内 `reason=discriminant_negative`、
    `field=discriminant`。
  带宽 `= 4|A|·tol`（即模型顶点到线段距离 ≤ tol）。
- 关掉分类（`flags` 不含 `CT_F_CLASSIFY`）时走旧路径：固定 8 段采样、
  逐弦套用与 `isect` 模块 `isect_seg` 相同的叉积算式，结果与现状**逐位一致**；
  `make -C contact compat` 直接链接 `bezier` / `isect` 源码做交叉验证。
- 控制点坐标非有限返回 `CT_E_CTRL` 并给出字段名 `c[k].x` / `c[k].y`；
  判别式为负可经 `CT_F_STRICT_DISC` 或单区间接口 `ct_classify_span` 拒绝，
  字段名 `discriminant`。错误码与字段名均记录在 `CtScan` / `CtRow`。
- `make -C contact demo`：8 与 64 段两档，逐场景输出穿过/擦边/判别负计数、
  相切 t 误差、耗时，并逐行与按公式手算的判别式核对；
  分类表导出为 `build/contact_rows_8.csv` / `build/contact_rows_64.csv`（含每行
  A/B/C/D、`chord_cross`、`h0/hm/h1`、切点参数、拒绝原因与字段），
  切点表导出为 `build/contact_hits_8.csv` / `build/contact_hits_64.csv`。
