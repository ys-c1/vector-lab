#define _POSIX_C_SOURCE 199309L
#include "stroke.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

/* 以直角阶梯形构造 n_corner 个拐点的折线，n = n_corner + 2 个顶点。
 * 水平段 10、竖直段 4；拐点都是 90° 直角，便于手算交点。 */
static int make_stair(int n_corner, SPoint *pts) {
    int n = n_corner + 2;
    pts[0] = (SPoint){0.0, 0.0};
    double x = 0.0, y = 0.0;
    int horiz = 1, dir = 1;
    for (int s = 0; s < n_corner + 1; ++s) {
        if (horiz) x += dir * 10.0;
        else { y += 4.0; dir = -dir; }
        horiz = !horiz;
        pts[s + 1] = (SPoint){x, y};
    }
    return n;
}

static double polygon_signed_area(const SPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        s += p[i].x * p[j].y - p[j].x * p[i].y;
    }
    return s * 0.5;
}

/* 标准绕数公式，独立于裁环实现。 */
static int winding_at(const SPoint *poly, int n, double x, double y) {
    int wn = 0;
    for (int k = 0; k < n; ++k) {
        SPoint a = poly[k], b = poly[(k + 1) % n];
        double cr = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
        if (a.y <= y) {
            if (b.y > y && cr > 0.0) ++wn;
        } else {
            if (b.y <= y && cr < 0.0) --wn;
        }
    }
    return wn;
}

typedef struct { double y; } Crossing;

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* 在竖线 x 上，与主方向同号的区域总高度。
 * 该高度在一段拓扑不变的竖条内是 x 的线性函数。 */
static double inside_height(const SPoint *poly, int n, double x, int sign) {
    double ys[STROKE_MAX_RAW];
    int k = 0;
    for (int i = 0; i < n; ++i) {
        SPoint a = poly[i], b = poly[(i + 1) % n];
        double lo = a.x < b.x ? a.x : b.x, hi = a.x < b.x ? b.x : a.x;
        if (x <= lo || x >= hi) continue;             /* 严格内部，排除竖直边 */
        double t = (x - a.x) / (b.x - a.x);
        ys[k++] = a.y + (b.y - a.y) * t;
    }
    qsort(ys, (size_t)k, sizeof(double), cmp_double);
    double h = 0.0;
    for (int i = 0; i + 1 < k; ++i) {
        if (ys[i + 1] - ys[i] < 1e-12) continue;
        double ym = 0.5 * (ys[i] + ys[i + 1]);
        if (winding_at(poly, n, x, ym) * sign > 0) h += ys[i + 1] - ys[i];
    }
    return h;
}

/* 扫描线精确面积：以所有顶点 x 与交点 x 为分界，逐竖条积分。
 * 竖条内没有拓扑变化，高度随 x 线性，故用两个内点做梯形即精确。 */
