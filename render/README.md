# render

场景光栅化与导出：`scene`（多边形场景）→ `pipeline`（仿射摆放）→
`raster`（4x4 超采样、any-hit 二值）→ `export`（8 位 PGM）。

原有入口保持原样：

```
make build/cli
./build/cli scene.txt out.pgm w h scale
```

## 贪心定窗口（winplan）

**问题**：窗口数量定死时，位置随手取，有些区域根本没被窗口盖到；覆盖率
只能靠碰运气。`winplan` 把窗口大小固定、位置当变量，用贪心逐块挑：

> 每次在所有候选格点里挑**覆盖未覆盖图形像素最多**的那一块，接受它，
> 再挑下一块，直到某个停止条件命中。

### 口径

- **图形像素**：整幅渲染（同 `pipeline_render`，ss=4、any-hit）里值非零的
  像素。覆盖率 = 被窗口盖到的图形像素 / 图形像素总数。留白不算漏覆盖，
  图形像素没被任何窗口盖到才算——所以覆盖率和“窗口数”解耦。
- **候选格点**：x = 0, stride, 2·stride, … 直到 ≤ w-win_w，再补贴右边界的
  w-win_w（落不到格点上时）；y 同理。窗口不越界。增益并列取扫描序
  （先小 y 后小 x）第一个，结果确定、可复现。
- **重叠率**：`(窗口面积之和 − 窗口并集面积) / 窗口面积之和`，按像素
  整数精确计算。窗口等大且不越界时分子分母都精确。

### 停止条件

每接受一个窗口后按顺序判断，先命中者生效：

| stop | 条件 | 结果 |
|---|---|---|
| `FULL` | 全部图形像素已覆盖 | **立即停**，不再挑下一块；覆盖率 1 |
| `TARGET` | 覆盖率 ≥ target | 达标停 |
| `OVERLAP` | 所有剩余候选都会让重叠率超上限，全部被拒 | 拒绝并说明；已接受集合仍守上限 |
| `NOGAIN` | 所有候选增益为 0（目标未达） | 失败，rc=-2 |
| `MAXWIN` | 窗口数用满（目标未达） | 失败，rc=-2 |
| `EMPTY` | 图形像素数为 0 | 覆盖率约定为 1，0 个窗口 |
| `FIXED` | 固定窗口数对照模式走完 | 仅对照用，不做贪心、不查重叠 |

**重叠上限是硬约束**：若当前增益最大的候选会让重叠率超过 `overlap_cap`，
拒绝它、记下“位置 / 增益 / 若接受会达到的重叠率”，再试下一个候选；被拒
候选永久作废（已接受集合只增不减，同一候选再算只会更差）。因此 `OVERLAP`
停止时已接受窗口的重叠率必然 ≤ 上限。

**已全覆盖时立即停**：`FULL` 判断排在 `TARGET` 前面，覆盖率到 1 就不再
用 target 提前收手。

### 用法

```
make build/winplan_cli
./build/winplan_cli run <scene.txt> <out.pgm> <w> <h> \
        [win_w win_h stride target overlap_cap max_wins] [--fixed=N] [--scale=S]
./build/winplan_cli legacy <scene.txt> <out.pgm> <w> <h> <scale>
```

默认参数：窗口 40×40、步长 8、目标 0.98、重叠上限 0.60、最多 256 个窗口。

`run` 输出：`plan stop=… nwins=… coverage=… overlap=…`、每个窗口的
`win k x y w h`、每条拒绝的 `reject #k after=… at (x,y) gain=… overlap_would_be=…`、
分段耗时（`full_ms` 整幅基准、`plan_ms` 规划、`render_ms` 按窗口渲染）、
`pixel_match`（渲染点亮数 == 规划 covered）与 `target_met`。

退出码：0 达标；3 重叠上限拒光（`OVERLAP`）；4 目标未达（`NOGAIN`/`MAXWIN`）；
2 参数或 IO 错误。

### 四组图形结果（256×256，默认参数）

