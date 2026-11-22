# vector-lab

矢量图形小模块集合，每个模块独立构建，自带测试与演示产物。

| 模块 | 产物 | 内容 |
|---|---|---|
| bezier | demo.txt | 三次贝塞尔求值与按容差细分 |
| stroke | demo.txt | 折线描边到多边形轮廓 |
| dash | demo.txt | 虚线切分与相位控制 |
| bbox | demo.txt | 点集包围盒与旋转盒 |
| svg | demo_<W>x<H>.pgm | SVG 路径解析、viewBox 单位换算与 PGM 填充 |

每个模块：`make -C <模块> test`，演示：`make -C <模块> demo`。

## svg：viewBox 单位换算

`svg_fill_mapped` 在 `flags` 含 `SV_F_VIEWBOX` 时按 viewBox 与目标尺寸
换算：`sx = w / viewBox.w`、`sy = h / viewBox.h`，偏移 `tx = -minx*sx`、
`ty = -miny*sy`；光栅化时采样点做像素中心校正，像素 `(px,py)` 的判定点
是设备坐标 `(px+0.5, py+0.5)` 反算回的用户坐标。

- 边缘像素偏差上界：轴向半像素，二维欧氏 `edge_px = 0.5*sqrt(2)`，
  换算回用户单位为 `edge_user`。
- viewBox 宽高为零/负 -> `SV_E_VIEWBOX`，`field` 指出 `viewBox.w`/`viewBox.h`；
  目标尺寸为零/负 -> `SV_E_TARGET`，`field` 指出 `target.w`/`target.h`。
- 不含 `SV_F_VIEWBOX` 时直接走原 `svg_fill`，产物逐像素相同。

`make -C svg demo` 输出手算映射表逐点比对、边缘偏差实测与上界、开关
关闭的逐像素一致性、两档尺寸的宽高比误差与耗时（字段名、宽高比误差、
耗时各一栏），并导出两档尺寸的 PGM。
