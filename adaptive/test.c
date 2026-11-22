#include "adaptive.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

#define PI 3.14159265358979323846

/* 演示用圆角矩形：直线与圆弧交替，四角半径 6/20/11/30。 */
typedef struct {
    AdPiece piece[8];
    int kind[8];
    double len[8], R[8];
    /* 线段参数 */
    double x0[8], y0[8], ux[8], uy[8];
    /* 圆弧参数 */
    double cx[8], cy[8], a0[8], dir[8];
} Shape;

static void shape_init(Shape *s) {
    memset(s, 0, sizeof(*s));
    const double W = 100.0, H = 70.0;
    const double r0 = 6.0, r1 = 20.0, r2 = 11.0, r3 = 30.0;
    struct { int kind; double x0, y0, x1, y1, cx, cy, R, a0, sweep; } spec[8] = {
        {AD_PIECE_LINE, r0, 0.0, W - r1, 0.0, 0, 0, 0, 0, 0},
        {AD_PIECE_ARC, 0, 0, 0, 0, W - r1, r1, r1, -PI / 2.0, PI / 2.0},
        {AD_PIECE_LINE, W, r1, W, H - r2, 0, 0, 0, 0, 0},
        {AD_PIECE_ARC, 0, 0, 0, 0, W - r2, H - r2, r2, 0.0, PI / 2.0},
        {AD_PIECE_LINE, W - r2, H, r3, H, 0, 0, 0, 0, 0},
        {AD_PIECE_ARC, 0, 0, 0, 0, r3, H - r3, r3, PI / 2.0, PI / 2.0},
        {AD_PIECE_LINE, 0.0, H - r3, 0.0, r0, 0, 0, 0, 0, 0},
        {AD_PIECE_ARC, 0, 0, 0, 0, r0, r0, r0, PI, PI / 2.0},
    };
    for (int i = 0; i < 8; ++i) {
        s->kind[i] = spec[i].kind;
        if (spec[i].kind == AD_PIECE_LINE) {
            double dx = spec[i].x1 - spec[i].x0, dy = spec[i].y1 - spec[i].y0;
            double L = sqrt(dx * dx + dy * dy);
            s->piece[i].kind = AD_PIECE_LINE;
            s->piece[i].len = L;
            s->piece[i].R = 0.0;
            s->len[i] = L;
            s->x0[i] = spec[i].x0; s->y0[i] = spec[i].y0;
            s->ux[i] = dx / L; s->uy[i] = dy / L;
        } else {
            double L = spec[i].R * fabs(spec[i].sweep);
            s->piece[i].kind = AD_PIECE_ARC;
            s->piece[i].len = L;
            s->piece[i].R = spec[i].R;
            s->len[i] = L;
            s->R[i] = spec[i].R;
            s->cx[i] = spec[i].cx; s->cy[i] = spec[i].cy;
            s->a0[i] = spec[i].a0;
            s->dir[i] = spec[i].sweep >= 0.0 ? 1.0 : -1.0;
        }
    }
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

/* 输出折线的实测偏差：段内加密采样真实形状，量到对应切点弦的距离。 */
static double measured_dev(const Shape *sh, const AdPlan *plan, int piece,
                           const AdPoint *pts, int off) {
    if (!plan->a[piece].is_curve) return 0.0;
    int n = plan->a[piece].n;
    double worst = 0.0;
    for (int k = 0; k < 4000; ++k) {
        double s = sh->piece[piece].len * ((double)k + 0.5) / 4000.0;
        AdPoint q = shape_eval(piece, s, (void *)sh);
        int j = (int)(s / sh->piece[piece].len * n);
        if (j < 0) j = 0;
        if (j > n - 1) j = n - 1;
        double d = point_seg_dist(q, pts[off + j], pts[off + j + 1]);
        if (d > worst) worst = d;
    }
    return worst;
}

static void offsets(const AdPlan *plan, int *off) {
    int k = 0;
    for (int i = 0; i < plan->npieces; ++i) { off[i] = k; k += plan->a[i].n; }
    off[plan->npieces] = k;
}

int main(void) {
    /* ---- 连续下界式 (2)：c = len*sqrt(k/(8 eps)) ---- */
    CK(fabs(adaptive_piece_cont(10.0, 5.0, 0.5) -
            10.0 * sqrt((1.0 / 5.0) / 4.0)) < 1e-12);
    CK(adaptive_piece_cont(10.0, 5.0, 1e-9) >
       adaptive_piece_cont(10.0, 5.0, 1e-3));
    CK(adaptive_piece_cont(10.0, 0.0, 1e-3) == 0.0);

    /* ---- 精确最少份数：n 满足、n-1 不满足 ---- */
    for (int t = 0; t < 4; ++t) {
        double R = (double[]){6.0, 11.0, 20.0, 30.0}[t];
        double len = PI / 2.0 * R;
        double eps = 5e-4;
        int n = adaptive_piece_exact_n(len, R, eps);
        double phi = len / R;
        CK(R * (1.0 - cos(phi / (2.0 * n))) <= eps);
        if (n > 1) CK(R * (1.0 - cos(phi / (2.0 * (n - 1)))) > eps);
    }
    /* 同一圆心角下份数按 sqrt(R) 增长；同一半径下份数按弧长线性增长 */
    int n6 = adaptive_piece_exact_n(PI / 2.0 * 6.0, 6.0, 5e-4);
    int n30 = adaptive_piece_exact_n(PI / 2.0 * 30.0, 30.0, 5e-4);
    CK(n6 < n30);
    CK(fabs((double)n30 / (double)n6 - sqrt(5.0)) < 0.02);
    int n_half = adaptive_piece_exact_n(PI / 4.0 * 20.0, 20.0, 5e-4);
    int n_full = adaptive_piece_exact_n(PI / 2.0 * 20.0, 20.0, 5e-4);
    CK(n_half * 2 >= n_full && n_half * 2 <= n_full + 2);
    /* eps 收紧，份数单调不减 */
    CK(adaptive_piece_exact_n(PI / 2.0 * 20.0, 20.0, 2e-5) >
       adaptive_piece_exact_n(PI / 2.0 * 20.0, 20.0, 5e-4));

    /* ---- 规划：直线 1 段、圆弧按式 (4) 取整；逐段不超上界 ---- */
    Shape sh;
    shape_init(&sh);
    AdPlan plan;
    int bad = -1;
    char field[AD_FIELD_MAX];
    double eps = 5e-4;
    CK(adaptive_plan(sh.piece, 8, eps, &plan, &bad, field) == AD_OK);
    CK(plan.npieces == 8);
    CK(plan.a[0].is_curve == 0 && plan.a[0].n == 1);
    CK(plan.a[2].n == 1 && plan.a[4].n == 1 && plan.a[6].n == 1);
    for (int i = 0; i < 8; ++i) {
        if (plan.a[i].is_curve) {
            CK(plan.a[i].n >= (int)ceil(plan.a[i].n_cont));
            CK(plan.a[i].n == (plan.a[i].n_cont > 1.0
                   ? (int)ceil(plan.a[i].n_cont) : 1));
            CK(plan.a[i].dev_exact <= eps);
            CK(plan.a[i].dev_model <= eps);
            /* 少切一刀就超上界（逐段硬最优） */
            if (plan.a[i].n > 1) {
                double d = plan.a[i].len / (plan.a[i].n - 1) / plan.a[i].R;
                double dev = plan.a[i].R * (1.0 - cos(d * 0.5));
                CK(dev > eps);
            }
        } else {
            CK(plan.a[i].n == 1);
            CK(plan.a[i].kappa == 0.0 && plan.a[i].dev_exact == 0.0);
        }
    }
    CK(plan.n_total == plan.n_curve + plan.n_line);
    CK(plan.n_total == 397);          /* 手算：61+83+112+137 + 4 条直线 */
    CK(plan.n_curve == 393 && plan.n_line == 4);

    /* ---- 拉格朗日比例：等长圆弧份数之比 = sqrt(k_i/k_j) ---- */
    AdPiece pair[2] = {
        {AD_PIECE_ARC, PI / 2.0 * 20.0, 20.0},
        {AD_PIECE_ARC, PI / 2.0 * 5.0, 5.0},
    };
    AdPlan pp;
    CK(adaptive_plan(pair, 2, 1e-6, &pp, &bad, field) == AD_OK);
    double ratio = (double)pp.a[0].n / (double)pp.a[1].n;
    CK(fabs(ratio - 2.0) < 0.02);     /* 等角下 k 之比 4，份数之比 2 */
    double devs = pp.a[0].dev_exact + pp.a[1].dev_exact;
    CK(devs <= 2.0 * 1e-6);

    /* ---- 均匀基线：每段同样份数，必须满足上界，且不少于自适应 ---- */
    CK(plan.uniform_total == plan.uniform_per_piece * 8);
    CK(plan.uniform_per_piece >= 1);
    for (int i = 0; i < 8; ++i) {
        if (!plan.a[i].is_curve) continue;
        int ne = adaptive_piece_exact_n(plan.a[i].len, plan.a[i].R, eps);
        CK(plan.uniform_per_piece >= ne);
    }
    CK(plan.uniform_total > plan.n_total);
    double reduction = 1.0 - (double)plan.n_total / (double)plan.uniform_total;
    CK(reduction >= 0.20);

    /* ---- 两档上界：段数增加、仍逐段不超上界 ---- */
    AdPlan plan2;
    CK(adaptive_plan(sh.piece, 8, 2e-5, &plan2, &bad, field) == AD_OK);
    CK(plan2.n_total > plan.n_total);
    CK(plan2.n_total == 1958);
    for (int i = 0; i < 8; ++i) {
        CK(plan2.a[i].dev_exact <= 2e-5);
        if (plan2.a[i].is_curve) CK(plan2.a[i].dev_model <= 2e-5);
    }
    CK(1.0 - (double)plan2.n_total / (double)plan2.uniform_total >= 0.20);

    /* ---- 落点：点数 = sum n + 1；段间端点相接；实测偏差不超上界 ---- */
    int off[9];
    offsets(&plan, off);
    AdPoint *pts = (AdPoint *)malloc(sizeof(AdPoint) * (size_t)(plan.n_total + 1));
    int got = adaptive_plan_points(&plan, shape_eval, &sh, pts, plan.n_total + 1);
    CK(got == plan.n_total + 1);
    for (int i = 0; i < 8; ++i) {
        AdPoint a = shape_eval(i, 0.0, &sh);
        CK(fabs(pts[off[i]].x - a.x) < 1e-12 && fabs(pts[off[i]].y - a.y) < 1e-12);
        if (i + 1 < 8) {
            AdPoint b = shape_eval(i, sh.piece[i].len, &sh);
            CK(fabs(pts[off[i + 1]].x - b.x) < 1e-9 &&
               fabs(pts[off[i + 1]].y - b.y) < 1e-9);
        }
        double md = measured_dev(&sh, &plan, i, pts, off[i]);
        CK(md <= eps);
        /* 加密采样只在离散点上量，较解析弓高略小；上界判定用解析值 */
        if (plan.a[i].is_curve)
            CK(md <= plan.a[i].dev_exact &&
               md >= plan.a[i].dev_exact * (1.0 - 1e-3));
    }
    /* 闭合：最后一点回到起点 */
    AdPoint first = shape_eval(0, 0.0, &sh);
    CK(fabs(pts[plan.n_total].x - first.x) < 1e-9 &&
       fabs(pts[plan.n_total].y - first.y) < 1e-9);

    /* 缓冲不足拒绝 */
    CK(adaptive_plan_points(&plan, shape_eval, &sh, pts, plan.n_total) == AD_E_CAP);

    /* ---- 均匀落点：每段 uniform_per_piece 份 ---- */
    int uni_n = plan.uniform_per_piece * 8 + 1;
    AdPoint *uni = (AdPoint *)malloc(sizeof(AdPoint) * (size_t)uni_n);
    int ug = adaptive_uniform_points(&plan, shape_eval, &sh, uni, uni_n);
    CK(ug == uni_n);
    /* 均匀分配下逐段偏差都不超上界（基线合法） */
    int uoff = 0;
    for (int i = 0; i < 8; ++i) {
        int nu = plan.uniform_per_piece;
        if (plan.a[i].is_curve) {
            double md = 0.0;
            for (int k = 0; k < 4000; ++k) {
                double s = sh.piece[i].len * ((double)k + 0.5) / 4000.0;
                AdPoint q = shape_eval(i, s, &sh);
                int j = (int)(s / sh.piece[i].len * nu);
                if (j < 0) j = 0;
                if (j > nu - 1) j = nu - 1;
                double d = point_seg_dist(q, uni[uoff + j], uni[uoff + j + 1]);
                if (d > md) md = d;
            }
            CK(md <= eps);
        }
        uoff += nu;
    }
    free(uni);

    /* ---- 点文件存读回环 ---- */
    const char *tmp = "build/test-points.txt";
    CK(adaptive_points_save(tmp, pts, plan.n_total + 1) == AD_OK);
    AdPoint *back = NULL;
    int bn = 0;
    CK(adaptive_points_load(tmp, &back, &bn) == AD_OK);
    CK(bn == plan.n_total + 1);
    int same = 1;
    for (int i = 0; i < bn; ++i)
        if (fabs(back[i].x - pts[i].x) > 5e-6 || fabs(back[i].y - pts[i].y) > 5e-6)
            same = 0;
    CK(same);
    free(back);
    free(pts);

    /* ---- 误差路径 ---- */
    CK(adaptive_plan(NULL, 8, eps, &plan, &bad, field) == AD_E_ARG);
    CK(strcmp(field, "pieces") == 0);
    CK(adaptive_plan(sh.piece, 0, eps, &plan, &bad, field) == AD_E_ARG);
    CK(adaptive_plan(sh.piece, AD_MAX_PIECES + 1, eps, &plan, &bad, field) == AD_E_ARG);
    CK(adaptive_plan(sh.piece, 8, eps, NULL, &bad, field) == AD_E_ARG);
    CK(adaptive_plan(sh.piece, 8, 0.0, &plan, &bad, field) == AD_E_EPS);
    CK(strcmp(field, "eps") == 0);
    CK(adaptive_plan(sh.piece, 8, -1.0, &plan, &bad, field) == AD_E_EPS);
    CK(adaptive_plan(sh.piece, 8, NAN, &plan, &bad, field) == AD_E_EPS);
    CK(adaptive_plan(sh.piece, 8, INFINITY, &plan, &bad, field) == AD_E_EPS);

    AdPiece one = {AD_PIECE_ARC, 10.0, 5.0};
    AdPiece line = {AD_PIECE_LINE, 10.0, 0.0};
    AdPiece badlen = {AD_PIECE_ARC, 0.0, 5.0};
    AdPiece badlen2 = {AD_PIECE_ARC, -2.0, 5.0};
    AdPiece badR = {AD_PIECE_ARC, 10.0, 0.0};
    AdPiece badR2 = {AD_PIECE_ARC, 10.0, -5.0};
    AdPiece lineR = {AD_PIECE_LINE, 10.0, 2.0};
    AdPiece badkind = {7, 10.0, 0.0};
    AdPiece nanlen = {AD_PIECE_ARC, NAN, 5.0};
    CK(adaptive_plan(&one, 1, eps, &plan, &bad, field) == AD_OK);
    CK(adaptive_plan(&line, 1, eps, &plan, &bad, field) == AD_OK);
    CK(plan.n_total == 1 && plan.a[0].n == 1);
    bad = -1;
    CK(adaptive_plan(&badlen, 1, eps, &plan, &bad, field) == AD_E_LEN);
    CK(bad == 0 && strcmp(field, "len") == 0);
    CK(adaptive_plan(&badlen2, 1, eps, &plan, &bad, field) == AD_E_LEN);
    CK(adaptive_plan(&nanlen, 1, eps, &plan, &bad, field) == AD_E_LEN);
    CK(adaptive_plan(&badR, 1, eps, &plan, &bad, field) == AD_E_RADIUS);
    CK(strcmp(field, "R") == 0);
    CK(adaptive_plan(&badR2, 1, eps, &plan, &bad, field) == AD_E_RADIUS);
    CK(adaptive_plan(&lineR, 1, eps, &plan, &bad, field) == AD_E_RADIUS);
    CK(adaptive_plan(&badkind, 1, eps, &plan, &bad, field) == AD_E_KIND);
    CK(strcmp(field, "kind") == 0);
    /* 出错段号 */
    bad = -1;
    AdPiece mixed[3] = {{AD_PIECE_LINE, 5.0, 0.0}, {AD_PIECE_ARC, 5.0, 4.0},
                        {AD_PIECE_ARC, 2.0, 0.0}};
    CK(adaptive_plan(mixed, 3, eps, &plan, &bad, field) == AD_E_RADIUS);
    CK(bad == 2);

    /* ---- 曲线段实测偏差、直线段为 0；弯道与直道份数鲜明对比 ---- */
    CK(adaptive_arc_dev_measure(20.0, PI / 2.0 * 20.0, 112, 65) > 0.0);
    CK(adaptive_arc_dev_measure(20.0, PI / 2.0 * 20.0, 1, 65) >
       adaptive_arc_dev_measure(20.0, PI / 2.0 * 20.0, 112, 65));
    CK(adaptive_arc_dev_measure(0.0, 5.0, 3, 9) == 0.0);

    /* ---- strerror 覆盖 ---- */
    CK(strcmp(adaptive_strerror(AD_OK), "ok") == 0);
    CK(adaptive_strerror(-999) != NULL);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