static double scanline_area(const SPoint *poly, int n) {
    double sign = polygon_signed_area(poly, n) > 0.0 ? 1.0 : -1.0;
    double xs[STROKE_MAX_RAW * STROKE_MAX_RAW];
    int nx = 0;
    for (int i = 0; i < n; ++i) xs[nx++] = poly[i].x;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            SPoint a0 = poly[i], a1 = poly[(i + 1) % n];
            SPoint b0 = poly[j], b1 = poly[(j + 1) % n];
            double rx = a1.x - a0.x, ry = a1.y - a0.y;
            double sx = b1.x - b0.x, sy = b1.y - b0.y;
            double den = rx * sy - ry * sx;
            if (fabs(den) < 1e-12) continue;
            double qx = b0.x - a0.x, qy = b0.y - a0.y;
            double t = (qx * sy - qy * sx) / den;
            double u = (qx * ry - qy * rx) / den;
            if (t > 1e-9 && t < 1.0 - 1e-9 && u > 1e-9 && u < 1.0 - 1e-9)
                xs[nx++] = a0.x + rx * t;
        }
    }
    qsort(xs, (size_t)nx, sizeof(double), cmp_double);
    double area = 0.0;
    for (int i = 0; i + 1 < nx; ++i) {
        double x0 = xs[i], x1 = xs[i + 1];
        if (x1 - x0 < 1e-12) continue;
        double w = x1 - x0;
        double ha = inside_height(poly, n, x0 + 0.25 * w, sign);
        double hb = inside_height(poly, n, x0 + 0.75 * w, sign);
        area += 0.5 * (ha + hb) * w;
    }
    return area;
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    int groups[2] = { 4, 32 };
    /* 宽度 1：两组都只有 proper 交点，保留环两两不重叠，
     * 保留面积与扫描线参考可精确核对（误差仅为浮点舍入）。 */
    double width = 1.0;
    int rep = 5000;
    const char *csv = argc > 1 ? argv[1] : "build/loops.csv";
    const char *hit_csv = argc > 2 ? argv[2] : "build/hits.csv";
    const char *loop_csv = argc > 3 ? argv[3] : "build/cut_loops.csv";
    FILE *fh = fopen(hit_csv, "w");
    FILE *fl = fopen(loop_csv, "w");
    if (!fh || !fl) { perror("csv"); return 1; }
    static SPoint pts[300];

    FILE *f = fopen(csv, "w");
    if (!f) { perror(csv); return 1; }
    fprintf(f, "corners,raw_points,hit_pairs,kept_rings,cut_loops,raw_area,kept_area,"
               "removed_area,closure_error,ref_area,area_error,time_us\n");

    printf("corners raw_pts hits kept cut  raw_area     kept_area    removed_area "
           "closure    ref_area     area_err     time_us\n");

    for (int g = 0; g < 2; ++g) {
        int n = make_stair(groups[g], pts);
        SStrokeResult r;
        int rc = stroke_resolve(pts, n, width, 1, &r);
        if (rc != STROKE_OK) {
            printf("corners %d: rc=%d (%s) pair=%d\n",
                   groups[g], rc, stroke_strerror(rc), r.bad_pair);
            continue;
        }

        /* 裁环结果（用于统计），再单独计时。 */
        double t0 = now_ms();
        for (int k = 0; k < rep; ++k) stroke_resolve(pts, n, width, 1, &r);
        double t1 = now_ms();
        double us = (t1 - t0) * 1000.0 / rep;

        double ref = scanline_area(r.raw, r.raw_count);
        double area_err = fabs(r.kept_area - ref);

        printf("%7d %7d %4d %4d %3d  %10.5f  %10.5f  %11.5f  %8.1e  %10.5f  "
               "%11.3e  %8.2f\n",
               groups[g], r.raw_count, r.hit_count, r.kept_count, r.loop_count,
               r.raw_area, r.kept_area, r.removed_area, r.closure_error,
               ref, area_err, us);
        fprintf(f, "%d,%d,%d,%d,%d,%.12g,%.12g,%.12g,%.3e,%.12g,%.3e,%.3f\n",
                groups[g], r.raw_count, r.hit_count, r.kept_count, r.loop_count,
                r.raw_area, r.kept_area, r.removed_area, r.closure_error,
                ref, area_err, us);

        stroke_hits_csv(&r, fh);
        stroke_loops_csv(&r, fl);

        /* 逐环明细：编号、对号、所在原边、面积。 */
        for (int k = 0; k < r.loop_count; ++k) {
            SStrokeLoop *l = &r.loops[k];
            printf("   loop#%-2d pair=%-3d edges=%d-%d area=%11.6f at=(%.4f,%.4f) "
                   "hits=", l->index, l->pair, l->ea, l->eb, l->area,
                   l->at.x, l->at.y);
            for (int q = 0; q < l->hit_pair_count; ++q)
                printf("%s%d", q ? "," : "", l->hit_pairs[q]);
            printf("\n");
        }

        /* 每个交点对都必须能追溯到至少一个保留环或切除环。 */
        int traced = 0;
        for (int h = 0; h < r.hit_count; ++h) {
            int found = 0;
            for (int k = 0; k < r.loop_count && !found; ++k)
                for (int q = 0; q < r.loops[k].hit_pair_count; ++q)
                    if (r.loops[k].hit_pairs[q] == r.hits[h].pair) found = 1;
            for (int k = 0; k < r.kept_count && !found; ++k) {
                SStrokeRing *ring = &r.kept[k];
                for (int i = 0; i < ring->count; ++i)
                    if (r.edges[ring->off + i] == r.hits[h].ea ||
                        r.edges[ring->off + i] == r.hits[h].eb) { found = 1; break; }
            }
            if (found) ++traced;
        }
        printf("   hit pairs traced to a ring: %d/%d\n", traced, r.hit_count);

        int residual = 0;
        for (int k = 0; k < r.kept_count; ++k) {
            SPoint ring[STROKE_POOL_PTS];
            SStrokeRing *ri = &r.kept[k];
            for (int i = 0; i < ri->count; ++i) ring[i] = r.points[ri->off + i];
            SStrokeResult chk;
            if (stroke_resolve_polygon(ring, ri->count, 0, &chk) != STROKE_OK ||
                chk.hit_count != 0) ++residual;
        }
        printf("   kept rings with residual proper intersections: %d\n", residual);

        /* 诊断：保留环之间是否真的互相覆盖（bbox 相交后再采样判定）。
         * 重叠时 kept_area 是各环代数和，不等于真实带宽的并集面积。 */
        int real_overlap = 0;
        for (int a = 0; a < r.kept_count; ++a) {
            for (int b = a + 1; b < r.kept_count; ++b) {
                SStrokeRing *A = &r.kept[a], *B = &r.kept[b];
                double ax0 = 1e30, ax1 = -1e30, ay0 = 1e30, ay1 = -1e30;
                double bx0 = 1e30, bx1 = -1e30, by0 = 1e30, by1 = -1e30;
                for (int i = 0; i < A->count; ++i) {
                    double x = r.points[A->off + i].x, y = r.points[A->off + i].y;
                    if (x < ax0) ax0 = x;
                    if (x > ax1) ax1 = x;
                    if (y < ay0) ay0 = y;
                    if (y > ay1) ay1 = y;
                }
                for (int i = 0; i < B->count; ++i) {
                    double x = r.points[B->off + i].x, y = r.points[B->off + i].y;
                    if (x < bx0) bx0 = x;
                    if (x > bx1) bx1 = x;
                    if (y < by0) by0 = y;
                    if (y > by1) by1 = y;
                }
                double x0 = ax0 > bx0 ? ax0 : bx0, x1 = ax1 < bx1 ? ax1 : bx1;
                double y0 = ay0 > by0 ? ay0 : by0, y1 = ay1 < by1 ? ay1 : by1;
                if (!(x1 > x0 && y1 > y0)) continue;
                int shared = 0;
                for (int iy = 0; iy < 6 && !shared; ++iy) {
                    for (int ix = 0; ix < 6 && !shared; ++ix) {
                        double px = x0 + (ix + 0.5) * (x1 - x0) / 6.0;
                        double py = y0 + (iy + 0.5) * (y1 - y0) / 6.0;
                        if (winding_at(r.points + A->off, A->count, px, py) != 0 &&
                            winding_at(r.points + B->off, B->count, px, py) != 0)
                            shared = 1;
                    }
                }
                if (shared) ++real_overlap;
            }
        }
        printf("   kept-ring real overlaps (0 => kept_area == union area): %d\n",
               real_overlap);
    }

    fclose(f);
    fclose(fh);
    fclose(fl);
    printf("summary -> %s; hits -> %s; cut loops -> %s\n",
           csv, hit_csv, loop_csv);
    return 0;
}
