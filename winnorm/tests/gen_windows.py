#!/usr/bin/env python3
"""生成 winnorm 的窗口数据，纯 Python，不调用被测代码。

用法：
    python3 -I gen_windows.py [输出目录]      # 默认 ../data

文件格式（每行）：
    # 注释
    W <窗口号>                窗口头，编号唯一
    x y                       顶点
    S <窗口号> <边a> <边b>    拆分声明（可选）：在边 a 与边 b 的交点切开

生成的窗口：
    win_clean_ccw.txt   W1：单个逆时针矩形（+640，干净）
    win_clean_cw.txt    W2：单个顺时针矩形（-640，干净，需翻）
    win_bowtie0.txt     W3：蝴蝶结，未声明拆分（自交未拆分，应拒，边号 0/2）
    win_bowtie1.txt     W4：同一蝴蝶结，声明拆成 2 个子环（-192, +192）
    win_double0.txt     W5：双环 6 边形，未声明拆分（应拒，边号 0/4）
    win_double1.txt     W6：同一图形，声明拆成 3 个子环（+64, -128, +64）
    win_dup.txt         W7：带重复点（应拒，点号 1）
    win_contact.txt     W8：菱形一角顶点贴到对边（应拒，边号 0/3）
    wins_four.txt       W1/W2/W4/W6 合在一个文件里（四组窗口）
    bad_*.txt           装载器的其它拒绝用例
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def signed_area(pts):
    s = 0.0
    for i, (x, y) in enumerate(pts):
        x2, y2 = pts[(i + 1) % len(pts)]
        s += x * y2 - x2 * y
    return 0.5 * s


def cross3(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def proper(a, b, c, d):
    """两条线段是否在各自内部严格穿过（与 winnorm 的口径一致）。"""
    rx, ry = b[0] - a[0], b[1] - a[1]
    sx, sy = d[0] - c[0], d[1] - c[1]
    den = rx * sy - ry * sx
    if den == 0.0:
        return False
    qx, qy = c[0] - a[0], c[1] - a[1]
    t = (qx * sy - qy * sx) / den
    u = (qx * ry - qy * rx) / den
    return 0.0 < t < 1.0 and 0.0 < u < 1.0


def first_cross(pts):
    """字典序第一处真穿过（边号 a < b，均按输入环编号）。"""
    n = len(pts)
    for i in range(n):
        for j in range(i + 1, n):
            if j == i + 1 or (i == 0 and j == n - 1):
                continue
            if proper(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n]):
                return i, j
    return None


def cross_pt(a, b, c, d):
    rx, ry = b[0] - a[0], b[1] - a[1]
    sx, sy = d[0] - c[0], d[1] - c[1]
    den = rx * sy - ry * sx
    qx, qy = c[0] - a[0], c[1] - a[1]
    t = (qx * sy - qy * sx) / den
    return (a[0] + rx * t, a[1] + ry * t)


def split_all(pts):
    """递归拆分，与 winnorm 口径一致：每次找字典序第一个真穿过。"""
    cs = first_cross(pts)
    if cs is None:
        return [pts]
    i, j = cs
    n = len(pts)
    q = cross_pt(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n])
    A = [q]
    k = (i + 1) % n
    while True:
        A.append(pts[k])
        if k == j:
            break
        k = (k + 1) % n
    B = [q]
    k = (j + 1) % n
    while True:
        B.append(pts[k])
        if k == i:
            break
        k = (k + 1) % n
    return split_all(A) + split_all(B)


def poly_block(wid, pts):
    return "W %d\n" % wid + "".join("%.6f %.6f\n" % (x, y) for x, y in pts)


def split_line(wid, a, b):
    return "S %d %d %d\n" % (wid, a, b)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "data")
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)

    def write(name, text):
        with open(os.path.join(out, name), "w") as f:
            f.write(text)

    # 1) 干净逆时针矩形：整环 +640
    ccw = [(4.0, 4.0), (36.0, 4.0), (36.0, 24.0), (4.0, 24.0)]
    assert signed_area(ccw) == 640.0
    write("win_clean_ccw.txt", poly_block(1, ccw))

    # 2) 干净顺时针矩形（与 1 同尺寸，仅绕序相反）：整环 -640
    cw = [(44.0, 4.0), (44.0, 24.0), (76.0, 24.0), (76.0, 4.0)]
    assert signed_area(cw) == -640.0
    write("win_clean_cw.txt", poly_block(2, cw))

    # 3/4) 蝴蝶结：整环面积恰为 0（符号骗人），拆开是两个 192
    bow = [(84.0, 4.0), (116.0, 28.0), (116.0, 4.0), (84.0, 28.0)]
    assert signed_area(bow) == 0.0
    ca, cb = first_cross(bow)
    assert (ca, cb) == (0, 2)
    subs = split_all(bow)
    assert len(subs) == 2
    assert sorted(abs(signed_area(s)) for s in subs) == [192.0, 192.0]
    assert [signed_area(s) for s in subs] == [-192.0, 192.0]
    write("win_bowtie0.txt", poly_block(3, bow))
    write("win_bowtie1.txt", poly_block(4, bow) + split_line(4, 0, 2))

    # 5/6) 双环 6 边形：整环面积也是 0，拆开是 64 / 128 / 64
    dbl = [(124.0, 4.0), (140.0, 20.0), (156.0, 4.0), (156.0, 20.0),
           (140.0, 4.0), (124.0, 20.0)]
    assert signed_area(dbl) == 0.0
    assert first_cross(dbl) == (0, 4)
    subs = split_all(dbl)
    assert len(subs) == 3
    assert [signed_area(s) for s in subs] == [64.0, -128.0, 64.0]
    write("win_double0.txt", poly_block(5, dbl))
    write("win_double1.txt", poly_block(6, dbl) + split_line(6, 0, 4) +
          split_line(6, 1, 3))

    # 7) 重复点：相邻两点重合（点号 1）
    dup = [(4.0, 44.0), (36.0, 44.0), (36.0, 44.0), (36.0, 64.0), (4.0, 64.0)]
    write("win_dup.txt", poly_block(7, dup))

    # 8) 顶点贴边：菱形第 5 点落在边 0 上（非真穿过，拆分也消不掉）
    touch = [(4.0, 84.0), (36.0, 84.0), (36.0, 104.0), (4.0, 104.0),
             (20.0, 84.0)]
    write("win_contact.txt", poly_block(8, touch))

    # 四组窗口合在一个文件里：干净逆时针、干净顺时针、已拆分蝴蝶结、
    # 已拆分双环。各自坐标互不重叠。
    four = (poly_block(1, ccw) + poly_block(2, cw) +
            poly_block(4, bow) + split_line(4, 0, 2) +
            poly_block(6, dbl) + split_line(6, 0, 4) + split_line(6, 1, 3))
    write("wins_four.txt", four)

    # 装载器拒绝用例
    write("bad_few.txt", "W 1\n0 0\n10 0\n")
    write("bad_empty.txt", "")
    write("bad_dup_id.txt", poly_block(1, ccw) + poly_block(1, cw))
    write("bad_split_adjacent.txt", poly_block(1, ccw) + split_line(1, 0, 1))
    write("bad_split_outside.txt", poly_block(1, ccw) + split_line(1, 0, 2))
    write("bad_split_win.txt", poly_block(1, ccw) + split_line(9, 0, 2))
    write("bad_vertex_before.txt", "0 0\n" + poly_block(1, ccw))
    write("bad_vertex_extra.txt", "W 1\n0 0 4\n10 0\n10 10\n")

    print("wrote", out)
    for name in sorted(os.listdir(out)):
        print("  ", name)


if __name__ == "__main__":
    main()
