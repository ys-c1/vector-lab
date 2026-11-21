#!/usr/bin/env python3
"""stroke_outline 的独立参考实现与逐点核对。

几何完全独立推导：单位切向/左法向 -> 偏移线交点(斜接/内侧) ->
斜切双端点 -> 圆角用极角短弧细分。与 C 实现共用同一份输入路径，
逐点比较并核对 L 形手算结果。
"""
import csv
import math
import os
import subprocess
import sys

EPS = 1e-9
ROUND_STEP = math.radians(8.0)
MITER_LIMIT = 4.0
H = 1.0  # width 2.0
JOIN = {"auto": 0, "miter": 1, "bevel": 2, "round": 3}


def make_path(n_corner):
    pts = [(0.0, -20.0)]
    n = n_corner + 2
    for i in range(n_corner + 1):
        ang = -math.pi / 2 + i / n_corner * 1.5 * math.pi
        r = 18.0 if i % 2 else 30.0
        pts.append((r * math.cos(ang), r * math.sin(ang)))
    assert len(pts) == n
    return pts


def tangents(pts):
    T, L = [], []
    for a, b in zip(pts, pts[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy)
        tx, ty = dx / ln, dy / ln
        T.append((tx, ty))
        L.append((-ty, tx))
    return T, L


def meet(q1, u, q2, v):
    det = u[0] * v[1] - u[1] * v[0]
    s = ((q2[0] - q1[0]) * v[1] - (q2[1] - q1[1]) * v[0]) / det
    return q1[0] + u[0] * s, q1[1] + u[1] * s


def chain(pts, T, L, h, side, chosen):
    out = [(pts[0][0] + side * L[0][0] * h, pts[0][1] + side * L[0][1] * h)]
    for i in range(1, len(pts) - 1):
        a, b = T[i - 1], T[i]
        cr = a[0] * b[1] - a[1] * b[0]
        na = (side * L[i - 1][0], side * L[i - 1][1])
        nb = (side * L[i][0], side * L[i][1])
        q1 = (pts[i][0] + na[0] * h, pts[i][1] + na[1] * h)
        q2 = (pts[i][0] + nb[0] * h, pts[i][1] + nb[1] * h)
        outer = (side > 0) == (cr > 0.0)
        if abs(cr) <= EPS:
            out.append(q1)
            continue
        if not outer or chosen[i - 1] == "miter":
            # 内侧永远是两偏移线交点；外侧斜接同理
            out.append(meet(q1, a, q2, b))
        elif chosen[i - 1] == "bevel":
            # 仅外侧斜切：入边/出边两个偏移端点
            out.append(q1)
            out.append(q2)
        else:  # round
            a0, a1 = math.atan2(na[1], na[0]), math.atan2(nb[1], nb[0])
            sweep = a1 - a0
            while sweep > math.pi:
                sweep -= 2 * math.pi
            while sweep <= -math.pi:
                sweep += 2 * math.pi
            kseg = max(1, math.ceil(abs(sweep) / ROUND_STEP))
            out.append(q1)
            for k in range(1, kseg + 1):
                ang = a0 + sweep * k / kseg
                out.append((pts[i][0] + math.cos(ang) * h,
                            pts[i][1] + math.sin(ang) * h))
    out.append((pts[-1][0] + side * L[-1][0] * h,
                pts[-1][1] + side * L[-1][1] * h))
    return out


def select_joins(pts, T, mode):
    chosen, info = [], []
    for i in range(1, len(pts) - 1):
        a, b = T[i - 1], T[i]
        cr = a[0] * b[1] - a[1] * b[0]
        dot = max(-1.0, min(1.0, a[0] * b[0] + a[1] * b[1]))
        delta = math.acos(dot)
        gamma = math.pi - delta
        ratio = 1.0 / math.cos(delta / 2)
        if mode == "auto":
            if abs(cr) <= EPS:
                j = "miter"
            elif gamma >= math.radians(120):
                j = "miter"
            elif gamma < math.radians(60):
                j = "round"
            else:
                j = "bevel"
        else:
            j = mode
        chosen.append(j)
        info.append((i, math.degrees(gamma), j, ratio))
    return chosen, info


def outline(pts, mode):
    T, L = tangents(pts)
    chosen, info = select_joins(pts, T, mode)
    left = chain(pts, T, L, H, +1, chosen)
    right = chain(pts, T, L, H, -1, chosen)
    return left + right[::-1], info


def area(ring):
    return abs(sum(a[0] * b[1] - b[0] * a[1]
                   for a, b in zip(ring, ring[1:] + ring[:1]))) * 0.5


def dump_rings(stroke_dir):
    """让 C 编译器输出各策略轮廓，dumper 位于 stroke/ 根目录。"""
    bench = os.path.join(stroke_dir, "bench")
    exe = os.path.join(bench, "build", "dump")
    src = os.path.join(stroke_dir, "dump.c")
    subprocess.run(["cc", "-Isrc", "-O2", src, "src/stroke.c", "-lm", "-o", exe],
                   check=True, cwd=stroke_dir)
    out = subprocess.run([exe], capture_output=True, text=True, check=True)
    rings, rejected = {}, {}
    cur = None
    for line in out.stdout.splitlines():
        if line.startswith("MODE "):
            parts = line.split()
            cur = parts[1]
            if len(parts) > 2 and parts[2] == "REJECTED":
                rejected[cur] = (int(parts[3].split("=")[1]),
                                 int(parts[4].split("=")[1]))
                cur = None
            else:
                rings[cur] = []
        elif cur:
            x, y = line.split()
            rings[cur].append((float(x), float(y)))
    return rings, rejected


def main():
    root = os.path.dirname(__file__)
    c_rings, c_rejected = dump_rings(os.path.join(root, ".."))
    failures = 0
    print("== 逐点核对 C vs Python 参考 ==")
    for nc in (8, 64):
        pts = make_path(nc)
        T, _ = tangents(pts)
        for mode in ("miter", "bevel", "round", "auto"):
            key = f"{nc}_{mode}"
            # Python 侧同样计算斜接比率，判断该策略是否应被拒
            _, info = select_joins(pts, T, mode)
            py_reject = any(j == "miter" and ratio > MITER_LIMIT
                            for _, _, j, ratio in info)
            if py_reject:
                if key in c_rejected and c_rejected[key][0] == -4:
                    print(f"  nc={nc:2d} {mode:6s} 双方均拒绝 rc=-4 "
                          f"corner={c_rejected[key][1]} OK")
                else:
                    print(f"  nc={nc} {mode}: 应拒绝但 C 未拒绝 FAIL")
                    failures += 1
                continue
            ref, _ = outline(pts, mode)
            got = c_rings.get(key)
            if got is None:
                print(f"  nc={nc} {mode}: C 意外拒绝 FAIL")
                failures += 1
                continue
            if len(ref) != len(got):
                print(f"  nc={nc} {mode}: 点数不符 {len(ref)} != {len(got)}")
                failures += 1
                continue
            worst = max(math.hypot(a[0] - b[0], a[1] - b[1])
                        for a, b in zip(ref, got))
            ok = worst < 1e-9
            failures += 0 if ok else 1
            print(f"  nc={nc:2d} {mode:6s} n={len(ref):4d} "
                  f"area={area(ref):.6f} max|d|={worst:.2e} {'OK' if ok else 'FAIL'}")

    print("== L 形手算三接法（h=1）==")
    Lp = [(0, 0), (10, 0), (10, 10)]
    hand = {
        # 与 C 输出逐点一致的期望；面积为独立鞋带公式核对值
        "miter": ([(0, 1), (9, 1), (9, 10), (11, 10), (11, -1), (0, -1)], 40.0),
        "bevel": ([(0, 1), (10, 1), (9, 0), (9, 10),
                   (11, 10), (11, -1), (0, -1)], 40.5),
    }
    for mode in ("miter", "bevel", "round"):
        ring, _ = outline(Lp, mode)
        print(f"  {mode:6s} n={len(ring):2d} area={area(ring):.6f}")
    for mode, (hr, ha) in hand.items():
        ring, _ = outline(Lp, mode)
        match = len(ring) == len(hr) and all(
            abs(a[0] - b[0]) < 1e-9 and abs(a[1] - b[1]) < 1e-9
            for a, b in zip(ring, hr))
        if ha is not None:
            match &= abs(area(ring) - ha) < 1e-9
        print(f"  手算 {mode}: {'OK' if match else 'FAIL'}")
        failures += 0 if match else 1

    print("== 选择表导出核对 ==")
    csv_path = os.path.join(root, "bench", "selection.csv")
    if os.path.exists(csv_path):
        rows = list(csv.DictReader(open(csv_path)))
        print(f"  {csv_path}: {len(rows)} 行")
        for nc in (8, 64):
            pts = make_path(nc)
            _, info = outline(pts, "auto")
            sub = [r for r in rows if int(r["corners"]) == nc]
            ok = (len(sub) == len(info) and
                  all(abs(float(r["angle_deg"]) - ang) < 1e-6 and
                      r["chosen_join"] == j for r, (_, ang, j, _) in zip(sub, info)))
            print(f"  nc={nc} 选择表与参考一致: {'OK' if ok else 'FAIL'}")
            failures += 0 if ok else 1
    print("RESULT", "ALL OK" if failures == 0 else f"{failures} FAILURES")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
