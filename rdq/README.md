# rdq

率失真（rate-distortion）灰度位深分配 + 块内误差扩散。面向只吃 4 位灰度
（16 级）的老设备：在给定体积预算下，为每个块单独选位深，使 PSNR 尽量高。

## 模型

- 图像切成 `block x block` 的块（默认 16）。
- 每块候选位深 `d ∈ [dmin, dmax]`，默认 `1..4`（`RQ_DEV_BITS=4`）。
- 块 `i` 在位深 `d` 下的**失真** `D_i(d)`：块内蛇形 Floyd–Steinberg 扩散后的
  真实 SSE（不是估计值，与最终码流逐位一致）。
- **码率** `R_i(d) = ceil(w_i*h_i*d/8)` 字节，加每块 1 字节的位深表。

## 最优分配

对每块先取 Pareto 前沿（去掉「字节更多、SSE 不更低」的档），再取凸包下沿，
得到该块**边际斜率严格递减**的升级步。所有块的步做全局归并，形成一条与预算
无关的阶梯；取阶梯前 `m` 步即拉格朗日乘子 `λ` 下的最优解，停下的那一步斜率
就是该预算的 `λ`（记录在 `RQStats.lambda`）。

预算变大只会沿同一阶梯多走几步，累计 SSE 单调下降，因此 **PSNR 随预算单调
不减是构造保证**，不是经验观察。

## 误差扩散与设备网格

- 每块内部按蛇形 Floyd–Steinberg 把量化误差扩散给邻居；块边界残差丢弃，
  所以解码只查本块码字，无跨块状态。
- 每块的 `2^d` 级调色板都取自 4 位设备的 16 级网格 `k*255/15`（映射为
  `round(j*15/(2^d-1))`），任何位深的结果都能在 4 位设备上原样显示。
- 块缝与块内误差实测相当（约 0.9–1.03 倍），不形成可见台阶。

## 体积口径

`RQStats.nbytes` 就是落盘文件字节数：头 18 字节 + 每块 1 字节位深表 +
每块按位打包的索引。编码器绝不让 `nbytes` 超预算；若连全图最低档都放不下，
返回 `-2` 并把 `*badparam` 设为 `"budget"`。

预算 `<= 0` 直接拒绝，`*badparam = "budget"`。

纯色图（所有像素相同）走无损路径：头标志 `lossless=1`，只存一个 8 位灰阶，
解码逐字节还原，PSNR 为 `INFINITY`。

## 用法

```
make -C rdq test                        # 单元测试
make -C rdq demo                        # 四档预算结果 + 断言

./rdq/build/cli encode in.pgm out.rq <budget_bytes> [block]
./rdq/build/cli decode in.rq out.pgm
```

`in.pgm` 为 P5 / maxval 255。`encode` 打印实际体积、占预算比、PSNR、λ。

## 既有 8 位导出

`render/src/export.c` 未改动，`export_pgm` 输出逐字节不变（同一输入两次
渲染 SHA-256 相同）。
