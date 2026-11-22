/* 窗口归一化验证驱动。
 *
 * 用法：
 *   run report <windows.txt> [tsv]
 *       逐窗口报告：子环数、绕序符号（拆分后各子环原符号）、被翻转的
 *       子环数、面积绝对值合计、归一化前后面积绝对值的最大偏差（必须
 *       恰为 0）、归一化耗时。给 tsv 时同时写机器可读的一行一窗口。
 *
 *   run render <windows.txt> <w> <h> <ss> <legacy.pgm> <new.pgm>
 *       同一组窗口出两张图：mode=0 走"关掉新逻辑"的老路径（输入顶点
 *       原样 + 奇偶规则，逐字节复刻 render 模块的 raster_fill）；mode=1
 *       走归一化子环 + 非零环绕。报告逐像素差异。
 *
 * 退出码：0 全部通过；1 有不满足项；2 输入被拒。
 */
#define _POSIX_C_SOURCE 199309L
#include "winnorm.h"
#include "winsplit.h"
#include "winsolve.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

static int sign_char(double v) {
    return v > 0.0 ? '+' : (v < 0.0 ? '-' : '0');
}

/* 独立复算：用 long double 鞋带公式从归一化后的顶点重算 |面积|，与模块
 * 存的 abs_area 逐项比较。这是外部核对，不读任何"翻前"元数据。 */
static int external_area_ok(const WNorm *nrm, double *out_dev) {
    double dev = 0.0;
    for (int k = 0; k < nrm->rings; ++k) {
        long double s = 0.0L;
        const WRing *r = &nrm->ring[k];
        for (int i = 0; i < r->n; ++i) {
            const WPoint a = r->pts[i], b = r->pts[(i + 1) % r->n];
            s += (long double)a.x * (long double)b.y -
                 (long double)b.x * (long double)a.y;
        }
        if (s < 0.0L) s = -s;
        long double d = 0.5L * s - (long double)nrm->abs_area[k];
        if (d < 0.0L) d = -d;
        double dd = (double)d;
        if (!(dd <= dev)) dev = dd;
    }
    if (out_dev) *out_dev = dev;
    return dev == 0.0;
}

/* 独立复刻的奇偶规则，用于"几何不变"核对。 */
static int even_odd(const WPoint *p, int n, double x, double y) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        const WPoint a = p[i], b = p[(i + 1) % n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

/* 点到线段距离（用于跳过落在边上的采样点，那里奇偶与环绕的半开约定
 * 本来就可能判得不一样，属于边界约定而非几何差异）。 */
static double seg_dist(double px, double py, WPoint a, WPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - a.x) * dx + (py - a.y) * dy) / len2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double cx = a.x + dx * t, cy = a.y + dy * t;
    double ex = px - cx, ey = py - cy;
    return sqrt(ex * ex + ey * ey);
}

/* 几何不变：在包围盒里撒 61x61 个采样点，比较"输入环的奇偶覆盖"与
 * "归一化后全部子环（合并所有边）的奇偶覆盖"。拆分只在真穿过处加顶点，
 * 边集合不变；逐环反转方向也不改变该环的奇偶覆盖。所以两者必须逐点
 * 相同——这是与填充规则无关的硬不变量。
 * 落在任何边 1e-6 以内的采样点跳过（奇偶与环绕对"正好在边上"的半开
 * 约定不同，那是边界约定而非几何差异）。 */
static long geom_mismatch(const WPoint *in, int in_n, const WNorm *nrm) {
    double minx = in[0].x, maxx = in[0].x, miny = in[0].y, maxy = in[0].y;
    for (int i = 1; i < in_n; ++i) {
        if (in[i].x < minx) minx = in[i].x;
        if (in[i].x > maxx) maxx = in[i].x;
        if (in[i].y < miny) miny = in[i].y;
        if (in[i].y > maxy) maxy = in[i].y;
    }
    if (!(maxx > minx) || !(maxy > miny)) return 0;
    long bad = 0;
    const int N = 61;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            double x = minx + (i + 0.5) / N * (maxx - minx);
            double y = miny + (j + 0.5) / N * (maxy - miny);
            int near = 0;
            for (int s = 0; s < in_n && !near; ++s)
                if (seg_dist(x, y, in[s], in[(s + 1) % in_n]) < 1e-6) near = 1;
            for (int k = 0; k < nrm->rings && !near; ++k)
                for (int s = 0; s < nrm->ring[k].n && !near; ++s)
                    if (seg_dist(x, y, nrm->ring[k].pts[s],
                                 nrm->ring[k].pts[(s + 1) % nrm->ring[k].n]) < 1e-6)
                        near = 1;
            if (near) continue;
            int a = even_odd(in, in_n, x, y);
            int c = 0;
            for (int k = 0; k < nrm->rings; ++k)
                for (int s = 0; s < nrm->ring[k].n; ++s) {
                    WPoint p0 = nrm->ring[k].pts[s];
                    WPoint p1 = nrm->ring[k].pts[(s + 1) % nrm->ring[k].n];
                    if ((p0.y > y) == (p1.y > y)) continue;
                    double t = (y - p0.y) / (p1.y - p0.y);
                    if (p0.x + (p1.x - p0.x) * t > x) ++c;
                }
            if (a != (c & 1)) ++bad;
        }
    return bad;
}

