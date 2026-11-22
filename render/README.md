# render

栅格渲染管线：场景变换、扫描线填充、PGM 导出，以及连续对数缩放。

## 缩放层

缩放写成以 2 为底的对数，层号 `level = log2(scale)` 允许小数。
小数层号拆成相邻两个整数层 `lo`、`hi`，权重 `w = level - lo`：

- `w = 0`、`level` 为整数时只取一档，不插值；
- `0 < w < 1` 时两档各按自己的缩放渲染覆盖率，再按 `w` 逐像素插值，
  避免整层硬跳。

`scale <= 0`、`NaN`、`Inf` 一律拒绝，理由见 `render/cli.c` 的退出码 8/9。

## 命令

- `make -C render test`：单元测试
- `make -C render check`：四档缩放报告、整数层单独判定、非法缩放、
  逐字节回归、跳变对比
- `make -C render golden`：默认路径与引入缩放层之前的二进制产物逐字节比对

## CLI

```
build/cli scene.txt out.pgm w h scale [--blend|--hard|--off]
```

- 默认（或 `--off`）：旧的整数层路径，按 `scale` 原值直接变换、二值填充；
- `--blend`：连续层过渡；
- `--hard`：走新代码但强制硬切，用来与 `--blend` 对比跳变。
