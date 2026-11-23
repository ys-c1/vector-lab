# vector-lab

矢量图形 C 模块集合，每个模块独立构建、自带测试与演示，模块间零跨依赖。

## 模块

| 组 | 模块 | 内容 |
|---|---|---|
| 1 | bezier | 三次贝塞尔求值与按容差细分 |
| 1 | stroke | 折线描边到多边形轮廓 |
| 1 | dash | 虚线切分与相位控制 |
| 1 | bbox | 点集包围盒与旋转盒 |
| 2 | hull | 凸包 |
| 2 | arclen | 累计弧长与等分取点 |
| 3 | isect | 线段相交 |
| 3 | clip | 凸多边形裁剪 |
| 4 | svg | SVG path 解析与 PGM 填充 |
| 4 | xform | 仿射变换 |
| 5 | gold | 独立参照实现（超采样面积/像素/等分点） |
| 5 | render | 场景→光栅→管线→PGM 导出，含 CLI |

每个库模块：`make -C <模块> test`，演示：`make -C <模块> demo`。
render 出图：`make -C render build/cli`；gold：`make -C gold`。

## 从空环境到出图（最短 5 步）

```bash
sudo apt-get update && sudo apt-get install -y gcc make libc6-dev coreutils diffutils gawk bash
git clone <repo> && cd vector-lab
make -C gold && make -C render build/cli
render/build/cli fixtures/scene.txt /tmp/out.pgm 40 40 1.0
tools/diff.sh /tmp/out.pgm fixtures/baseline.pgm   # 期望 IDENTICAL，0 差异像素
```

## 一键校验

```bash
python3 tools/verify_manifest.py --root .          # 人读
python3 tools/verify_manifest.py --root . --json   # 机读
```

完整依赖清单（每模块头文件/库、apt 包、缺失报错、步骤条数）见
[`DEPS.md`](DEPS.md) 与机读清单 [`manifest.json`](manifest.json)。
出现未声明依赖、构建失败或渲染差异非零时，校验脚本以非零退出码失败。
