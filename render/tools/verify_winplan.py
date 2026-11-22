#!/usr/bin/env python3
"""独立复算贪心窗口规划（绕数法判定图形像素，与 C 端射线交叉法不同）。

输入 C 端 winplan_cli 的输出：
    plan stop=... nwins=... coverage=...
    plan sum_area=... union_area=...
    win <k> <x> <y> <w> <h>
    reject #k at (x,y) gain=... overlap_would_be=...
（win 行的第一个数字是该窗口的序号，1 起。）

复算项：
  - 图形像素集合：按场景包围盒映射到 image_w x image_h，每像素 4x4 子采样，
    任一子点在内即亮（与 raster_fill 的 any-hit 口径一致）；
  - 每个窗口按贪心次序是否确为“当时未覆盖像素增益最大”的候选（并列取扫描
    序第一个）；
  - 覆盖率、窗口并集面积、重叠率；
  - 拒绝条目：若接受该窗口，重叠率应等于记录值且超过上限。

用法：verify_winplan.py <scene.txt> <cli_log> <image_w> <image_h> \
                         <win_w> <win_h> <stride> <target> <cap> [scale]
退出码 0 全部一致，1 有不一致，2 参数/IO 错误。
"""
import sys

SS = 4


def load_polys(path):
    polys = []
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t or t[0] != 'p':
                continue
            v = [float(x) for x in t[1:]]
            pts = list(zip(v[0::2], v[1::2]))
            if len(pts) >= 3:
                polys.append(pts)
    return polys


def bbox(polys):
    xs = [x for p in polys for x, _ in p]
    ys = [y for p in polys for _, y in p]
    return min(xs), min(ys), max(xs), max(ys)


def winding_inside(px, py, poly):
    """绕数法（与 C 端的奇偶射线交叉法是不同算法）。"""
    wn = 0
    n = len(poly)
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % n]
        cross = (bx - ax) * (py - ay) - (px - ax) * (by - ay)
        if ay <= py:
            if by > py and cross > 0:
                wn += 1
        else:
            if by <= py and cross < 0:
                wn -= 1
    return wn != 0


def geom_mask(polys, W, H, scale):
    minx, miny, maxx, maxy = bbox(polys)
    minx *= scale
    miny *= scale
    maxx *= scale
    maxy *= scale
    dx = (maxx - minx) / W
    dy = (maxy - miny) / H
    m = bytearray(W * H)
    for y in range(H):
        for x in range(W):
            hit = 0
            for j in range(SS):
                py = miny + (y + (j + 0.5) / SS) * dy
                for i in range(SS):
                    px = minx + (x + (i + 0.5) / SS) * dx
                    for p in polys:
                        sp = [(sx * scale, sy * scale) for sx, sy in p]
                        if winding_inside(px, py, sp):
                            hit = 1
                            break
                    if hit:
                        break
                if hit:
                    break
            m[y * W + x] = hit
    return m


def positions(dim, win, stride):
    if win >= dim:
        return [0]
    pos = list(range(0, dim - win + 1, stride))
    if pos[-1] != dim - win:
        pos.append(dim - win)
    return pos


def union_area(rects):
    if not rects:
        return 0
    xs = sorted({r[0] for r in rects} | {r[2] for r in rects})
    area = 0
    for a, b in zip(xs, xs[1:]):
        if b <= a:
            continue
        iv = sorted((r[1], r[3]) for r in rects if r[0] <= a and r[2] >= b)
        if not iv:
            continue
        tot = 0
        cs, ce = iv[0]
        for s, e in iv[1:]:
            if s > ce:
                tot += ce - cs
                cs, ce = s, e
            elif e > ce:
                ce = e
        tot += ce - cs
        area += tot * (b - a)
    return area


def parse_log(path):
    stop = None
    sum_area = union = None
    coverage = None
    wins = []
    rejects = []
    with open(path) as f:
        for line in f:
            t = line.split()
            if line.startswith("plan stop="):
                for tok in t[1:]:
                    k, _, v = tok.partition('=')
                    if k == 'stop':
                        stop = v
                    elif k == 'coverage':
                        coverage = float(v)
            elif line.startswith('plan sum_area='):
                sum_area = int(t[1].split('=')[1])
                union = int(t[2].split('=')[1])
            elif t and t[0] == 'win' and len(t) == 6:
                wins.append(tuple(int(v) for v in t[2:]))
            elif t and t[0] == 'reject' and t[1].startswith('#'):
                # reject #k after=<n> at (x,y) gain=<g> overlap_would_be=<o> (cap <c>)
                after = int(t[2].split('=')[1])
                x, y = t[4].strip('()').split(',')
                gain = int(t[5].split('=')[1])
                ov = float(t[6].split('=')[1])
                rejects.append((int(x), int(y), gain, ov, after))
    return stop, sum_area, union, coverage, wins, rejects


