#define _POSIX_C_SOURCE 199309L
#include "iaudit.h"
#include <math.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

const char *ia_stage_name(IAStage st) {
    static const char *names[IA_NSTAGES] = {
        "trig", "affine", "window", "sample"};
    return names[st];
}

static void stage_init(IAStageResult *r) {
    r->st = IVL_OK;
    r->max_width = 0.0;
    r->max_bound = 0.0;
    r->max_pixbd = 0.0;
    r->worst_dev = 0.0;
    r->worst_pix = 0.0;
    r->contained = 1;
}

static void fold(IAStageResult *r, const Ival *v) {
    double wd = iv_width(v);
    if (wd > r->max_width) {
        r->max_width = wd;
        /* 真值与浮点结果都落在 [lo,hi] 内，偏差的严格上界是整个宽度 */
        r->max_bound = wd;
    }
}

static void fold_emp(IAStageResult *r, double d) {
    if (d > r->worst_dev) r->worst_dev = d;
}

static void check_inside(IAStageResult *r, double v, const Ival *box) {
    if (v < box->lo - fabs(box->lo) * 1e-12 ||
        v > box->hi + fabs(box->hi) * 1e-12)
        r->contained = 0;
}

/* 采样参数：4 个角 + 中心，确定性抽点，覆盖每类运算一次 */
#define NS 5
static const double SXS[NS] = {0.5, 63.5, 0.5, 63.5, 32.0};
static const double SYS[NS] = {0.5, 0.5, 63.5, 63.5, 32.0};

#define FAIL(rep, stg, status) do {                                   \
    (rep)->rejected = 1; (rep)->fail_stage = (stg);                  \
    (rep)->fail_status = (status); (rep)->stages[(stg)].st = status; \
    return -1;                                                        \
} while (0)

#define CK(rep, stg, expr) do {                 \
    IvalStatus _st = (expr);                    \
    if (_st != IVL_OK) FAIL(rep, stg, _st);     \
} while (0)

/* 输入建模为 ±1 ulp 的区间：把每个标量的最后一位舍入不确定性
   显式带进整条链，得到的是“输入舍入 + 逐运算舍入”的总误差上界。 */
static Ival input_iv(double x) {
    Ival r;
    r.lo = nextafter(x, -INFINITY);
    r.hi = nextafter(x, INFINITY);
    return r;
}

