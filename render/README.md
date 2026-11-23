# render

场景多边形的栅格化与 PGM 导出，提供两套超采样方案。

## 两套采样

- **等分方案（冻结）**：`raster_fill` / `pipeline_render`，4x4 等距网格、二值 0/255。
  输出已冻结，任何改动不得改变其产物（CI 与 git 旧版逐字节比对）。
- **确定性抖动方案**：`raster_fill_jitter` / `pipeline_render_jitter`，每像素 ss×ss
  个抖动点，输出灰度覆盖 `255*hits/ss²`。

### 确定性规格（与 `gold/ref.c jitter` 同一规格、独立实现）

- PRNG：SplitMix64，状态初值 = `(uint64_t)seed`，全整数运算，位级跨平台一致。
- 64 位整数 z 映射到 [0,1)：`(z >> 11) * 2^-53`（2 的幂乘法，double 位级可复现）。
- 每个采样点消耗两次抽取，先 ux 后 uy；像素内顺序 `t = y*ss + x`。
- **拒绝规则**：`seed == 0` 返回 -5；序列长度 n <= 0 返回 -1，并在 stderr 说明。

### 台阶度量

斜边台阶数 = 混合 2x2 块中四像素全为纯 0/255 的块数（硬拐角块即肉眼可见的台阶）。
两套方案用同一函数评分。抖动灰度覆盖把硬拐角变成软过渡，四角度（15/30/45/60°）
台阶数相对等分下降均 >= 30%（实测 100%）。

## 用法

```
make test     # 单元测试（含 SplitMix64 KAT、确定性、拒绝规则）
make demo     # 四角度报表：台阶数、十次一致标记、毫秒，并导出 demo-seq.txt
make build/cli
cli scene.txt out.pgm w h scale                 # 等分（旧用法不变）
cli jitter scene.txt out.pgm w h scale ss seed  # 确定性抖动
cli sequence out.txt seed n                     # 导出抖动序列
```

## 序列逐项比对

```
../tools/jitter-check.sh build/cli ../gold/build/ref <seed> <n> <workdir>
```

输出 `ITEM-IDENTICAL` 表示 n 对 ux/uy、表头、行数全部精确相等；篡改任一项即失败。

## KAT（seed=1, n=4，固化在 test.c）

```
0.5665615751722809   0.74578175726270113
0.97100275358679622  0.44435921705577208
0.44426470082635805  0.76289439191176101
0.87734868676417299  0.52306717985098139
```
