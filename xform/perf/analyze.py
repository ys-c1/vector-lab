#!/usr/bin/env python3
# 汇总三轮块宽扫描，给出每个场景的推荐块宽与完整曲线（Markdown 表）。
import statistics
import pathlib

SCEN = ["dense", "sparse", "nan", "long"]
HERE = pathlib.Path(__file__).resolve().parent


def read(path):
    rows = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if parts and parts[0].isdigit():
            rows.append((int(parts[0]), float(parts[2]), float(parts[3])))
    return rows


def main():
    summary = {}
    for s in SCEN:
        rounds = [read(HERE / f"final_{s}_r{r}.txt") for r in (1, 2, 3)]
        widths = [w for w, _, _ in rounds[0]]
        agg = []
        for j, w in enumerate(widths):
            ns = [r[j][1] for r in rounds]
            gbs = [r[j][2] for r in rounds]
            agg.append((w, statistics.median(ns), min(ns), max(ns),
                        statistics.median(gbs)))
        summary[s] = agg
        best = min(agg, key=lambda r: r[1])
        # 与最优值差距 <=2% 的最窄块宽：即“再窄就明显变慢”的拐点
        knee = min((r for r in agg if r[1] <= best[1] * 1.02),
                   key=lambda r: r[0])
        print(f"## {s}")
        print("| w(点) | 中位 ns/pt | 三轮区间 | 等效GB/s | 与最优差 |")
        print("|---|---:|---:|---:|---:|")
        for w, med, lo, hi, gb in agg:
            delta = (med / best[1] - 1.0) * 100.0
            mark = " **best**" if w == best[0] else ""
            print(f"| {w}{mark} | {med:.4f} | {lo:.4f}–{hi:.4f} | {gb:.1f} | +{delta:.1f}% |")
        print(f"\nbest={best[0]} ({best[1]:.4f} ns/pt), "
              f"knee(<=2%内最窄)={knee[0]} ({knee[1]:.4f} ns/pt)\n")
    return summary


if __name__ == "__main__":
    main()
