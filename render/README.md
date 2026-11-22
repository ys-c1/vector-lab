# render

场景光栅化与导出：`scene` 多边形场景 → `raster` 超采样填充 → `export` PGM。

## 自适应超采样（adapt）

固定大 ss 慢、ss=1 有锯齿。`adapt` 在边缘锯齿指标达标的前提下用二分反解
最小 ss，合法域 `[1, 8]`。

- **指标**：以 32×32 细网格、与光栅器相同的 any-hit 规则（任一子样本命中
  即 on）为参照，只在边缘带（0 < 覆盖率 < 1 的像素）上统计候选结果与参照
  的不一致比例，范围 `[0, 1]`，越小越好。
- **二分**：区间 `[ss_lo, ss_hi]`，达标（指标 ≤ tol）压上界，否则抬下界。
  探针回调只收 `ss` 一个变量，场景/窗口/尺寸封在 ctx 里，保证每次只改 ss。
- **守卫**：
  - ss ≤ 0 或 > 8、空指针、阈值非法 → `ADAPT_ERR_ARG`；
  - 指标随 ss 上升（与任一已探测点配对违例）→ `ADAPT_ERR_NONMONO`，
    结果里给出双方 ss 与指标；
  - ss=8 仍不达标 → `ADAPT_ERR_NOPASS`。

`pipeline_render_opt(..., adaptive, tol, ...)`：`adaptive=0` 时走与
`pipeline_render` 完全相同的固定 ss=4 路径（共用 `render_core`），
输出逐字节不变；`adaptive=1` 时启用二分。

## 构建

- `make test`：单元测试（含 NOPASS / NONMONO / ss≤0 / 逐字节比对）
- `make demo`：直边、斜边、细缝、尖角四组，各报 ss、锯齿指标、逐探针耗时
- `make build/cli`：原命令行入口，行为不变
