# render

场景多边形的点采样栅格化与 PGM 导出。

## 固定路径（旧路径，保持不变）

```
make build/cli
./build/cli scene.txt out.pgm w h scale
```

固定 ss=4、any-hit 二值化。关闭新逻辑（继续用 `cli`）时旧产物逐字节不变。

## 二维参数反解（ss × bits）

同一个场景用不同设置导出会得到不同字节。`paramsolve` 把「超采样数 ss」和
「位深 bits」当成二维参数网格，穷举哪组 `(ss, bits)` 导出的 PGM 与参照
**逐字节一致**。

```
make paramcli
./build/paramcli scene.poly ref.pgm w h [sslo..sshi] [bitslo..bitshi] [min|max]
```

- 渲染模型与 `gold/ref.c` 的覆盖率口径对齐：每像素 `ss×ss` 个子样本，
  命中数 `c ∈ [0, ss²]`。
  - `bits = 1`（旧路径口径）：`c > 0 → 255`，即 any-hit；与 `raster_fill`
    完全等价，所以 **(ss=4, bits=1) 就是原来的默认导出**。
  - `bits ∈ [2,8]`：`level = floor(c·(2^bits−1)/ss²)`，灰度
    `round(255·level/(2^bits−1))`。**bits=8、ss=8** 时与 gold 的
    `(unsigned char)(255*c/(8*8))` 逐字节一致。
- 合法域 `ss ∈ [1,8]`、`bits ∈ [1,8]`。越界或区间倒置（lo>hi）一律拒绝，
  退出码 2，并写明字段（`field=ss` / `field=bits`）。
- 多组并列：并列组合导出的 PGM 逐字节相同，选哪组都不改变图像。默认按
  **成本最小**选（`min`：ss 优先，其次 bits），`max` 取最大。工具会如实
  打印 `matches=N` 与 `ambiguous`，不会假装唯一。若必须确认「当初是哪组
  导出的」，单凭字节做不到，需要外部信息。

### 输出

```
MATCH ss=8 bits=8 diff_bytes=0          # 每个并列组合一行
CHOSEN ss=8 bits=8 diff_bytes=0 matches=1 rule=min(ss,bits)
LEGACY_DEFAULT ss=4 bits=1 diff_bytes=908   # 旧默认口径与参照差多少字节
TIME ss=8 coverage_ms=32.77             # 各 ss 覆盖率渲染耗时
TOTAL ms=101.57
```

退出码：0 成功（含并列）；2 参数越界/倒置；3 合法域内无任何组合能复现；
4 文件/IO/PGM 格式；5 参照尺寸不符；6 其它。

### 四组场景结果

参照取 gold 等价口径 (ss=8, bits=8)，256×256：

| 场景 | 唯一? | 选中 (ss,bits) | 差异字节 | 旧默认(4,1) 差异 | 耗时 |
|---|---|---|---|---|---|
| diagonal 斜边四边形 | 唯一 | 8,8 | 0 | 890 | ~120 ms |
| sharp 尖角三角 | 唯一 | 8,8 | 0 | 908 | ~102 ms |
| sliver 细长条 | 唯一 | 8,8 | 0 | 898 | ~120 ms |
| lshape L 形 | 并列 (4,8)/(8,8) | 4,8 | 0 | 219 | ~160 ms |

并列说明：L 形的 8 位量化在 ss=4 与 ss=8 下落到同一张图，字节无法区分这两组，
按成本选 ss 更小的 (4,8)。

## 构建

- `make test`：单元测试（量化表、旧路径逐字节等价、反解自洽、越界拒绝、
  PGM 往返）
- `make demo`：四组场景反解报告 + 并列选取说明 + 参数守卫 + 旧路径逐字节比对
- `make paramcli`：反解命令行
- `make build/cli`：原命令行入口，行为不变
