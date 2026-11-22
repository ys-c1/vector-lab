#define _POSIX_C_SOURCE 199309L
#include "spick.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * 屏幕空间容差拾取基准：两档缩放比（0.5x / 2.0x），各配同一旋转角 0.6 rad。
 *
 * 场景（形状空间）：200 段短线段，第 i 段水平放在离点击点 back=(0,0) 的
 * 半径 r_i = 0.4 + i*0.06 的圆环上，段长固定；点击点为形状原点经 M 映射
 * 后的屏幕点。于是：
 *   - 雅可比路径的形状容差 = tol_px / k：k=0.5 得 6，k=2 得 1.5，
 *     命中段数随缩放变化，但屏幕上的有效容差恒为 tol_px 像素；
 *   - 旧路径形状容差恒为 tol_px，屏幕上有效容差 = k*tol_px，随缩放漂移。
 *
 * 逐像素点击参考（demo 内独立实现）：枚举点击点 tol_px 圆内每个整数像素
 * 中心 p，用 M^{-1} 映回形状空间 back_p，对每段算形状最近点并映回屏幕，
 * 若 p 到段屏幕像的距离 <= sqrt(2)/2（像素中心到像素角）则点亮该段，任一
 * 像素点亮即参考命中。参考不使用雅可比/奇异值。
 *
 * 输出分三栏：雅可比（sigma_max/sigma_min/kappa/det）、容差（tol_px/
 * tol_shape/屏幕有效容差）、耗时（雅可比/旧路径/逐像素参考）；另附命中数
 * 与逐项比对的误差上界。 */
#define NSEGS 200
#define NPIX_MAX 256
#define REPS 300

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static SpForm sim(double k, double th, double tx, double ty) {
    SpForm m;
    m.a = k * cos(th);  m.b = k * sin(th);
    m.c = -k * sin(th); m.d = k * cos(th);
    m.e = tx;           m.f = ty;
    return m;
}

static double dist_pt_seg(SpPoint p, SpPoint a, SpPoint b, double *u_out) {
    double sx = b.x - a.x, sy = b.y - a.y;
    double len2 = sx * sx + sy * sy;
    double u = 0.0;
    if (len2 > 1e-12) {
        u = ((p.x - a.x) * sx + (p.y - a.y) * sy) / len2;
        if (u < 0.0) u = 0.0;
        if (u > 1.0) u = 1.0;
    }
    double qx = a.x + u * sx, qy = a.y + u * sy;
    double dx = p.x - qx, dy = p.y - qy;
    if (u_out) *u_out = u;
    return sqrt(dx * dx + dy * dy);
}

/* 逐像素点击参考（独立实现，不复用库内函数） */
static int pixel_reference(const SpSeg *segs, int n, const SpForm *m,
                           SpPoint scr, double tol_px, unsigned char *hit) {
    const double hd = 0.70710678118654752440;
    double det = m->a * m->d - m->b * m->c;
    int r = (int)ceil(tol_px);
    memset(hit, 0, (size_t)n);
    int count = 0;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            double px = floor(scr.x) + (double)dx;
            double py = floor(scr.y) + (double)dy;
            double ex = px - scr.x, ey = py - scr.y;
            if (ex * ex + ey * ey > tol_px * tol_px) continue;
            /* 像素中心 -> 形状空间（显式逆矩阵） */
            double qx = px - m->e, qy = py - m->f;
            SpPoint back = {( m->d * qx - m->c * qy) / det,
                            (-m->b * qx + m->a * qy) / det};
            for (int i = 0; i < n; ++i) {
                if (hit[i]) continue;
                double u = 0.0;
                dist_pt_seg(back, segs[i].p0, segs[i].p1, &u);
                SpPoint q = {segs[i].p0.x + u * (segs[i].p1.x - segs[i].p0.x),
                             segs[i].p0.y + u * (segs[i].p1.y - segs[i].p0.y)};
                SpPoint qs = sp_map(m, q);
                double rx = qs.x - px, ry = qs.y - py;
                if (sqrt(rx * rx + ry * ry) <= hd) {
                    hit[i] = 1;
                    ++count;
                }
            }
        }
    }
    return count;
}

