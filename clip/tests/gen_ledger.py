#!/usr/bin/env python3
"""生成逐刀账目验证用的四组窗口序列（纯几何，不依赖被测代码）。

输出目录：clip/tests/ledger/
  poly_*.txt            主体多边形
  win_*.txt             窗口（凸多边形）
  seq1.txt .. seq4.txt  每个窗口清单 <窗口号> <窗口文件>
  plan.txt              四组序列的 plan

序列覆盖：轴对齐两刀、120 顶点主体 + 旋转窗口、三刀、顺时针窗口。
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "ledger")


def write(name, pts):
    with open(os.path.join(OUT, name), "w") as f:
        for x, y in pts:
            f.write("%.17g %.17g\n" % (x, y))


def rect(x0, y0, x1, y1, cw=False):
    p = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return list(reversed(p)) if cw else p


def regular(n, r, cx, cy, rot):
    return [(cx + r * math.cos(rot + 2 * math.pi * i / n),
             cy + r * math.sin(rot + 2 * math.pi * i / n)) for i in range(n)]


def main():
    os.makedirs(OUT, exist_ok=True)

    # 序列 1：轴对齐两刀
    write("poly_hex.txt", [(0, 0), (160, 0), (200, 80), (150, 160),
                           (40, 170), (-30, 100), (-20, 30)])
    write("win_1a.txt", rect(20, 20, 130, 130))
    write("win_1b.txt", rect(60, 30, 180, 120))
    with open(os.path.join(OUT, "seq1.txt"), "w") as f:
        f.write("101 win_1a.txt\n102 win_1b.txt\n")

    # 序列 2：120 顶点主体 + 两个旋转窗口（裁剪后顶点数超过 64）
    write("poly_120.txt", regular(120, 200.0, 0.0, 0.0, 0.03))
    write("win_2a.txt", regular(4, 75.0, 90.0, 80.0, 0.35))
    write("win_2b.txt", regular(4, 85.0, 120.0, 70.0, 0.9))
    with open(os.path.join(OUT, "seq2.txt"), "w") as f:
        f.write("201 win_2a.txt\n202 win_2b.txt\n")

    # 序列 3：三刀
    write("poly_tri.txt", [(-10, -20), (220, -10), (120, 190)])
    write("win_3a.txt", [(30, 40), (150, 20), (110, 130)])
    write("win_3b.txt", rect(50, 50, 190, 140))
    write("win_3c.txt", regular(4, 70.0, 120.0, 90.0, 0.6))
    with open(os.path.join(OUT, "seq3.txt"), "w") as f:
        f.write("301 win_3a.txt\n302 win_3b.txt\n303 win_3c.txt\n")

    # 序列 4：第一刀顺时针，第二刀旋转
    write("poly_oct.txt", [(-60, 10), (-20, -80), (80, -100), (170, -40),
                           (190, 60), (130, 150), (20, 170), (-50, 120)])
    write("win_4a.txt", rect(20, 20, 140, 140, cw=True))
    write("win_4b.txt", regular(4, 80.0, 110.0, 90.0, 0.45))
    with open(os.path.join(OUT, "seq4.txt"), "w") as f:
        f.write("401 win_4a.txt\n402 win_4b.txt\n")

    # 拒绝用例
    write("win_empty.txt", [])
    write("win_two.txt", [(0, 0), (10, 0)])
    write("win_nonconvex.txt", [(0, 0), (100, 0), (40, 30), (100, 100), (0, 100)])
    with open(os.path.join(OUT, "seq_bad_empty.txt"), "w") as f:
        f.write("901 win_1a.txt\n902 win_empty.txt\n")
    with open(os.path.join(OUT, "seq_bad_two.txt"), "w") as f:
        f.write("903 win_two.txt\n")
    with open(os.path.join(OUT, "seq_bad_dup.txt"), "w") as f:
        f.write("904 win_1a.txt\n905 win_1b.txt\n904 win_2a.txt\n")
    with open(os.path.join(OUT, "seq_bad_nonconvex.txt"), "w") as f:
        f.write("906 win_nonconvex.txt\n")

    with open(os.path.join(OUT, "plan.txt"), "w") as f:
        f.write("# 序列名 多边形文件 窗口清单\n")
        f.write("seq1_axis2 poly_hex.txt seq1.txt\n")
        f.write("seq2_rot120 poly_120.txt seq2.txt\n")
        f.write("seq3_three poly_tri.txt seq3.txt\n")
        f.write("seq4_cw poly_oct.txt seq4.txt\n")
    for name, poly, seq in [
        ("rej_empty", "poly_hex.txt", "seq_bad_empty.txt"),
        ("rej_two", "poly_hex.txt", "seq_bad_two.txt"),
        ("rej_dup", "poly_hex.txt", "seq_bad_dup.txt"),
        ("rej_nonconvex", "poly_hex.txt", "seq_bad_nonconvex.txt"),
    ]:
        with open(os.path.join(OUT, "plan_%s.txt" % name), "w") as f:
            f.write("%s %s %s\n" % (name, poly, seq))

    print("ledger fixtures written to", OUT)


if __name__ == "__main__":
    main()
