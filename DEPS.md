# 依赖与复现说明（vector-lab）

本文件由 `manifest.json` 机读清单配套，所有结论均由 `tools/verify_manifest.py`
在干净构建中实测校验。**若清单与实际不符（含出现未声明依赖、构建失败、渲染差异非零），
校验脚本以非零退出码失败。**

## 1. 从空环境到出图：最短 5 步

目标平台：Debian/Ubuntu (apt)，x86_64，C11。

| 步骤 | 命令 | 作用 |
|---|---|---|
| 1 | `sudo apt-get update && sudo apt-get install -y gcc make libc6-dev coreutils diffutils gawk bash` | 装齐工具链与库 |
| 2 | `git clone <repo> && cd vector-lab` | 取源码 |
| 3 | `make -C gold && make -C render build/cli` | 构建参照实现与渲染器 |
| 4 | `render/build/cli fixtures/scene.txt /tmp/out.pgm 40 40 1.0` | 渲染出 PGM |
| 5 | `tools/diff.sh /tmp/out.pgm fixtures/baseline.pgm` | 与基线逐字节比对，期望 `IDENTICAL` |

一键校验（含全部 12 个模块的干净构建 + 依赖核对 + 渲染零差异）：

```bash
python3 tools/verify_manifest.py --root .        # 人读
python3 tools/verify_manifest.py --root . --json # 机读
```

## 2. 工具链与系统包

| 包 (apt) | 提供 | 用途 | 缺失时报错 |
|---|---|---|---|
| `gcc` | `gcc`/`cc` | 编译全部 C 代码 | `make: cc: No such file or directory` → `Error 127` |
| `make` | `make` | 驱动各模块 Makefile | `make: command not found` |
| `libc6-dev` | `math.h stdio.h stdlib.h string.h ctype.h`，`libc.so`、`libm.so` 开发链接 | 系统头与数学库 | 头缺失：`fatal error: math.h: No such file or directory`；库缺失：`undefined reference to 'fmod'/'sincos'` |
| `coreutils` | `mkdir` `rm` | Makefile 建/清 build 目录 | `make: mkdir: No such file or directory` |
| `diffutils` | `cmp` | `tools/diff.sh` 逐字节比对 | `tools/diff.sh: line N: cmp: command not found` |
| `gawk` | `awk` | `diff.sh` 仅在有差异时打印首个偏移 | `awk: command not found`（通过/失败判定只需 `cmp`） |
| `bash` | `bash` | 运行 `diff.sh` | `bad interpreter` |

> 所有模块 Makefile 统一带 `-lm`。在现代 gcc/glibc 下多数数学函数被内置内联，
> 但 **`dash`（`fmod`）与 `render`（`sincos`）去掉 `-lm` 必然链接失败**，
> 因此 `-lm` 是全模块必需，不可省略（也保证老工具链可移植）。

## 3. 五模块分组（对应 5 个功能提交）

| 组 | 模块 | 步骤条数 | 系统头 | 库 | 缺 `-lm` 报错 |
|---|---|---|---|---|---|
| 1 初始四库 | bezier, stroke, dash, bbox | 各 2 | 见下 | m | 仅 dash：`undefined reference to 'fmod'` |
| 2 凸包+弧长 | hull, arclen | 各 2 | 见下 | m | —（函数被内联，但仍声明 -lm） |
| 3 相交+裁剪 | isect, clip | 各 2 | 见下 | m | — |
| 4 SVG+仿射 | svg, xform | 各 2 | 见下 | m | — |
| 5 参照+渲染 | gold, render | gold=1，render=2 | 见下 | m | render：`undefined reference to 'sincos'` |

“步骤条数”= 该模块从干净树到产物所需的 make 调用数（普通模块：`test` + `demo` 共 2；
gold 仅构建 1；render 为 `test` + `build/cli` 共 2）。完整逐模块明细见 `manifest.json`。

### 逐模块头文件与库（编译单元并集，已无未声明项）

| 模块 | 系统头 | 本地头 | 库 |
|---|---|---|---|
| bezier | math.h, stdio.h | bezier.h | m |
| stroke | math.h, stdio.h | stroke.h | m |
| dash | math.h, stdio.h | dash.h | m |
| bbox | stdio.h | bbox.h | m |
| hull | math.h, stdio.h, stdlib.h | hull.h | m |
| arclen | math.h, stdio.h | arclen.h | m |
| isect | math.h, stdio.h | isect.h | m |
| clip | math.h, stdio.h | clip.h | m |
| svg | ctype.h, stdio.h, stdlib.h, string.h | svgpath.h | m |
| xform | stdio.h | xform.h | m |
| render | math.h, stdio.h, stdlib.h, string.h | scene.h, raster.h, pipeline.h, export.h | m |
| gold | math.h, stdio.h, stdlib.h, string.h | （无） | m |

模块之间**零跨模块依赖**：每个模块只 include 自己 `src/` 下的头，可独立构建。
所有系统头与 `libm` 均由 `libc6-dev` 提供，无任何第三方库。

## 4. 渲染零差异机制

- 夹具：`fixtures/scene.txt`（render 输入，非对称三角形 2,2 / 38,8 / 20,38）、
  `fixtures/poly.txt`（gold 输入，同几何）。
- 基线：`fixtures/baseline.pgm`，PGM P5、40×40、maxval 255，由**独立算法**
  `gold/ref`（8×8 超采样覆盖率灰度）生成并纳入版本控制。
- 渲染管线 `render` 与 gold 采用同一像素公式：
  `buf[i] = (unsigned char)(255 * hit / (ss * ss))`，超采样均为 8×8。
- 比对：`tools/diff.sh` 用 `cmp` 逐字节判定；校验脚本另做独立像素统计，
  报告 `differing_pixels`。当前结果：**differing_pixels = 0 / 1600，byte-identical**。

### 为达成零差异所做的渲染器修正（render 模块）

原始 `render/src/raster.c` 对每个像素只写硬阈值 `255/0`，且 `pipeline.c` 用 4×4
超采样；而 gold 写覆盖率灰度且用 8×8 超采样，二者必然不同（实测 125 像素差异）。
修正为与 gold 同构：

1. `raster.c`：累计命中子采样数，输出覆盖率灰度 `255*hit/(ss*ss)`；
2. `pipeline.c`：超采样等级 4 → 8。

修正后 render 自测仍 10/10 通过，且与 gold 逐字节相同。

## 5. 校验脚本覆盖项（136 项）

- 每个 Makefile 模块都在 manifest 声明；
- 扫描每个编译单元真实 `#include`，核对系统头/本地头/库全部已声明（**未声明即 FAIL**）；
- 每个系统头、每个库的 apt 归属包都在 toolchain 声明；
- 全部模块干净构建（先 clean）+ 单测 0 失败 + 产物存在；
- 实测移除 `-lm` 的链接结果与 manifest 标注一致；
- 端到端渲染、与基线逐字节比对、独立像素差异统计为 0；
- 现算 gold 与入库基线一致（保证基线本身可信）。
