# clip — 凸多边形裁剪与多窗口顺序裁决

Sutherland–Hodgman 凸裁剪，外加多窗口连续裁剪的顺序裁决。

## 单窗裁剪（原逻辑，字节兼容）

- `clip_convex(poly, n, win, wn, out)`：凸多边形被凸窗口逐边裁剪。
- `clip_area(p)`：鞋带公式面积。
- 以 `-DCLIP_NO_MULTI` 构建时，多窗口代码整体不编译，单窗路径与旧版逐字节一致
  （见 `make bytecheck`）。

## 多窗口顺序裁决（新逻辑）

多个窗口连续裁同一多边形时，浮点累计误差依赖裁剪顺序。`clip_verdict()`
枚举窗口的全部 m! 个排列，按累积误差最小者定顺序。

### 误差怎么累

对排列 π = (w₁,…,wₘ)，第 k 步有三个多边形：

- P_k：按 π 顺序**连续裁剪**得到的实际多边形（P₀ = 源多边形，
  P_k = clip(P_{k-1}, w_k)）；
- Q_k：前 k 个窗口的**凸交集多边形**（窗口之间依次 clip 得到，顺序无关）；
- S_k：源多边形对 Q_k 的**一次性裁剪** clip(poly, Q_k)，即该前缀的理想参照。

逐步误差 e_k = |area(P_k) − area(S_k)|，累积误差

```
E(π) = Σ_{k=1..m} e_k
```

面积一律用 Kahan 补偿求和（`clip_area_kahan`），把裁决自身的舍入噪声压到最低。
E 最小的排列胜出；并列时取枚举中最先出现者（字典序优先），结果确定。

### 非法排列（拒绝，不参与裁决）

- `CLIP_REASON_EMPTY`：某窗口为空/退化（顶点 <3，或面积 ≤ 1e-9 × 外接矩形面积），
  记录空窗口下标；
- `CLIP_REASON_DUPWIN`：两个窗口完全重合（交集面积与两者各自面积的相对差均 < 1e-9），
  记录这对窗口下标。部分相交与包含（嵌套）均为合法几何。

任一非法条件存在时所有排列都被拒绝，`clip_verdict` 返回 -3。

### 构建与产物

```
make test        # 单元测试（原 5 项单窗检查 + 154 项裁决检查）
make multi       # 内置 6 组用例，产物落 ../artifacts/multi/
make bytecheck   # 新旧单窗路径逐字节比对
make demo        # 原演示
```

每组用例产物：

| 文件 | 内容 |
|---|---|
| `<name>.verdict.txt` | 每排列合法性/拒绝原因/累积误差/逐步误差/中位数耗时，最优排列与全枚举耗时 |
| `<name>.verdict.json` | 同上的机读格式 |
| `<name>.stepdump.txt` | 最优排列逐步面积：实际 P_k、参照 S_k、前缀交集 Q_k 及各自顶点 |
| `summary.txt/json` | 六组汇总，含字典序误差对照（要求 best ≤ 0.5 × lex） |

耗时为每排列 7 次重复评估的中位数（纳秒，`CLOCK_MONOTONIC`），抑制单次抖动。

外部场景：`./build/multi <scene.txt> <outdir> <name>`，scene 格式见 `multi.c` 头注释。

## 内置用例结果

四组合法用例（two/three/four/nested）的最优累积误差均不超过字典序顺序的 31%，
嵌套组 innermost-first 误差为 0；两组非法用例（空窗口、重复窗口）全部 6 个排列被拒。
逐步面积与 `gold/ref` 独立参照（256×256 网格面积）交叉核对一致。
