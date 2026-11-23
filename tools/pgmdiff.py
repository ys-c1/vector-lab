#!/usr/bin/env python3
"""逐像素比较两个 PGM（P5, 8-bit）。输出一行：pixels <n> bytes <m>。
文件完全相同时输出：identical。"""
import sys


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P5"):
        raise SystemExit("not a P5 pgm: " + path)
    parts = data.split(b"\n", 3)
    if len(parts) < 4:
        raise SystemExit("truncated pgm: " + path)
    w, h = (int(v) for v in parts[1].split())
    px = parts[3]
    if len(px) != w * h:
        raise SystemExit("bad pixel payload in %s: got %d, want %d" % (path, len(px), w * h))
    return w, h, px


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: pgmdiff.py a.pgm b.pgm")
    wa, ha, pa = load(sys.argv[1])
    wb, hb, pb = load(sys.argv[2])
    if (wa, ha) != (wb, hb):
        print("size %dx%d vs %dx%d" % (wa, ha, wb, hb))
        return 1
    if pa == pb:
        print("identical")
        return 0
    pixel_diff = sum(1 for a, b in zip(pa, pb) if a != b)
    byte_diff = sum(1 for a, b in zip(pa, pb) if a != b)
    first = next(i for i, (a, b) in enumerate(zip(pa, pb)) if a != b)
    print("pixels %d bytes %d first_offset %d" % (pixel_diff, byte_diff, first))
    return 1


if __name__ == "__main__":
    sys.exit(main())
