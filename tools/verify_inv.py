#!/usr/bin/env python3 -I
"""独立核验反解缩放结果（绕开 C 实现，改用绕数法判定点在多边形内）。
用法: verify.py <scene.txt> <out.pgm> <w> <h> <scale> <target_density>
"""
import sys, math

SS = 4

def load(path):
    polys = []
    with open(path) as f:
        for line in f:
            v = line.split()
            if v and v[0] == 'p':
                nums = list(map(float, v[1:]))
                polys.append(list(zip(nums[0::2], nums[1::2])))
    return polys

def winding(px, py, poly):
    """绕数法：与 C 端的射线交叉法是不同算法。"""
    wn = 0
    for i in range(len(poly)):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % len(poly)]
        cross = (bx - ax) * (py - ay) - (px - ax) * (by - ay)
        if ay <= py:
            if by > py and cross > 0:
                wn += 1
        else:
            if by <= py and cross < 0:
                wn -= 1
    return wn != 0

def bbox(polys):
    xs = [p[0] for q in polys for p in q]
    ys = [p[1] for q in polys for p in q]
    return min(xs), min(ys), max(xs), max(ys)

def area(poly):
    return abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1]
                   - poly[(i + 1) % len(poly)][0] * poly[i][1]
                   for i in range(len(poly)))) * 0.5

def sample(polys, win, w, h, gx0, gy0, gx1, gy1):
    """窗口格距与采样相位固定；格号范围由调用方给定，可超出窗口。"""
    dx = (win[2] - win[0]) / w
    dy = (win[3] - win[1]) / h
    on = set()
    for gy in range(gy0, gy1):
        for gx in range(gx0, gx1):
            for j in range(SS):
                hit = False
                for i in range(SS):
                    px = win[0] + (gx + (i + 0.5) / SS) * dx
                    py = win[1] + (gy + (j + 0.5) / SS) * dy
                    if any(winding(px, py, q) for q in polys):
                        hit = True
                        break
                if hit:
                    on.add((gx, gy))
                    break
    return on

def read_pgm(path):
    with open(path, 'rb') as f:
        data = f.read()
    toks, i = [], 0
    while len(toks) < 4:
        while data[i] in b' \t\n\r':
            i += 1
        j = i
        while data[j] not in b' \t\n\r':
            j += 1
        toks.append(data[i:j]); i = j
    assert toks[0] == b'P5' and toks[3] == b'255'
    pw, ph = int(toks[1]), int(toks[2])
    return pw, ph, data[i + 1:]   # 头后恰好一个空白字节

scene, pgm = sys.argv[1], sys.argv[2]
w, h = int(sys.argv[3]), int(sys.argv[4])
scale, target = float(sys.argv[5]), float(sys.argv[6])

polys0 = load(scene)
total_area = sum(area(q) for q in polys0)
deg = [i for i, q in enumerate(polys0) if area(q) <= 1e-12]
polys = [[(x * scale, y * scale) for x, y in q] for q in polys0]
win = bbox(polys0)

pw, ph, raw = read_pgm(pgm)
assert (pw, ph) == (w, h) and len(raw) == w * h

on = sample(polys, win, w, h, 0, 0, w, h)
mismatch = sum(1 for y in range(h) for x in range(w)
               if bool(raw[y * w + x]) != ((x, y) in on))
density = len(on) / total_area

# 独立反解：Python 自行计算缩放上界并二分，与 C 结果互证
bx0, by0, bx1, by1 = bbox(polys0)
dxw = (win[2] - win[0]) / w
dyw = (win[3] - win[1]) / h
cand = []
if bx0 < 0: cand.append((win[0] + dxw) / bx0)
if bx1 > 0: cand.append((win[2] - dxw) / bx1)
if by0 < 0: cand.append((win[1] + dyw) / by0)
if by1 > 0: cand.append((win[3] - dyw) / by1)
smax = min(cand)

def dens_at(sc):
    q = [[(x * sc, y * sc) for x, y in p] for p in polys0]
    return len(sample(q, win, w, h, 0, 0, w, h)) / total_area

lo, hi = 0.0, smax
for _ in range(40):
    mid = (lo + hi) * 0.5
    if dens_at(mid) >= target:
        hi = mid
    else:
        lo = mid

# 扩大网格独立统计边缘丢失
gx0 = math.floor((bx0 * scale - win[0]) / dxw); gx1 = math.ceil((bx1 * scale - win[0]) / dxw)
gy0 = math.floor((by0 * scale - win[1]) / dyw); gy1 = math.ceil((by1 * scale - win[1]) / dyw)
full = sample(polys, win, w, h, gx0, gy0, gx1, gy1)
lost = sum(1 for (x, y) in full if x < 0 or x >= w or y < 0 or y >= h)

rel = abs(scale - hi) / max(hi, 1e-300)
print(f"area={total_area:.6f} degenerate_polys={deg}")
print(f"pgm_mismatch={mismatch} independent edge_lost={lost} on={len(on)}")
print(f"density={density:.6f} target={target:.6f}")
print(f"C scale={scale:.17g} py scale={hi:.17g} rel_diff={rel:.3e} py_smax={smax:.17g}")
ok = (mismatch == 0 and lost == 0 and density >= target
      and rel < 1e-9 and dens_at(lo) < target)
print("VERIFY", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