static int cmd_report(const char *path, const char *tsv_path) {
    WFileSet raw;
    char err[1024];
    if (wsplit_load(path, &raw, err, sizeof err) != 0) {
        fprintf(stderr, "拒绝：%s\n", err);
        return 2;
    }
    WSet s;
    ws_build(&raw, &s);

    FILE *tsv = NULL;
    if (tsv_path) {
        tsv = fopen(tsv_path, "w");
        if (!tsv) { fprintf(stderr, "无法写 %s\n", tsv_path); return 2; }
        fprintf(tsv, "window_id\tin_verts\trings\twinding_signs\tflipped\t"
                     "abs_total\tdev_before_after\tdev_external\tms\trc\t"
                     "geom_mismatch\tat\tat2\n");
    }

    printf("窗口  输入点  子环  绕序符号            翻转  面积绝对值合计  前后偏差  耗时\n");
    int bad = 0;
    double total_ms = 0.0, worst_dev = 0.0, worst_ext = 0.0;
    for (int k = 0; k < s.count; ++k) {
        WItem *w = &s.win[k];
        double t0 = now_ms();
        WNorm nrm;
        int at = -1, at2 = -1;
        int rc = winnorm_normalize(w->in, w->in_n, w->nsplit > 0 ? 1 : 0,
                                   &nrm, &at, &at2);
        double ms = now_ms() - t0;

        if (rc != WN_OK) {
            printf("W%-4d %-7d 拒绝 code=%d %s", w->id, w->in_n, rc,
                   winnorm_strerror(rc));
            if (at >= 0) printf(" 序号 %d", at);
            if (at2 >= 0) printf("/%d", at2);
            printf("\n");
            if (tsv)
                fprintf(tsv, "%d\t%d\t-\t-\t-\t-\t-\t-\t%.3f\t%d\t-\t%d\t%d\n",
                        w->id, w->in_n, ms, rc, at, at2);
            ++bad;
            continue;
        }
        total_ms += ms;

        /* 面积绝对值"前后偏差"：逐位比较。拆分后各子环 |原符号面积| 之和
         * 与归一化后 |面积| 之和必须完全相同（memcmp，不是容差比较）。 */
        double after = 0.0, before = 0.0;
        int nflip = 0;
        char signs[256];
        int off = 0;
        signs[0] = '\0';
        for (int r = 0; r < nrm.rings; ++r) {
            after += nrm.abs_area[r];
            before += fabs(nrm.in_signed_area[r]);
            nflip += nrm.flipped[r];
            off += snprintf(signs + off, sizeof signs - (size_t)off, "%s%c",
                            r ? " " : "", sign_char(nrm.in_signed_area[r]));
        }
        int dev_zero = memcmp(&after, &before, sizeof after) == 0;
        if (!dev_zero) { ++bad; printf("  违反：面积绝对值合计在翻转前后不再逐位相同\n"); }
        double dev = after - before;
        if (dev < 0.0) dev = -dev;
        if (dev > worst_dev) worst_dev = dev;

        /* 外部核对：从归一化后的顶点用 long double 重算 |面积|，与模块结果比较。 */
        double ext = 0.0;
        if (!external_area_ok(&nrm, &ext)) {
            ++bad;
            printf("  违反：外部复算的 |面积| 与模块结果不一致（最大差 %.3g）\n", ext);
        }
        if (ext > worst_ext) worst_ext = ext;

        /* 统一绕序后的硬性要求：每个子环有符号面积 > 0 */
        for (int r = 0; r < nrm.rings; ++r)
            if (winnorm_signed_area(nrm.ring[r].pts, nrm.ring[r].n) <= 0.0) {
                printf("  违反：子环 %d 归一化后符号面积不为正\n", r);
                ++bad;
            }
        /* 逐位不变量：翻转前后每个子环面积绝对值必须完全相同 */
        for (int r = 0; r < nrm.rings; ++r) {
            double raw = fabs(nrm.in_signed_area[r]);
            if (memcmp(&raw, &nrm.abs_area[r], sizeof raw) != 0) {
                printf("  违反：子环 %d 面积绝对值在翻转前后变了\n", r);
                ++bad;
            }
        }

        /* 几何不变：原输入的奇偶覆盖 vs 归一化子环的环绕覆盖 */
        long miss = geom_mismatch(w->in, w->in_n, &nrm);
        if (miss != 0) {
            printf("  违反：归一化后几何覆盖与原窗口不一致（%ld 个采样点）\n", miss);
            ++bad;
        }

        printf("W%-4d %-7d %-5d %-19s %-5d %-15.6f %-9.3g %.3f ms\n",
               w->id, w->in_n, nrm.rings, signs, nflip, after, dev, ms);

        if (tsv)
            fprintf(tsv, "%d\t%d\t%d\t%s\t%d\t%.6f\t%.3g\t%.3g\t%.3f\t0\t%ld\t%d\t%d\n",
                    w->id, w->in_n, nrm.rings, signs, nflip, after, dev, ext, ms,
                    miss, at, at2);
    }
    if (tsv) fclose(tsv);
    printf("合计：%d 个窗口，最坏前后偏差 %.3g（必须为 0），外部核对最大差 %.3g，"
           "归一化总耗时 %.3f ms\n",
           s.count, worst_dev, worst_ext, total_ms);
    return bad ? 1 : 0;
}