/* 现状（旧路径）的朴素参考：形状空间标量容差 */
static int legacy_reference(const SpSeg *segs, int n, const SpForm *m,
                            SpPoint scr, double tol, unsigned char *hit) {
    double det = m->a * m->d - m->b * m->c;
    double qx = scr.x - m->e, qy = scr.y - m->f;
    SpPoint back = {( m->d * qx - m->c * qy) / det,
                    (-m->b * qx + m->a * qy) / det};
    int count = 0;
    for (int i = 0; i < n; ++i) {
        double d = dist_pt_seg(back, segs[i].p0, segs[i].p1, NULL);
        hit[i] = (d <= tol);
        if (hit[i]) ++count;
    }
    return count;
}

int main(void) {
    SpSeg segs[NSEGS];
    for (int i = 0; i < NSEGS; ++i) {
        double r = 0.4 + 0.06 * (double)i;
        /* 圆环上水平短段，离原点最近距离约 r */
        segs[i].p0 = (SpPoint){r, -1.2};
        segs[i].p1 = (SpPoint){r, 1.2};
    }
    SpPoint back = {0.0, 0.0};
    const double tol_px = 3.0;

    typedef struct { double k; double th; const char *label; } Tier;
    Tier tiers[2] = {
        {0.5, 0.6, "0.5x"},
        {2.0, 0.6, "2.0x"},
    };

    printf("场景：%d 段，点击点=形状原点的屏幕像，tol_px=%.1f，旋转 0.6 rad\n",
           NSEGS, tol_px);

    /* ---- 表 1：雅可比与容差 ---- */
    printf("\n[雅可比与容差]\n");
    printf("%-8s | %10s | %10s | %9s | %9s | %8s | %10s | %12s\n",
           "缩放", "sigma_max", "sigma_min", "kappa", "det", "tol_px",
           "tol_shape", "屏幕有效容差");
    printf("---------|------------|------------|-----------|-----------|"
           "----------|------------|--------------\n");
    for (int t = 0; t < 2; ++t) {
        SpForm m = sim(tiers[t].k, tiers[t].th, 200.0, 100.0);
        SpJac j;
        char ef[SP_FIELD_MAX];
        if (sp_jacobian(&m, &j, ef) != SP_OK) {
            fprintf(stderr, "jac failed: %s\n", ef);
            return 2;
        }
        double tol_shape = tol_px * j.sigma_max;
        printf("%-8s | %10.6f | %10.6f | %9.4f | %9.4f | %8.1f | %10.6f | "
               "%12.6f\n",
               tiers[t].label, j.sigma_max, j.sigma_min, j.kappa, j.det,
               tol_px, tol_shape, tol_shape / j.sigma_max);
    }

    /* ---- 表 2：命中数与耗时 ---- */
    printf("\n[命中与耗时]\n");
    printf("%-8s | %12s | %10s | %12s | %8s | %9s | %12s | %11s | %13s\n",
           "缩放", "命中(雅可比)", "命中(旧)", "命中(参考)",
           "不一致", "误差上界", "耗时(雅可比)", "耗时(旧)", "耗时(参考)");
    printf("---------|--------------|------------|--------------|----------|"
           "-----------|--------------|-------------|---------------\n");

    int bad = 0;
    for (int t = 0; t < 2; ++t) {
        SpForm m = sim(tiers[t].k, tiers[t].th, 200.0, 100.0);
        SpPoint scr = sp_map(&m, back);
        SpPick pj, pl, again;
        char ef[SP_FIELD_MAX];
        unsigned char refhit[NSEGS], leghit[NSEGS];
        int nj = 0, nl = 0, nr = 0, nleg_ref = 0;
        double ms_jac = 0.0, ms_leg = 0.0, ms_ref = 0.0;

        for (int rep = 0; rep < REPS; ++rep) {
            double t0 = now_ms();
            if (sp_pick(segs, NSEGS, &m, scr, tol_px, SP_F_SCREEN_TOL,
                        &pj, ef) != SP_OK) {
                fprintf(stderr, "pick failed: %s\n", ef);
                return 2;
            }
            double t1 = now_ms();
            if (sp_pick(segs, NSEGS, &m, scr, tol_px, 0, &pl, ef) != SP_OK)
                return 2;
            double t2 = now_ms();
            nr = pixel_reference(segs, NSEGS, &m, scr, tol_px, refhit);
            double t3 = now_ms();
            if (rep == 0) {
                nj = pj.count;
                nl = pl.count;
            }
            ms_jac += t1 - t0;
            ms_leg += t2 - t1;
            ms_ref += t3 - t2;
        }
        ms_jac /= REPS; ms_leg /= REPS; ms_ref /= REPS;
        nleg_ref = legacy_reference(segs, NSEGS, &m, scr, tol_px, leghit);

        /* 逐项比对：雅可比路径 vs 逐像素参考 */
        int mism = 0;
        int ref_only = 0, new_only = 0;
        for (int i = 0; i < NSEGS; ++i) {
            int r = refhit[i] ? 1 : 0;
            if (pj.rows[i].hit != r) {
                ++mism;
                if (r && !pj.rows[i].hit) ++ref_only;
                if (!r && pj.rows[i].hit) ++new_only;
            }
        }
        if (mism > pj.band_n) {
            printf("  上界失效：不一致 %d > band_n %d\n", mism, pj.band_n);
            bad = 1;
        }

        /* 不启用时命中集合必须与现状完全一致（逐项，含距离逐位比较） */
        int diff = 0;
        if (sp_pick(segs, NSEGS, &m, scr, tol_px, 0, &again, ef) != SP_OK)
            return 2;
        for (int i = 0; i < NSEGS; ++i) {
            if (pl.rows[i].hit != (leghit[i] ? 1 : 0)) ++diff;
            if (again.rows[i].hit != pl.rows[i].hit) ++diff;
            if (again.rows[i].dist != pl.rows[i].dist) ++diff;
        }
        if (diff) { printf("  旧路径与现状不一致：%d 项\n", diff); bad = 1; }

        printf("%-8s | %12d | %10d | %12d | %8d | %9d | %10.4f ms | %8.4f ms | "
               "%9.4f ms\n",
               tiers[t].label, nj, nl, nr, mism, pj.band_n,
               ms_jac, ms_leg, ms_ref);
        printf("  不一致细分：仅参考命中 %d，仅新路径命中 %d；"
               "旧路径(现状)命中 %d；本次不一致率 %.2f%%，误差上界 %.2f%%\n",
               ref_only, new_only, nleg_ref,
               100.0 * mism / NSEGS, 100.0 * pj.band_n / NSEGS);

        /* CSV 导出（雅可比路径逐段） */
        char name[32];
        snprintf(name, sizeof name, "hits_%s.csv",
                 tiers[t].k < 1.0 ? "k0.5" : "k2.0");
        FILE *fp = fopen(name, "w");
        if (!fp || sp_export_rows(&pj, fp, 0) != SP_OK) {
            bad = 1;
            if (fp) fclose(fp);
        } else {
            fclose(fp);
        }
    }

    /* ---- 奇异/退化报错 ---- */
    printf("\n[奇异与退化]\n");
    SpSeg one[1] = {{{0, 0}, {1, 0}}};
    SpForm sing = {1, 2, 2, 4, 0, 0};
    SpPick ps;
    char ef[SP_FIELD_MAX] = "";
    SpPoint scr0 = {0, 0};
    int rc = sp_pick(one, 1, &sing, scr0, tol_px, SP_F_SCREEN_TOL, &ps, ef);
    printf("  det=0      -> rc=%d (%s) field=%s\n", rc, sp_strerror(rc), ef);
    if (rc != SP_E_SINGULAR || strcmp(ef, "det") != 0) bad = 1;
    SpForm nanm = {1, 0, 0, NAN, 0, 0};
    rc = sp_pick(one, 1, &nanm, scr0, tol_px, SP_F_SCREEN_TOL, &ps, ef);
    printf("  d=NaN      -> rc=%d (%s) field=%s\n", rc, sp_strerror(rc), ef);
    if (rc != SP_E_SINGULAR || strcmp(ef, "d") != 0) bad = 1;
    SpForm infm = {1, 0, 0, 1, 0, INFINITY};
    rc = sp_pick(one, 1, &infm, scr0, tol_px, SP_F_SCREEN_TOL, &ps, ef);
    printf("  f=inf      -> rc=%d (%s) field=%s\n", rc, sp_strerror(rc), ef);
    if (rc != SP_E_SINGULAR || strcmp(ef, "f") != 0) bad = 1;
    rc = sp_pick(one, 1, &sing, scr0, -1.0, SP_F_SCREEN_TOL, &ps, ef);
    printf("  tol=-1     -> rc=%d (%s) field=%s\n", rc, sp_strerror(rc), ef);
    if (rc != SP_E_TOL || strcmp(ef, "tol") != 0) bad = 1;

    return bad ? 1 : 0;
}
