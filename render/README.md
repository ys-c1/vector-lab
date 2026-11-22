# render

场景多边形的点采样栅格化与 PGM 导出。

## 固定缩放（旧路径，保持不变）

```
make build/cli
./build/cli scene.txt out.pgm w h scale
```

## 反解缩放

```
make inv
./build/invcli scene.txt out.pgm w h target_density [scalemax]
```

按**目标像素密度**（窗口内命中像素数 / 原图形世界面积）二分最小缩放，
再在与窗口同格距、同采样相位的扩大网格上验证边缘丢失像素数，必须为 0。

- 缩放上界：缩放后包围盒每边至少让出一个采样格；`scalemax` 可再收紧。
  目标密度在上界处仍不可达时拒绝（退出码 8），并打印可达上限。
- 退化图形：鞋带公式面积为 0 的多边形逐个数出；场景总面积为 0 时
  密度无定义，拒绝（退出码 7）。
- 超采样固定 SS=4，与 `pipeline_render` 一致；输出 PGM 与固定缩放路径同格式。
- 关闭新逻辑（不调用 `inverse_solve`、继续用 `cli`）时，旧产物逐字节不变。

退出码：0 成功；7 退化；8 密度越界；9 边缘丢失（正常不应出现）。

`tools/verify_inv.py` 用绕数法（区别于 C 端射线交叉法）独立复算命中、
边缘丢失、缩放上界并重新二分，与 C 结果互证。