def main(argv):
    if len(argv) < 10:
        print(__doc__)
        return 2
    scene, logpath = argv[1], argv[2]
    W, H = int(argv[3]), int(argv[4])
    ww, wh = int(argv[5]), int(argv[6])
    stride = int(argv[7])
    target = float(argv[8])
    cap = float(argv[9])
    max_wins = int(argv[10]) if len(argv) > 10 else 256
    scale = 1.0

    polys = load_polys(scene)
    if not polys:
        print("no polys")
        return 2
    on = geom_mask(polys, W, H, scale)
    xs = positions(W, ww, stride)
    ys = positions(H, wh, stride)
    total = sum(on)

    cov = bytearray(W * H)
    problems = []

    def gain(x0, y0):
        g = 0
        for y in range(y0, min(H, y0 + wh)):
            base = y * W
            for x in range(x0, min(W, x0 + ww)):
                if on[base + x] and not cov[base + x]:
                    g += 1
        return g

    def union(rs):
        return union_area([(x, y, min(W, x + ww), min(H, y + wh)) for x, y in rs])

    stop_c, sum_c, union_c, cov_c, wins_c, rejects = parse_log(logpath)

    # 独立重放整条贪心循环（不抄 C 的 win 列表）：同一套规则、同一候选网格，
    # 但几何掩码用绕数法另算。
    dead = set()
    ref_wins = []
    ref_rejects = []
    stop_ref = None
    cov = bytearray(W * H)
    covered = 0
    sum_v = 0
    union_v = 0
    while True:
        bx = by = bk = None
        bg = 0
        for kk in range(len(ys) * len(xs)):
            if kk in dead:
                continue
            x0, y0 = xs[kk % len(xs)], ys[kk // len(xs)]
            g = gain(x0, y0)
            if g > bg:
                bg, bx, by, bk = g, x0, y0, kk
        if bk is None or bg == 0:
            stop_ref = 'OVERLAP' if ref_rejects else 'NOGAIN'
            break
        if len(ref_wins) >= max_wins:
            stop_ref = 'MAXWIN'
            break
        rs = ref_wins + [(bx, by)]
        u = union(rs)
        s = len(rs) * ww * wh
        ov = (s - u) / s
        if ov > cap + 1e-12:
            ref_rejects.append((bx, by, bg, ov, len(ref_wins)))
            dead.add(bk)
            continue
        ref_wins.append((bx, by))
        sum_v, union_v = s, u
        for y in range(by, min(H, by + wh)):
            base = y * W
            for x in range(bx, min(W, bx + ww)):
                if on[base + x] and not cov[base + x]:
                    cov[base + x] = 1
                    covered += 1
        if covered == total:
            stop_ref = 'FULL'
            break
        if covered / total >= target - 1e-12:
            stop_ref = 'TARGET'
            break

    if ref_wins != [w[:2] for w in wins_c]:
        for i, (a, b) in enumerate(zip(ref_wins, [(w[0], w[1]) for w in wins_c]), 1):
            if a != b:
                problems.append(f"win {i}: ref {a} != C {b}")
                break
        if len(ref_wins) != len(wins_c):
            problems.append(f"nwins ref {len(ref_wins)} != C {len(wins_c)}")
    if stop_ref != stop_c:
        problems.append(f"stop ref {stop_ref} != C {stop_c}")
    for k, (wx, wy, ww2, wh2) in enumerate(wins_c, 1):
        if (ww2, wh2) != (ww, wh):
            problems.append(f"win {k}: size {ww2}x{wh2} != {ww}x{wh}")

    covered = sum(1 for i in range(W * H) if on[i] and cov[i])
    coverage = covered / total if total else 1.0
    sum_area = len(ref_wins) * ww * wh
    overlap = (sum_area - union_v) / sum_area if sum_area else 0.0

    if sum_c is not None and sum_area != sum_c:
        problems.append(f"sum_area {sum_area} != C {sum_c}")
    if union_c is not None and union_v != union_c:
        problems.append(f"union_area {union_v} != C {union_c}")
    if cov_c is not None and abs(coverage - cov_c) > 5e-7:
        problems.append(f"coverage {coverage:.9f} != C {cov_c:.9f}")

    for (rx, ry, rgain, rov, after) in rejects:
        base_rs = ref_wins[:after]
        rs = base_rs + [(rx, ry)]
        u2 = union(rs)
        s2 = len(rs) * ww * wh
        ov2 = (s2 - u2) / s2
        if abs(ov2 - rov) > 1e-6:
            problems.append(f"reject ({rx},{ry}): overlap {ov2:.9f} != {rov:.9f}")
        if ov2 <= cap + 1e-12:
            problems.append(f"reject ({rx},{ry}): overlap {ov2:.6f} <= cap {cap}")
        # 拒绝时的增益：在此前未覆盖集合上复算。
        cov2 = bytearray(W * H)
        for (wx, wy) in base_rs:
            for y in range(wy, min(H, wy + wh)):
                b = y * W
                for x in range(wx, min(W, wx + ww)):
                    cov2[b + x] = 1
        g = 0
        for y in range(ry, min(H, ry + wh)):
            b = y * W
            for x in range(rx, min(W, rx + ww)):
                if on[b + x] and not cov2[b + x]:
                    g += 1
        if g != rgain:
            problems.append(f"reject ({rx},{ry}): gain {g} != reported {rgain}")

    if stop_c == 'OVERLAP' and not rejects:
        problems.append("OVERLAP stop with no rejects listed")
    if stop_c == 'FULL' and covered != total:
        problems.append(f"FULL but covered {covered}/{total}")
    if stop_c == 'TARGET' and coverage + 1e-12 < target:
        problems.append(f"TARGET but coverage {coverage:.6f} < {target}")
    if stop_c in ('FULL', 'TARGET') and overlap > cap + 1e-12:
        problems.append(f"overlap {overlap:.6f} exceeds cap {cap}")

    print(f"ref on_pixels={total} covered={covered} coverage={coverage:.6f} "
          f"nwins={len(ref_wins)} sum_area={sum_area} union_area={union_v} "
          f"overlap={overlap:.6f} stop={stop_c} rejects={len(rejects)} "
          f"candidates={len(xs)*len(ys)}")
    for p in problems:
        print("MISMATCH " + p)
    if problems:
        return 1
    print("MATCH")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