/* 与 pipeline.c 的 move() 完全同序的浮点直算 */
static RPoint f_move(RPoint p, const RPlace *q) {
    double c = cos(q->rot), s = sin(q->rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q->scale + q->tx;
    r.y = (p.x * s + p.y * c) * q->scale + q->ty;
    return r;
}

static double bump(double v, int dir) {
    return nextafter(v, dir > 0 ? INFINITY : -INFINITY);
}

int iaudit_run(const RScene *s, RPlace place, const RBox *win,
               int w, int h, IAReport *rep) {
    double t0 = now_ms();
    for (int i = 0; i < IA_NSTAGES; ++i) stage_init(&rep->stages[i]);
    rep->rejected = 0;
    rep->fail_stage = IA_NSTAGES;
    rep->fail_status = IVL_OK;
    rep->over_tenth = 0;
    rep->tenth_px = (w < h ? w : h) * 0.1;

    double spanx = win->maxx - win->minx, spany = win->maxy - win->miny;
    if (!(spanx > 0.0) || !(spany > 0.0) || !isfinite(spanx) ||
        !isfinite(spany))
        FAIL(rep, IA_WINDOW, IVL_NONFINITE);
    double nx = (double)w / spanx, ny = (double)h / spany;

    /* ---- 区间链：输入按 ±1 ulp 建模，四阶段与浮点直算同序 ---- */
    Ival irot = input_iv(place.rot);
    Ival ico, isi;
    CK(rep, IA_TRIG, iv_cos(&irot, &ico));
    CK(rep, IA_TRIG, iv_sin(&irot, &isi));
    fold(&rep->stages[IA_TRIG], &ico);
    fold(&rep->stages[IA_TRIG], &isi);

    Ival itx = input_iv(place.tx), ity = input_iv(place.ty);
    Ival isc = input_iv(place.scale);
    for (int k = 0; k < s->npolys; ++k)
        for (int i = 0; i < s->polys[k].n; ++i) {
            RPoint p = s->polys[k].pts[i];
            Ival ix = input_iv(p.x), iy = input_iv(p.y);
            Ival t1, t2, t3, t4, mx, my;
            CK(rep, IA_AFFINE, iv_mul(&ix, &ico, &t1));
            CK(rep, IA_AFFINE, iv_mul(&iy, &isi, &t2));
            CK(rep, IA_AFFINE, iv_sub(&t1, &t2, &t3));
            CK(rep, IA_AFFINE, iv_mul(&t3, &isc, &t4));
            CK(rep, IA_AFFINE, iv_add(&t4, &itx, &mx));
            CK(rep, IA_AFFINE, iv_mul(&ix, &isi, &t1));
            CK(rep, IA_AFFINE, iv_mul(&iy, &ico, &t2));
            CK(rep, IA_AFFINE, iv_add(&t1, &t2, &t3));
            CK(rep, IA_AFFINE, iv_mul(&t3, &isc, &t4));
            CK(rep, IA_AFFINE, iv_add(&t4, &ity, &my));
            fold(&rep->stages[IA_AFFINE], &mx);
            fold(&rep->stages[IA_AFFINE], &my);
        }

    Ival imnx = input_iv(win->minx), imxx = input_iv(win->maxx);
    Ival imny = input_iv(win->miny), imxy = input_iv(win->maxy);
    Ival ispx = iv_s((double)w), ispy = iv_s((double)h);
    Ival ispanx, ispany, idx, idy;
    CK(rep, IA_WINDOW, iv_sub(&imxx, &imnx, &ispanx));
    CK(rep, IA_WINDOW, iv_sub(&imxy, &imny, &ispany));
    CK(rep, IA_WINDOW, iv_div(&ispanx, &ispx, &idx));
    CK(rep, IA_WINDOW, iv_div(&ispany, &ispy, &idy));
    fold(&rep->stages[IA_WINDOW], &ispanx);
    fold(&rep->stages[IA_WINDOW], &ispany);
    fold(&rep->stages[IA_WINDOW], &idx);
    fold(&rep->stages[IA_WINDOW], &idy);

    Ival iqx[NS], iqy[NS];
    for (int j = 0; j < NS; ++j) {
        Ival sx = iv_s(SXS[j]), sy = iv_s(SYS[j]);
        Ival a, b;
        CK(rep, IA_SAMPLE, iv_mul(&sx, &idx, &a));
        CK(rep, IA_SAMPLE, iv_add(&imnx, &a, &iqx[j]));
        CK(rep, IA_SAMPLE, iv_mul(&sy, &idy, &b));
        CK(rep, IA_SAMPLE, iv_add(&imny, &b, &iqy[j]));
        fold(&rep->stages[IA_SAMPLE], &iqx[j]);
        fold(&rep->stages[IA_SAMPLE], &iqy[j]);
    }

    /* ---- 经验验证：8 个标量输入各 ±1 ulp，重跑整条最近舍入浮点链 ---- */
    double base_c = cos(place.rot), base_s = sin(place.rot);
    double base_dx = spanx / w, base_dy = spany / h;
    double in[8] = {place.tx, place.ty, place.rot, place.scale,
                    win->minx, win->miny, win->maxx, win->maxy};
    for (int e = 0; e < 8; ++e)
        for (int dir = -1; dir <= 1; dir += 2) {
            RPlace q = place;
            RBox vw = *win;
            double v = bump(in[e], dir);
            switch (e) {
                case 0: q.tx = v; break;
                case 1: q.ty = v; break;
                case 2: q.rot = v; break;
                case 3: q.scale = v; break;
                case 4: vw.minx = v; break;
                case 5: vw.miny = v; break;
                case 6: vw.maxx = v; break;
                default: vw.maxy = v; break;
            }
            fold_emp(&rep->stages[IA_TRIG],
                     fmax(fabs(cos(q.rot) - base_c),
                          fabs(sin(q.rot) - base_s)));
            check_inside(&rep->stages[IA_TRIG], cos(q.rot), &ico);
            check_inside(&rep->stages[IA_TRIG], sin(q.rot), &isi);
            for (int k = 0; k < s->npolys; ++k)
                for (int i = 0; i < s->polys[k].n; ++i) {
                    RPoint b = f_move(s->polys[k].pts[i], &place);
                    RPoint p = f_move(s->polys[k].pts[i], &q);
                    fold_emp(&rep->stages[IA_AFFINE],
                             fmax(fabs(p.x - b.x), fabs(p.y - b.y)));
                }
            double vspanx = vw.maxx - vw.minx, vspany = vw.maxy - vw.miny;
            double vdx = vspanx / w, vdy = vspany / h;
            fold_emp(&rep->stages[IA_WINDOW],
                     fmax(fabs(vdx - base_dx), fabs(vdy - base_dy)));
            check_inside(&rep->stages[IA_WINDOW], vspanx, &ispanx);
            check_inside(&rep->stages[IA_WINDOW], vspany, &ispany);
            check_inside(&rep->stages[IA_WINDOW], vdx, &idx);
            check_inside(&rep->stages[IA_WINDOW], vdy, &idy);
            for (int j = 0; j < NS; ++j) {
                double fx = vw.minx + SXS[j] * vdx;
                double fy = vw.miny + SYS[j] * vdy;
                double bx = win->minx + SXS[j] * base_dx;
                double by = win->miny + SYS[j] * base_dy;
                fold_emp(&rep->stages[IA_SAMPLE],
                         fmax(fabs(fx - bx), fabs(fy - by)));
                check_inside(&rep->stages[IA_SAMPLE], fx, &iqx[j]);
                check_inside(&rep->stages[IA_SAMPLE], fy, &iqy[j]);
            }
        }

    /* ---- 汇总：像素换算、包含性检查、一成画面告警 ---- */
    double pxscale[IA_NSTAGES] = {1.0, fmax(nx, ny), fmax(nx, ny),
                                  fmax(nx, ny)};
    for (int st = 0; st < IA_NSTAGES; ++st) {
        IAStageResult *r = &rep->stages[st];
        r->worst_pix = r->worst_dev * pxscale[st];
        r->max_pixbd = r->max_bound * pxscale[st];
        if (r->max_pixbd > rep->tenth_px)
            rep->over_tenth = 1;
    }
    rep->elapsed_ms = now_ms() - t0;
    return 0;
}
