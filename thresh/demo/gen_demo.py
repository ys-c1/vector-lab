#!/usr/bin/env python3
"""生成四类验收用 PGM（P5, maxval=255）：
  gradient.pgm 宽分布灰度，反解容易达标
  bimodal.pgm  只有两档灰度，台阶粗，用来触发不可达拒绝
  black.pgm    纯黑
  white.pgm    纯白
同时生成 fixed 模式的独立参照输出 fixed_ref.pgm，供逐字节比对。
"""
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "demo/data"
os.makedirs(OUT, exist_ok=True)
W = H = 64


def write_pgm(path, pix):
    assert len(pix) == W * H and all(0 <= v <= 255 for v in pix)
    with open(path, "wb") as f:
        f.write(f"P5\n{W} {H}\n255\n".encode())
        f.write(bytes(pix))


# 1. 渐变：行方向 0..255 线性渐变 + 列方向小幅扰动，灰度分布宽而均匀。
gradient = []
for y in range(H):
    for x in range(W):
        v = round(x * 255.0 / (W - 1) + (7 if y % 2 else 0))
        gradient.append(min(255, max(0, v)))
write_pgm(os.path.join(OUT, "gradient.pgm"), gradient)

# 2. 双峰：一半像素 64，一半 192，中间没有任何灰度 -> 占比只能取 1.0/0.5/0.0。
bimodal = []
for y in range(H):
    for x in range(W):
        bimodal.append(64 if x < W // 2 else 192)
write_pgm(os.path.join(OUT, "bimodal.pgm"), bimodal)

# 3/4. 纯黑、纯白。
write_pgm(os.path.join(OUT, "black.pgm"), [0] * (W * H))
write_pgm(os.path.join(OUT, "white.pgm"), [255] * (W * H))

# 独立参照：对渐变图按固定阈值 128 二值化（与 C 端 fixed 模式不同实现）。
fixed_ref = [255 if v >= 128 else 0 for v in gradient]
write_pgm(os.path.join(OUT, "fixed_ref.pgm"), fixed_ref)
print("generated 5 pgm files in", OUT)
