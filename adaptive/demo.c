/* 曲率自适应切段演示：
 *   - 两档总偏差上界（5e-4 = 万分之五，2e-5 = 十万分之二）
 *   - 每段按拉格朗日下界反解切刀数，逐段等分落盘
 *   - 报告总段数、每段实际偏差（解析 / 加密采样 / 输出折线实测）、耗时
 *   - 与“每段同样段数”的均匀基线对比，要求总段数至少少两成
 *   - 弯路段（圆弧）的偏差单独成表
 *   - 每段落盘后与 gold/ref points 的参照点逐点对齐（tools/cmp_points）
 */
#define _POSIX_C_SOURCE 199309L
#include "adaptive.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define W 100.0
#define H 70.0
#define REPS_PLAN 2000
#define REPS_FULL 20
#define REFINE 500          /* gold/ref 单文件上限 512 点，留余量 */
#define DENSE 2000          /* 输出折线实测用的加密样本 */
#define ALIGN_TOL 1e-3      /* 与 gold 参照点的逐点容差（累计） */

#define NPIECE 8

typedef struct {
    AdPiece piece[NPIECE];
    /* 直线段参数 */
    double x0[NPIECE], y0[NPIECE], ux[NPIECE], uy[NPIECE];
    /* 圆弧段参数 */
    double cx[NPIECE], cy[NPIECE], a0[NPIECE], R[NPIECE], dir[NPIECE];
} Shape;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void add_line(Shape *s, int i, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0;
    double len = sqrt(dx * dx + dy * dy);
    s->piece[i].kind = AD_PIECE_LINE;
    s->piece[i].len = len;
    s->piece[i].R = 0.0;
    s->x0[i] = x0; s->y0[i] = y0;
    s->ux[i] = dx / len; s->uy[i] = dy / len;
}

static void add_arc(Shape *s, int i, double cx, double cy, double R,
                    double a0, double sweep) {
    s->piece[i].kind = AD_PIECE_ARC;
    s->piece[i].len = R * fabs(sweep);
    s->piece[i].R = R;
    s->cx[i] = cx; s->cy[i] = cy;
    s->R[i] = R;
    s->a0[i] = a0;
    s->dir[i] = sweep >= 0.0 ? 1.0 : -1.0;
}

/* 逆时针圆角矩形，圆角半径依次 6 / 20 / 11 / 30（左下 / 右下 / 右上 / 左上）。
 * 直线与圆弧交替排列：0 直线、1 圆弧、2 直线……7 圆弧。 */
static void build_shape(Shape *s) {
    const double r0 = 6.0, r1 = 20.0, r2 = 11.0, r3 = 30.0;
    const double P = 3.14159265358979323846;
    add_line(s, 0, r0, 0.0, W - r1, 0.0);
    add_arc(s, 1, W - r1, r1, r1, -P / 2.0, P / 2.0);
    add_line(s, 2, W, r1, W, H - r2);
    add_arc(s, 3, W - r2, H - r2, r2, 0.0, P / 2.0);
    add_line(s, 4, W - r2, H, r3, H);
    add_arc(s, 5, r3, H - r3, r3, P / 2.0, P / 2.0);
    add_line(s, 6, 0.0, H - r3, 0.0, r0);
    add_arc(s, 7, r0, r0, r0, P, P / 2.0);
}

static AdPoint shape_eval(int piece, double s, void *ctx) {
    Shape *sh = (Shape *)ctx;
    AdPoint p;
    if (sh->piece[piece].kind == AD_PIECE_LINE) {
        p.x = sh->x0[piece] + sh->ux[piece] * s;
        p.y = sh->y0[piece] + sh->uy[piece] * s;
    } else {
        double a = sh->a0[piece] + sh->dir[piece] * s / sh->R[piece];
        p.x = sh->cx[piece] + sh->R[piece] * cos(a);
        p.y = sh->cy[piece] + sh->R[piece] * sin(a);
    }
    return p;
}

static double point_seg_dist(AdPoint p, AdPoint a, AdPoint b) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double wx = p.x - a.x, wy = p.y - a.y;
    double L2 = vx * vx + vy * vy;
    double t = L2 > 0.0 ? (wx * vx + wy * vy) / L2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double dx = p.x - (a.x + t * vx), dy = p.y - (a.y + t * vy);
    return sqrt(dx * dx + dy * dy);
}