static int cmd_render(const char *path, int w, int h, int ss,
                      const char *legacy, const char *newp) {
    WFileSet raw;
    char err[1024];
    if (wsplit_load(path, &raw, err, sizeof err) != 0) {
        fprintf(stderr, "拒绝：%s\n", err);
        return 2;
    }
    WSet s;
    ws_build(&raw, &s);
    for (int k = 0; k < s.count; ++k)
        if (s.win[k].rc != WN_OK) {
            fprintf(stderr, "拒绝：窗口 %d 归一化失败 code=%d %s\n",
                    s.win[k].id, s.win[k].rc, winnorm_strerror(s.win[k].rc));
            return 2;
        }
    WBox b;
    if (ws_bbox(&s, &b) != 0) return 3;
    unsigned char *a = (unsigned char *)malloc((size_t)w * h);
    unsigned char *n = (unsigned char *)malloc((size_t)w * h);
    if (!a || !n) { free(a); free(n); return 4; }
    int rc = ws_render(&s, b, w, h, ss, 0, a);
    if (rc == 0) rc = ws_render(&s, b, w, h, ss, 1, n);
    if (rc == 0) rc = ws_save_pgm(legacy, a, w, h);
    if (rc == 0) rc = ws_save_pgm(newp, n, w, h);
    if (rc != 0) { free(a); free(n); return 5; }
    long d = ws_diff(a, n, w, h);
    long on_a = 0, on_n = 0;
    for (long i = 0; i < (long)w * h; ++i) {
        if (a[i]) ++on_a;
        if (n[i]) ++on_n;
    }
    printf("画布 %dx%d ss=%d 包围盒 [%.3f %.3f %.3f %.3f]\n", w, h, ss,
           b.minx, b.miny, b.maxx, b.maxy);
    printf("关掉新逻辑像素 %ld，新路径像素 %ld，逐像素差异 %ld\n", on_a, on_n, d);
    free(a);
    free(n);
    return d == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "用法: %s report <windows.txt> [tsv]\n"
                "      %s render <windows.txt> <w> <h> <ss> <legacy.pgm> <new.pgm>\n",
                argv[0], argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "report"))
        return cmd_report(argv[2], argc > 3 ? argv[3] : NULL);
    if (!strcmp(argv[1], "render") && argc >= 8)
        return cmd_render(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                          argv[6], argv[7]);
    fprintf(stderr, "未知子命令或参数不足\n");
    return 2;
}
