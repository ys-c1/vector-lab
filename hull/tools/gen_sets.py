#!/usr/bin/env python3
"""生成 hull 精简演示用点集，输出到 hull/data/<name>.txt，每行 "x y"。"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "data")


def write(name, pts):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".txt")
    with open(path, "w") as f:
        for x, y in pts:
            f.write(f"{x:.6f} {y:.6f}\n")
    print(f"{name}: {len(pts)} points -> {path}")


def circle():
    # 48 点近圆：圆周均布，带微小确定性抖动
    pts = []
    for i in range(48):
        a = 2.0 * math.pi * i / 48.0
        r = 10.0 + 0.15 * math.sin(7.0 * a)
        pts.append((round(r * math.cos(a), 4), round(r * math.sin(a), 4)))
    return pts


def ellipse():
    # 40 点椭圆 + 每条长轴边上各 3 个共线点
    pts = []
    for i in range(40):
        a = 2.0 * math.pi * i / 40.0
        pts.append((round(12.0 * math.cos(a), 4), round(6.0 * math.sin(a), 4)))
    for t in (0.25, 0.5, 0.75):
        pts.append((-12.0 + 24.0 * t, 0.0))   # 长轴水平直径上的共线点
        pts.append((0.0, -6.0 + 12.0 * t))    # 短轴竖直直径上的共线点
    return pts


def blob():
    # 60 点不规则凸形：半径多频扰动，仍保持严格凸
    rng = random.Random(20261123)
    n = 60
    radii = [10.0 + 1.2 * math.sin(3.0 * a) + 0.6 * math.cos(5.0 * a)
             for a in (2.0 * math.pi * i / n for i in range(n))]
    pts = []
    for i in range(n):
        a = 2.0 * math.pi * i / n
        jitter = rng.uniform(-0.05, 0.05)
        r = radii[i] + jitter
        pts.append((round(r * math.cos(a), 4), round(r * math.sin(a), 4)))
    return pts


def rect_bulge():
    # 矩形四边各 8 个共线点 + 四角外侧 8 个凸起点，共 40 点；
    # 共线点剔除后只剩 12 个严格凸顶点
    pts = []
    for t in (1.0 / 8, 2.0 / 8, 3.0 / 8, 5.0 / 8, 6.0 / 8, 7.0 / 8):
        pts.append((-10.0 + 20.0 * t, -6.0))
        pts.append((-10.0 + 20.0 * t, 6.0))
        pts.append((-10.0, -6.0 + 12.0 * t))
        pts.append((10.0, -6.0 + 12.0 * t))
    pts += [(-10.0, -6.0), (10.0, -6.0), (10.0, 6.0), (-10.0, 6.0)]
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            pts.append((sx * 10.8, sy * 6.0))
            pts.append((sx * 10.0, sy * 6.8))
    return pts


def flat():
    # 全共线：严格凸包只有 2 个端点，必须拒并
    return [(float(i), 0.5 * i) for i in range(12)]


def main():
    sets = {
        "circle": circle(),
        "ellipse": ellipse(),
        "blob": blob(),
        "rect_bulge": rect_bulge(),
        "flat": flat(),
    }
    for name, pts in sets.items():
        write(name, pts)


if __name__ == "__main__":
    sys.exit(main())