| 场景 | 内容 | 窗口数 | 覆盖率 | 重叠率 | 停止 | 耗时 |
|---|---|---|---|---|---|---|
| g1_sparse | 四角分离方块 | 4 | 1.000000 | 0.000000 | FULL | ~28 ms |
| g2_clusters | 两簇重叠圆盘 | 14 | 0.987987 | 0.265714 | TARGET | ~237 ms |
| g3_thin | 五条细斜条 | 15 | 0.981107 | 0.138667 | TARGET | ~42 ms |
| g4_mixed | L 形 + 卫星块 | 21 | 0.980794 | 0.125714 | TARGET | ~67 ms |

四组覆盖率全部达到 0.98 目标（g1 更是先到全覆盖）。耗时以整幅基准渲染
（`full_ms`）为主，规划本身在 1 ms 量级。

### 重叠上限拒绝示例

同参数把上限收紧：

| 场景 | cap | 窗口数 | 覆盖率 | 重叠率 | 停止 | rc |
|---|---|---|---|---|---|---|
| g5_overlap | 0.22 | 30 | 0.984236 | 0.210667 | TARGET | 0 |
| g5_overlap | 0.20 | 30 | 0.982036 | 0.197333 | TARGET | 0 |
| g2_clusters | 0.20 | 16 | 0.966464 | 0.170000 | OVERLAP | 3 |
| g2_clusters | 0.30 | 14 | 0.987987 | 0.265714 | TARGET | 0 |

g2_clusters 在 cap=0.20 时，再往下每块都会把重叠率推到 0.2338 以上（第一条
拒绝记录），40 个候选被拒后停，覆盖率 0.966464 未达 0.98——如实报失败，
不会偷偷超上限。

### 固定窗口数对照（旧做法）

`--fixed=N` 按候选扫描序取前 N 个窗口，不做贪心、不查重叠。g4_mixed 同参数：

| 模式 | 窗口数 | 覆盖率 | 重叠率 |
|---|---|---|---|
| 贪心 | 21 | 0.980794 | 0.125714 |
| --fixed=4 | 4 | 0.000000 | 0.600000 |
| --fixed=8 | 8 | 0.006305 | 0.700000 |
| --fixed=21 | 21 | 0.027741 | 0.761905 |

同样 21 个窗口，贪心覆盖 98%，定死数量按扫描序摆只能覆盖 2.8%——这就是
“窗口数量定死、有些区域根本没被覆盖”的量化对照。

## 关掉新逻辑逐字节不变

- `winplan` 是独立目标：`build/cli` 只编 `scene/raster/pipeline/export`，
  行为与改动前完全一致。
- `pipeline_render` 的运算链未改（`pipeline_transform` 是它的子步骤，
  渲染顺序与采样公式不变），`raster_fill` 的循环经 `fill_box` 重构但每个
  字节的算式逐项相同。
- `verify_winplan.sh` 用旧 CLI 重跑五组场景，与仓库内基线
  `tests/baseline/*.pgm` 逐字节比对，全部 `IDENTICAL`。

## 全窗口并集与旧渲染逐字节相同

`winplan_render` 用与 `raster_fill` 相同的采样相位（ss=4）按窗口只填区域
（`raster_fill_region`）。计划 `stop=FULL` 时窗口并集已盖住全部图形像素，
窗口内外的字节与整幅 `pipeline_render` 完全一致——`verify_winplan.sh`
对 g1_sparse / g6_cover_all / g7_dense 用 `cmp` 验证，全部 `IDENTICAL`。

## 独立复算

`tools/verify_winplan.py` 用绕数法（C 端是奇偶射线交叉法）重算图形像素，
并按同一候选网格**独立重放整条贪心循环**，逐项核对：窗口序列、停止码、
覆盖率、并集面积、重叠率、每条拒绝记录的重叠率与增益。四组图形与拒绝
用例全部 `MATCH`。

## 构建与验证

- `make test`：原单元测试 + `winplan` 单元测试（109 项，覆盖 FULL/TARGET/
  OVERLAP/NOGAIN/MAXWIN/EMPTY/FIXED 各停止条件、候选计数、参数守卫、
  区域渲染与整幅渲染逐字节一致、部分计划的像素子集关系）
- `make verify-winplan`：四组图形报告、重叠上限拒绝、FULL 立即停并逐字节
  比对、旧 CLI 与基线逐字节比对、固定窗口数对照，每组附带独立复算
