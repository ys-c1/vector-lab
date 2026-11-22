#!/usr/bin/env python3
"""生成裁剪验证用的测试数据：多边形、窗口、清单。
所有输出都是纯文本 "x y"，不依赖任何被测代码。"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def write(name, pts):
    with open(os.path.join(HERE, name), "w") as f:
        for x, y in pts:
            f.write(f"{x:.6f} {y:.6f}\n")


def rect(x0, y0, x1, y1, cw=False):
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return list(reversed(pts)) if cw else pts


def regular(n, r, rot=0.0):
    return [(r * math.cos(rot + 2 * math.pi * i / n),
             r * math.sin(rot + 2 * math.pi * i / n)) for i in range(n)]


# 1) 基础矩形：一刀（合并窗口）vs 两刀（逐窗口）结果必须一致
#    win_a ∩ win_b = [5,15]x[2,8] = win_union，且三者都完全落在多边形内
write("poly_rect.txt", rect(0, 0, 20, 10))
write("win_a.txt", rect(5, -5, 15, 8))                 # 第一刀
write("win_b.txt", rect(-2, 2, 18, 12))                # 第二刀
write("win_union.txt", rect(5, 2, 15, 8))              # 两刀的交集 = 一刀
with open(os.path.join(HERE, "manifest_two.txt"), "w") as f:
    f.write("1 win_a.txt\n2 win_b.txt\n")
with open(os.path.join(HERE, "manifest_one.txt"), "w") as f:
    f.write("7 win_union.txt\n")

# 2) 顺时针窗口：旧实现会把窗口内部裁掉
write("win_cw.txt", rect(2, 2, 8, 8, cw=True))
with open(os.path.join(HERE, "manifest_cw.txt"), "w") as f:
    f.write("3 win_cw.txt\n")

# 3) 大顶点多边形，逐刀裁剪会超过旧实现 62 点的静默截断
big = regular(120, 100.0, rot=0.03)
write("poly_big.txt", big)
write("win_big.txt", rect(-40, -40, 60, 50))
with open(os.path.join(HERE, "manifest_big.txt"), "w") as f:
    f.write("4 win_big.txt\n")

# 4) 非轴对齐多边形（三角形 + 旋转正方形），走 gold 容差路径
tri = [(0.0, 0.0), (12.0, 1.0), (4.0, 9.0)]
write("poly_tri.txt", tri)
sq = regular(4, 7.0, rot=math.pi / 5)
write("win_rot.txt", sq)
with open(os.path.join(HERE, "manifest_rot.txt"), "w") as f:
    f.write("5 win_rot.txt\n")

# 5) 拒绝用例：空窗口、两点窗口、重复窗口号
write("win_empty.txt", [])
write("win_two.txt", [(0.0, 0.0), (1.0, 1.0)])
with open(os.path.join(HERE, "manifest_bad_empty.txt"), "w") as f:
    f.write("1 win_a.txt\n2 win_empty.txt\n")
with open(os.path.join(HERE, "manifest_bad_two.txt"), "w") as f:
    f.write("1 win_two.txt\n")
with open(os.path.join(HERE, "manifest_bad_dupid.txt"), "w") as f:
    f.write("1 win_a.txt\n1 win_b.txt\n")
# 退化窗口（相邻顶点重合）
write("win_degenerate.txt", [(0, 0), (10, 0), (10, 0), (10, 10), (0, 10)])
with open(os.path.join(HERE, "manifest_bad_degenerate.txt"), "w") as f:
    f.write("6 win_degenerate.txt\n")

print("test data written to", HERE)
