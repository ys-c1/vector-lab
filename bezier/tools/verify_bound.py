#!/usr/bin/env python3
"""独立验证弦高误差上界 (3/4)*max(|P0-2P1+P2|,|P1-2P2+P3|)。

纯 Python 复算，不链接 C 实现：随机生成大量三次贝塞尔，
对每段密集采样求曲线到端点弦的最大距离，与二阶差分上界比较。
"""
import math
import random
import sys


def evaluate(c, t):
    u = 1.0 - t
    return (u ** 3 * c[0][0] + 3 * u * u * t * c[1][0] + 3 * u * t * t * c[2][0] + t ** 3 * c[3][0],
            u ** 3 * c[0][1] + 3 * u * u * t * c[1][1] + 3 * u * t * t * c[2][1] + t ** 3 * c[3][1])


def dist_point_segment(p, a, b):
    vx, vy = b[0] - a[0], b[1] - a[1]
    l2 = vx * vx + vy * vy
    if l2 == 0.0:
        return math.hypot(p[0] - a[0], p[1] - a[1])
    s = ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / l2
    s = max(0.0, min(1.0, s))
    return math.hypot(p[0] - (a[0] + s * vx), p[1] - (a[1] + s * vy))


def second_diff(c):
    ax, ay = c[0][0] - 2 * c[1][0] + c[2][0], c[0][1] - 2 * c[1][1] + c[2][1]
    bx, by = c[1][0] - 2 * c[2][0] + c[3][0], c[1][1] - 2 * c[2][1] + c[3][1]
    return max(math.hypot(ax, ay), math.hypot(bx, by))


def main():
    random.seed(20261123)
    worst = 0.0
    worst_c = None
    trials = 5000
    samples = 400
    for _ in range(trials):
        c = [(random.uniform(-50, 50), random.uniform(-50, 50)) for _ in range(4)]
        m = second_diff(c)
        if m < 1e-12:
            continue
        measured = 0.0
        for i in range(samples + 1):
            p = evaluate(c, i / samples)
            measured = max(measured, dist_point_segment(p, c[0], c[3]))
        ratio = measured / m
        if ratio > worst:
            worst, worst_c = ratio, c
    limit = 0.75
    print(f"trials={trials} samples={samples} worst measured/bound={worst:.12f} limit={limit}")
    if worst_c is not None:
        print("worst curve:", worst_c)
    if worst <= limit:
        print("OK: chord-height bound (3/4) holds on all samples")
        return 0
    print("FAIL: observed ratio exceeds 3/4")
    return 1


if __name__ == "__main__":
    sys.exit(main())
