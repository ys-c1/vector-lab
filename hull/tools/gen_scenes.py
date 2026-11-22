#!/usr/bin/env python3
"""把 hull/data 的点集转成 render CLI 的场景格式：
每行一个多边形，行首 'p'，后跟 "x y" 序列。"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
OUT = os.path.join(HERE, "..", "scenes")


def main():
    os.makedirs(OUT, exist_ok=True)
    names = ["circle", "ellipse", "blob", "rect_bulge"]
    for name in names:
        pts = []
        with open(os.path.join(DATA, name + ".txt")) as f:
            for line in f:
                line = line.strip()
                if line:
                    pts.append(line)
        with open(os.path.join(OUT, name + ".scene"), "w") as f:
            f.write("p " + " ".join(pts) + "\n")
        print(f"{name}: {len(pts)} verts")


if __name__ == "__main__":
    sys.exit(main())