/* 输出折线的实测偏差：每段内加密采样真实形状，量到该段切点折线的距离。 */
static double measure_piece_dev(const Shape *sh, int piece, int n,
                                const AdPoint *pts, int off) {
    if (sh->piece[piece].kind == AD_PIECE_LINE) return 0.0;
    double worst = 0.0;
    for (int k = 0; k < DENSE; ++k) {
        double s = sh->piece[piece].len * ((double)k + 0.5) / (double)DENSE;
        AdPoint q = shape_eval(piece, s, (void *)sh);
        int j = (int)(s / sh->piece[piece].len * (double)n);
        if (j < 0) j = 0;
        if (j > n - 1) j = n - 1;
        double d = point_seg_dist(q, pts[off + j], pts[off + j + 1]);
        if (d > worst) worst = d;
    }
    return worst;
}

/* 每段的切点在整条点序列里的起始下标 */
static void piece_offsets(const AdPlan *plan, int *off) {
    int k = 0;
    for (int i = 0; i < plan->npieces; ++i) {
        off[i] = k;
        k += plan->a[i].n;
    }
    off[plan->npieces] = k;
}

static double plan_ms(const AdPiece *pieces, int np, double eps) {
    AdPlan p;
    int bad = -1;
    char field[AD_FIELD_MAX];
    double t0 = now_ms();
    for (int r = 0; r < REPS_PLAN; ++r) {
        if (adaptive_plan(pieces, np, eps, &p, &bad, field) != AD_OK) return -1.0;
    }
    return (now_ms() - t0) / (double)REPS_PLAN;
}

/* 把一段加密采样成折线，写成 gold/ref points 能吃的格式 */
static int write_piece_polyline(const Shape *sh, int piece, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    if (sh->piece[piece].kind == AD_PIECE_LINE) {
        AdPoint a = shape_eval(piece, 0.0, (void *)sh);
        AdPoint b = shape_eval(piece, sh->piece[piece].len, (void *)sh);
        fprintf(f, "%.9f %.9f\n%.9f %.9f\n", a.x, a.y, b.x, b.y);
    } else {
        for (int k = 0; k <= REFINE; ++k) {
            double s = sh->piece[piece].len * (double)k / (double)REFINE;
            AdPoint p = shape_eval(piece, s, (void *)sh);
            fprintf(f, "%.9f %.9f\n", p.x, p.y);
        }
    }
    fclose(f);
    return 0;
}

static int write_points_slice(const AdPoint *pts, int off, int n, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", n + 1);
    for (int i = 0; i <= n; ++i) fprintf(f, "%.6f %.6f\n", pts[off + i].x, pts[off + i].y);
    fclose(f);
    return 0;
}

/* 一段落盘点与 gold 参照点对齐：先由 gold 对该段加密折线等分，再逐点比。
 * 返回最大逐点距离（<0 表示失败），并把 gold 的等分点数回报在 ref_n。 */
static double align_piece(const Shape *sh, int piece, const AdPoint *pts, int off,
                          int n, const char *outdir, const char *ref, int *ref_n) {
    char poly[512], rp[512], op[512], cmd[2048];
    snprintf(poly, sizeof poly, "%s/piece%02d-poly.txt", outdir, piece);
    snprintf(rp, sizeof rp, "%s/piece%02d-ref.txt", outdir, piece);
    snprintf(op, sizeof op, "%s/piece%02d-ours.txt", outdir, piece);
    if (write_piece_polyline(sh, piece, poly) != 0) return -1.0;
    if (write_points_slice(pts, off, n, op) != 0) return -1.0;
    snprintf(cmd, sizeof cmd, "%s points %s %d > %s", ref, poly, n + 1, rp);
    if (system(cmd) != 0) return -1.0;
    AdPoint *r = NULL;
    int rn = 0;
    if (adaptive_points_load(rp, &r, &rn) != AD_OK) return -1.0;
    *ref_n = rn;
    if (rn != n + 1) { free(r); return -1.0; }
    double worst = 0.0;
    for (int i = 0; i <= n; ++i) {
        double dx = pts[off + i].x - r[i].x, dy = pts[off + i].y - r[i].y;
        double d = sqrt(dx * dx + dy * dy);
        if (d > worst) worst = d;
    }
    free(r);
    return worst;
}

