# render

场景/多边形光栅化、管线与 8 位 PGM 导出。`cli` 与 `src/{scene,raster,
pipeline,export}.c` 未改动，输出逐字节保持原样。

## 窗口旋转反解（winrot）

窗口是 `win_w x win_h` 的矩形，绕图形轴对齐包围盒的中心旋转 `theta`。
窗口四角要不出空，等价于旋转后的窗口仍完全落在图形里；用包围盒做必要条件，
得到两条约束（`theta` 取 0..90 度）：

```
宽度: win_w*cos(t) + win_h*sin(t) <= bw
高度: win_w*sin(t) + win_h*cos(t) <= bh
```

两条曲线在 `[0,90]` 内先升后降，各自左侧分支的根就是该约束允许的最大角；
取两者较小者。某条约束在零旋转就满足时它允许到 90 度，不参与限制。
窗口在零旋转就比包围盒大，空角必然存在，直接拒绝（`WINROT_NOFIT`）。

**适用前提**：包围盒只是必要条件。上面的解是「零空角」的上界；当图形铺满
自己的包围盒（矩形、三角剖分、网格、扇形拼块等）时该上界可达，实测空角为 0。
图形没铺满包围盒时，即使角度小于解，角上仍可能是背景像素——这时报出的
角度是上界，不是保证。求解的是「窗口中心与包围盒中心重合」这一族摆放；
窗口可任意平移时空间是无限的（能塞进角落），最大角退化为 90 度。

| 返回码 | 含义 |
|---|---|
| `WINROT_OK` | 解在 cap 以内 |
| `WINROT_BADARG` | 参数非有限/窗口非正/cap 不在 (0,90] |
| `WINROT_DEGEN_POINT` | 包围盒退化成点（宽高都为 0） |
| `WINROT_DEGEN_LINE` | 包围盒退化成线（宽或高为 0） |
| `WINROT_NOFIT` | 零旋转窗口就超出包围盒 |
| `WINROT_OVER_CAP` | 解出的最大角超过 cap（默认 45 度），拒绝并说明 |

## 用法

```sh
make -C render test          # 原有单元测试
make -C render test-winrot   # 反解单元测试（47 项）
make -C render verify-winrot # 四组报告 + 拒绝/退化 + 零旋转逐字节比对

./render/build/winrot_cli solve <bw> <bh> <win_w> <win_h> [cap]
./render/build/winrot_cli run <scene.txt> <win_w> <win_h> <out.pgm|-> <w> <h> \
        [ss] [cap] [--allow-over-cap] [--theta=<deg>]
```

`run` 的场景格式与 `cli` 相同（每行 `p x y x y ...`）。`scene` 也可以写成
`bw,bh@cx,cy` 的合成包围盒，此时只验证求解器、不渲染。四角方块边长取
`min(w,h)/8`（至少 1 像素），`corner_empty_px` 是四个角方块里值为 0 的像素数。

`--theta=` 强制角度，仅作诊断：扫过解出的角度可以复现「恰好在边界上
空角为 0，再加一点就出现空角」。

## 零旋转基线

`make build/cli_zero` 只编原有四个源文件，把新逻辑整体关掉；`verify_winrot.sh`
用同一场景、同一尺寸分别跑 `cli_zero` 和 `winrot_cli --theta=0`，再用
`tools/diff.sh` 逐字节比对，三者全部 `IDENTICAL`。
