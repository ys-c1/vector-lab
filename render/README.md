# render — 场景渲染与命令行

多边形场景渲染管线：场景解析 → 仿射放置 → 超采样光栅化 → PGM 导出。

## 构建

```
make            # 单元测试 + 两个 CLI 二进制
make test       # C 单元测试
make cli        # build/rvec（新入口）、build/rvec-legacy（纯旧入口）
make check-cli  # 端到端：错误矩阵 / 帮助快照 / 新旧一致性（78 项）
```

## 场景文件（版本 1）

行式文本，`#` 起始为注释，空行忽略。必填段：`version`、`canvas`、至少一个 `poly…end`。

```
version 1            # 必填，目前仅支持 1
canvas 16 16 1.0     # 必填：宽(1..4096) 高(1..4096) 缩放(非零有限数)
poly                 # 多边形段开始
0 0                  # 每行一个顶点 x y（有限数），每多边形 3..64 个
10 0
10 10
0 10
end                  # 段结束，最多 32 个多边形
```

## 子命令

| 子命令 | 作用 |
|---|---|
| `parse <scene>` | 解析并校验，打印版本/画布/多边形/顶点摘要 |
| `diagnose <scene> [--verbose]` | 七项诊断逐条 PASS/FAIL，末尾给 blocked 状态 |
| `render <scene> [--ss 1..8]` | 渲染到内存并报告点亮像素数，不写文件 |
| `export <scene> --out <path> [--ss 1..8] [--force]` | 渲染并导出 PGM；默认拒绝覆盖 |

参数表以程序内同一张规格表驱动 `--help`，文档与实现不会脱节：`rvec <子命令> --help`。

### 错误码（退出码即错误码，全部 `blocked=yes` 拦下）

场景文件错误（1..6，带 `field=` 出错字段与 `line=` 行号）：

| 退出码 | 名称 | 触发输入 |
|---|---|---|
| 1 | E_FILE | 路径不存在/不可读/目录 |
| 2 | E_VERSION | 缺 version、重复 version、版本号不支持或非整数 |
| 3 | E_SYNTAX | 未知段、段字段数错、poly 缺 end、end 带字段、超长行 |
| 4 | E_MISSING | 缺 canvas 段、一个 poly 段都没有 |
| 5 | E_VALUE | 尺寸越界(1..4096)、scale 为 0、坐标非有限数、顶点少于 3 |
| 6 | E_CAPACITY | 多边形超过 32 个或单多边形顶点超过 64 个 |

命令行错误：

| 退出码 | 名称 | 触发输入 |
|---|---|---|
| 7 | E_USAGE | 未知子命令/选项、缺位置参数、多给场景、选项缺值、缺必填 --out |
| 8 | E_FIELD | --ss 非整数或越界、输出文件已存在（未给 --force） |
| 9 | E_OUTPUT | 输出路径不可写 |

错误输出统一格式：

```
error E_VALUE field=canvas.w line=2 blocked=yes: dimensions must be in 1..4096
```

## 旧位置参数入口（行为保持不变）

旧格式场景（无版本号，`p x y …` 行）与旧五参数用法仍可用：

```
# 编译期纯旧版：只认位置参数，不含新入口代码
build/rvec-legacy scene.txt out.pgm w h scale

# 同一个新二进制的运行期开关
RVEC_LEGACY=1 build/rvec scene.txt out.pgm w h scale
```

旧入口退出码沿用 1..7 与原文案。新入口**不**静默接受旧位置参数形式
（会报 E_USAGE），必须显式经上述两个开关进入旧路径。
`make check-cli` 会把旧路径产物与 git HEAD 上的旧 `cli.c` 产物逐字节比对。