static int run_eps(Shape *sh, double eps, const char *outdir,
                   const char *ref, const char *cmp, int *fail) {
    AdPlan plan;
    int err_piece = -1;
    char field[AD_FIELD_MAX];
    int rc = adaptive_plan(sh->piece, NPIECE, eps, &plan, &err_piece, field);
    if (rc != AD_OK) {
        printf("plan failed: %s (piece %d, field %s)\n",
               adaptive_strerror(rc), err_piece, field);
        return 1;
    }

    int off[NPIECE + 1];
    piece_offsets(&plan, off);
    int cap = plan.n_total + 1;
    AdPoint *pts = (AdPoint *)malloc(sizeof(AdPoint) * (size_t)cap);
    AdPoint *uni = (AdPoint *)malloc(sizeof(AdPoint) *
                     (size_t)((long)plan.uniform_per_piece * NPIECE + 1));
    if (!pts || !uni) { free(pts); free(uni); return 1; }
    int npts = adaptive_plan_points(&plan, shape_eval, sh, pts, cap);
    int nuni = adaptive_uniform_points(&plan, shape_eval, sh, uni,
                     plan.uniform_per_piece * NPIECE + 1);

    double t_full0 = now_ms();
    for (int r = 0; r < REPS_FULL; ++r) {
        AdPlan tmp;
        adaptive_plan(sh->piece, NPIECE, eps, &tmp, NULL, NULL);
        adaptive_plan_points(&tmp, shape_eval, sh, pts, cap);
    }
    double full_ms = (now_ms() - t_full0) / (double)REPS_FULL;
    double planms = plan_ms(sh->piece, NPIECE, eps);

    char afile[512], ufile[512];
    snprintf(afile, sizeof afile, "%s/adaptive-eps%g.txt", outdir, eps);
    snprintf(ufile, sizeof ufile, "%s/uniform-eps%g.txt", outdir, eps);
    double tsave0 = now_ms();
    int s1 = adaptive_points_save(afile, pts, npts);
    int s2 = adaptive_points_save(ufile, uni, nuni);
    double save_ms = now_ms() - tsave0;

    printf("\n================ eps = %g ================\n", eps);
    printf("%-3s %-5s %8s %8s %10s %6s %11s %11s %11s\n",
           "#", "kind", "len", "R", "n_cont", "n", "dev_model", "dev_exact", "dev_output");
    double max_dev = 0.0, max_curve = 0.0, sum_curve = 0.0;
    int ncurve_pieces = 0, over = 0;
    for (int i = 0; i < plan.npieces; ++i) {
        AdAlloc *a = &plan.a[i];
        double dout = measure_piece_dev(sh, i, a->n, pts, off[i]);
        double de = a->dev_exact > dout ? a->dev_exact : dout;
        if (de > max_dev) max_dev = de;
        if (a->is_curve) {
            if (de > max_curve) max_curve = de;
            sum_curve += de;
            ++ncurve_pieces;
        }
        if (de > eps) ++over;
        printf("%-3d %-5s %8.3f %8.2f %10.3f %6d %11.3e %11.3e %11.3e%s\n",
               i, a->is_curve ? "arc" : "line", a->len, a->R, a->n_cont, a->n,
               a->dev_model, a->dev_exact, dout, de > eps ? "  OVER" : "");
    }

    printf("-- curve (arc) pieces only --\n");
    printf("%-3s %8s %6s %11s %11s\n", "#", "len", "n", "dev_exact", "dev_output");
    for (int i = 0; i < plan.npieces; ++i) {
        AdAlloc *a = &plan.a[i];
        if (!a->is_curve) continue;
        printf("%-3d %8.3f %6d %11.3e %11.3e\n", i, a->len, a->n,
               a->dev_exact, measure_piece_dev(sh, i, a->n, pts, off[i]));
    }

    double reduction = plan.uniform_total > 0
        ? 1.0 - (double)plan.n_total / (double)plan.uniform_total : 0.0;
    int passes = (over == 0) && (max_dev <= eps) && (reduction >= 0.20) &&
                 (s1 == AD_OK) && (s2 == AD_OK);

    printf("total segments        : %d (curve %d, line %d)\n",
           plan.n_total, plan.n_curve, plan.n_line);
    printf("uniform baseline      : %d = %d per piece x %d pieces\n",
           plan.uniform_total, plan.uniform_per_piece, plan.npieces);
    printf("reduction vs uniform  : %.2f%% (need >= 20%%) %s\n",
           reduction * 100.0, reduction >= 0.20 ? "PASS" : "FAIL");
    printf("max deviation (all)   : %.6e  (eps %.6e) %s\n",
           max_dev, eps, max_dev <= eps ? "PASS" : "FAIL");
    printf("max deviation (curve) : %.6e  mean %.6e over %d arcs\n",
           max_curve, ncurve_pieces ? sum_curve / ncurve_pieces : 0.0, ncurve_pieces);
    printf("timing                : plan %.4f ms  plan+points %.4f ms  save %.4f ms\n",
           planms, full_ms, save_ms);
    printf("points saved          : %s (%d pts), %s (%d pts)\n", afile, npts, ufile, nuni);

    /* 与参照点对齐：每段分别由 gold/ref points 等分同一段加密折线 */
    printf("-- alignment with gold/ref points (per piece, tol cum %.3g) --\n", ALIGN_TOL);
    double align_worst = 0.0;
    int align_err = 0;
    for (int i = 0; i < plan.npieces; ++i) {
        int rn = 0;
        double d = align_piece(sh, i, pts, off[i], plan.a[i].n, outdir, ref, &rn);
        if (d < 0.0) {
            printf("piece %-2d %-5s ALIGN-ERROR\n", i, plan.a[i].is_curve ? "arc" : "line");
            ++align_err;
            continue;
        }
        if (d > align_worst) align_worst = d;
        char poly[512], rp[512], op[512], cmd[2048];
        snprintf(poly, sizeof poly, "%s/piece%02d-poly.txt", outdir, i);
        snprintf(rp, sizeof rp, "%s/piece%02d-ref.txt", outdir, i);
        snprintf(op, sizeof op, "%s/piece%02d-ours.txt", outdir, i);
        snprintf(cmd, sizeof cmd, "%s %s %s %g > %s/piece%02d-cmp.txt",
                 cmp, op, rp, ALIGN_TOL, outdir, i);
        int cr = system(cmd);
        printf("piece %-2d %-5s n=%-4d ref_pts=%-4d max_point_dist=%.3e cmp=%s\n",
               i, plan.a[i].is_curve ? "arc" : "line", plan.a[i].n, rn, d,
               cr == 0 ? "OK" : "FAIL");
        if (cr != 0) passes = 0;
    }
    int align_ok = align_err == 0 && align_worst <= ALIGN_TOL;
    printf("alignment max point distance: %.3e %s (errors %d)\n",
           align_worst, align_ok ? "PASS" : "FAIL", align_err);
    if (!align_ok) passes = 0;

    printf("RESULT eps=%g: %s\n", eps, passes ? "PASS" : "FAIL");
    if (!passes) *fail = 1;
    free(pts);
    free(uni);
    return 0;
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "out";
    const char *ref = argc > 2 ? argv[2] : "../gold/build/ref";
    const char *cmp = argc > 3 ? argv[3] : "../tools/build/cmp_points";
    if (mkdir(outdir, 0755) != 0) {
        struct stat st;
        if (stat(outdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "cannot create output dir %s\n", outdir);
            return 1;
        }
    }
    Shape sh;
    build_shape(&sh);

    printf("shape: %g x %g rounded rect, corner radii 6/20/11/30\n", W, H);
    printf("allocation: n_i = max(1, ceil(len_i * sqrt(k_i / (8 eps)))), k_i = 1/R_i\n");

    int fail = 0;
    run_eps(&sh, 5e-4, outdir, ref, cmp, &fail);
    run_eps(&sh, 2e-5, outdir, ref, cmp, &fail);
    printf("\n%s\n", fail ? "OVERALL FAIL" : "OVERALL PASS");
    return fail ? 1 : 0;
}
