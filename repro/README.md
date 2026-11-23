# 可复现构建与等价性验证

`repro/repro.sh` 回答两个问题，且只凭哈希与像素数下结论，不靠“肉眼看不出差别”：

1. **同一组源码构建两次，两份可执行文件逐字节相同吗？** —— 比对 sha256。
2. **这两份可执行文件各渲同一场景，两张图逐像素一致吗？** —— 严格解析 P5 头后逐字节比对像素区。

脚本退出码是判定依据：`0` = 全部一致，`1` = 有任一哈希或像素不一致。

## 三组源码

| 组 | 源码 | 可执行文件 | 渲染输入 | 输出 |
|---|---|---|---|---|
| render | `render/src/*.c` + `render/cli.c` | `cli` | `scene/star.scene`（4 位小数坐标） | P5 200×120，scale 0.5 |
| svg | `svg/src/svgpath.c` + `svg/demo.c` | `cli` | `scene/star.svg`（同一几何的路径串） | P5 200×120 |
| gold | `gold/ref.c` | `ref` | `scene/star.poly`（同一批顶点） | P5 200×120，8×8 超采样 |

`scene/star.*` 三个文件由同一条 Python 生成式算出（10 角星），几何完全一致；渲染窗口和算法不同，因此三组图像**不要求**彼此相同，只要求每组**自身两次构建**的产物相同。

## 固定的不确定来源

| 来源 | 对应手段 |
|---|---|
| 时间戳 | `SOURCE_DATE_EPOCH`（缺省 0）；`__DATE__/__TIME__` 由 GCC 在定义该变量时固定；`tar --mtime`、`ar` 的 `ZERO_AR_DATE` |
| 文件顺序 | 源码/头文件清单显式列出并按路径 `sort`，链接顺序固定；不用 shell glob 或 make wildcard；`tar --sort=name` |
| 归档元数据 | `tar --owner=0 --group=0 --numeric-owner --mode='u+rwX,go+rX,go-w' --format=gnu --mtime`；`ar rcsD` |
| 编译路径 | 源码先拷进每构建独立的快照，`-ffile-prefix-map/-fmacro-prefix-map/-fdebug-prefix-map` 统一映射为 `/src`，`-g` 调试信息里也不留真实路径 |
| 编译器随机性 | `-frandom-seed=vector-lab`、`-fno-ident`、`-Wl,--build-id=sha1` |
| 进程环境 | `LC_ALL=C`、`TZ`、`umask`、`TMPDIR`、cwd 两个构建刻意取不同值 |

### 两次构建是刻意“不友好”的

- 构建 a：正序拷贝源码、所有 mtime = epoch、`umask 0022`、`TZ=UTC`、cwd 为默认目录。
- 构建 b：**倒序**拷贝源码、每个文件 mtime = epoch+序号（彼此不同）、`umask 0077`、`TZ=America/New_York`、cwd 切到 `\tmp`、`TMPDIR=/var/tmp`。

这两组条件下哈希仍一致，才说明固定手段是生效的，而不是恰好两次都在同一台同样状态的机器、同一秒内编译。

## 使用

```bash
repro/repro.sh                 # 默认 SOURCE_DATE_EPOCH=0
SOURCE_DATE_EPOCH=1700000000 repro/repro.sh
```

产物（`repro/out/`，已 gitignore）：

```
out/<组>/<a|b>/bin/<可执行文件>
out/<组>/<a|b>/artifacts/star.pgm
out/<组>/<a|b>/logs/{compile,render}.log
out/<a|b>.tar.gz            # 三组可执行文件的确定性归档
```

## 实测环境

- gcc 15.2.0（Ubuntu 15.2.0-16ubuntu1），GNU binutils 2.46，GNU Make 4.4.1
- Linux 6.18.54.1-microsoft-standard-WSL2 (Ubuntu 26.04)

## 已知边界

- 可复现性针对**同一编译器、同一 binutils、同一 libc 的同一版本**。换 GCC 版本、换 binutils 或换 libc，二进制哈希会变；此时需同时比对 `sha256sum` 之外的东西（例如同一源在固定工具链镜像里构建）。
- 图像比对还检查 P5 头（`P5`、宽高、maxval）一致；只比像素不看头会漏掉尺寸变化。
- `SOURCE_DATE_EPOCH` 改变会改变**归档**哈希（成员 mtime 变了），这是预期行为；**可执行文件**哈希不变，因为源码不读 `__DATE__/__TIME__`，且 `-Wdate-time` 会对此告警。
