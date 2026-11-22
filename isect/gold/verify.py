#!/usr/bin/env python3 -I
"""独立于 gold/ref.c 的精确核验：有理数 Fraction 定向谓词 + 精确距离吸附，
逐对比较。用法: verify.py <master.txt> <gold.out> [snap]
"""
import sys
from fractions import Fraction


def load(path):
    segs = []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 4:
                x0, y0, x1, y1 = (Fraction(p) for p in parts)
                segs.append(((x0, y0), (x1, y1)))
    return segs


def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def in_box(a, b, p):
    return (min(a[0], b[0]) <= p[0] <= max(a[0], b[0])
            and min(a[1], b[1]) <= p[1] <= max(a[1], b[1]))


def on_seg(s, p):
    a, b = s
    return orient(a, b, p) == 0 and in_box(a, b, p)


def hit_strict(u, v):
    (a, b), (c, d) = u, v
    uz, vz = a == b, c == d
    if uz and vz:
        return a == c
    if uz:
        return on_seg(v, a)
    if vz:
        return on_seg(u, c)
    o1, o2 = orient(a, b, c), orient(a, b, d)
    o3, o4 = orient(c, d, a), orient(c, d, b)
    if ((o1 > 0 > o2 or o1 < 0 < o2) and (o3 > 0 > o4 or o3 < 0 < o4)):
        return True
    if o1 == 0 and on_seg(u, c):
        return True
    if o2 == 0 and on_seg(u, d):
        return True
    if o3 == 0 and on_seg(v, a):
        return True
    if o4 == 0 and on_seg(v, b):
        return True
    if 0 == o1 == o2 == o3 == o4:
        if a[0] != b[0] or c[0] != d[0]:
            return max(min(a[0], b[0]), min(c[0], d[0])) <= min(max(a[0], b[0]), max(c[0], d[0]))
        return max(min(a[1], b[1]), min(c[1], d[1])) <= min(max(a[1], b[1]), max(c[1], d[1]))
    return False


def dist2_point_seg(p, s):
    a, b = s
    if a == b:
        return (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2
    vx, vy = b[0] - a[0], b[1] - a[1]
    l2 = vx * vx + vy * vy
    wx, wy = p[0] - a[0], p[1] - a[1]
    t = (wx * vx + wy * vy) / l2
    if t <= 0:
        q = a
    elif t >= 1:
        q = b
    else:
        q = (a[0] + vx * t, a[1] + vy * t)
    return (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2


def within_snap(u, v, snap):
    if hit_strict(u, v):
        return True
    if snap == 0:
        return False
    s2 = Fraction(snap) ** 2
    # 与 gold/ref.c 同口径：四个端点对另一线段的垂距取 min
    pts = [u[0], u[1], v[0], v[1]]
    segs = [v, v, u, u]
    for p, s in zip(pts, segs):
        a, b = s
        if a == b:
            d2 = (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2
        else:
            vx, vy = b[0] - a[0], b[1] - a[1]
            l2 = vx * vx + vy * vy
            wx, wy = p[0] - a[0], p[1] - a[1]
            tnum = wx * vx + wy * vy
            if tnum <= 0:
                d2 = (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2
            elif tnum >= l2:
                d2 = (p[0] - b[0]) ** 2 + (p[1] - b[1]) ** 2
            else:
                cr = vx * wy - vy * wx
                d2 = cr * cr / l2
        if d2 <= s2:
            return True
    return False


def main():
    snap = Fraction(sys.argv[3]) if len(sys.argv) > 3 else Fraction(0)
    segs = load(sys.argv[1])
    gold = set()
    with open(sys.argv[2]) as f:
        header = f.readline()
        for line in f:
            i, j = (int(x) for x in line.split())
            gold.add((i, j))
    mine = {(i, j) for i in range(len(segs)) for j in range(i + 1, len(segs))
            if within_snap(segs[i], segs[j], snap)}
    if mine == gold:
        print(f"AGREE n={len(segs)} pairs={len(gold)} snap={snap}  ({header.strip()})")
        return 0
    print("MISMATCH", "only_gold=", sorted(gold - mine), "only_py=", sorted(mine - gold))
    return 1


if __name__ == "__main__":
    sys.exit(main())
