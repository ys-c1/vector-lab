#include "fixed.h"
#include "raster.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef int64_t fx;   /* 定点数：真值 = raw / 2^q */
typedef __int128 fx2; /* 中间积，杜绝溢出 */

#define FXLIM 0x1p61 /* |v| * 2^q 的安全上限，对应 FX_INT_LIM_BIT */

int fixed_bits_for_eps(double eps) {
    if (!isfinite(eps) || eps <= 0.0) return -1;
    int q = (int)ceil(-log2(eps)); /* 2^-q <= eps */
    if (q < 0) q = 0;
    if (q > 40) q = 40;
    return q;
}

int fixed_check_overflow(const RScene *s, const RBox *win, int q, double *worst, int *qmax) {
    if (!s || q < 0) return -1;
    double m = 0.0;
    if (win) {
        if (fabs(win->minx) > m) m = fabs(win->minx);
        if (fabs(win->maxx) > m) m = fabs(win->maxx);
        if (fabs(win->miny) > m) m = fabs(win->miny);
        if (fabs(win->maxy) > m) m = fabs(win->maxy);
    }
    for (int k = 0; k < s->npolys; ++k)
        for (int i = 0; i < s->polys[k].n; ++i) {
            if (fabs(s->polys[k].pts[i].x) > m) m = fabs(s->polys[k].pts[i].x);
            if (fabs(s->polys[k].pts[i].y) > m) m = fabs(s->polys[k].pts[i].y);
        }
    if (worst) *worst = m;
    if (qmax) {
        int qm = 0;
        while (qm < FX_INT_LIM_BIT - 1 && ldexp(m, qm + 1) < FXLIM) ++qm;
        *qmax = qm;
    }
    return ldexp(m, q) >= FXLIM ? 1 : 0;
}

/* 量化：最近舍入，|v|*2^q 超界返回 -1 */
static int to_fx(double v, int q, fx *out) {
    if (!isfinite(v) || ldexp(fabs(v), q) >= FXLIM) return -1;
    *out = (fx)llround(ldexp(v, q));
    return 0;
}

/* 2^(2q) 中间量舍回 q 位小数（半进远离零），结果须 < 2^62，否则 -1 */
static int fx_round_ck(fx2 v, int q, fx *out) {
    fx2 half = (fx2)1 << (q - 1);
    fx2 lim = (fx2)1 << (62 + q);
    if (v >= lim || v <= -lim) return -1;
    if (v >= 0) *out = (fx)((v + half) >> q);
    else *out = (fx)(-((-v + half) >> q));
    return 0;
}

typedef struct { int n; fx x[64], y[64]; } FxPoly;
typedef struct { int npolys; FxPoly polys[32]; } FxScene;

static int quantize_scene(const RScene *s, int q, FxScene *fs) {
    fs->npolys = s->npolys;
    for (int k = 0; k < s->npolys; ++k) {
        fs->polys[k].n = s->polys[k].n;
        for (int i = 0; i < s->polys[k].n; ++i) {
            if (to_fx(s->polys[k].pts[i].x, q, &fs->polys[k].x[i]) != 0) return -1;
            if (to_fx(s->polys[k].pts[i].y, q, &fs->polys[k].y[i]) != 0) return -1;
        }
    }
    return 0;
}

/* 与浮点 inside 同规则，但改写成无除法整数比较：
 * ax*dy + (bx-ax)*(y-ay) 与 x*dy 比大小，dy<0 时不等号翻转。 */
static int inside_fx(const FxPoly *p, fx x, fx y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        fx ax = p->x[i], ay = p->y[i];
        fx bx = p->x[(i + 1) % p->n], by = p->y[(i + 1) % p->n];
        if ((ay > y) == (by > y)) continue;
        fx dy = by - ay;
        fx2 lhs = (fx2)ax * dy + (fx2)(bx - ax) * (y - ay);
        fx2 rhs = (fx2)x * dy;
        if (dy > 0 ? lhs > rhs : lhs < rhs) ++c;
    }
    return c & 1;
}

static int raster_fx(const FxScene *fs, const RBox *box, int w, int h, int ss, int q, unsigned char *buf) {
    fx minx, miny, maxx, maxy;
    if (to_fx(box->minx, q, &minx) || to_fx(box->miny, q, &miny) ||
        to_fx(box->maxx, q, &maxx) || to_fx(box->maxy, q, &maxy)) return -4;
    fx spanx = maxx - minx, spany = maxy - miny;
    long denx = 2L * ss * w, deny = 2L * ss * h;
    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss && !hit; ++j)
                for (int i = 0; i < ss && !hit; ++i) {
                    /* px = minx + span*(2*ss*x+2*i+1)/(2*ss*w)，单次舍入 */
                    long nx = 2L * ss * x + 2L * i + 1;
                    long ny = 2L * ss * y + 2L * j + 1;
                    fx px = minx + (fx)(((fx2)spanx * nx + denx / 2) / denx);
                    fx py = miny + (fx)(((fx2)spany * ny + deny / 2) / deny);
                    for (int k = 0; k < fs->npolys; ++k)
                        if (inside_fx(&fs->polys[k], px, py)) { hit = 1; break; }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
    return 0;
}

int raster_fill_fixed(const RScene *s, const RBox *box, int w, int h, int ss, int q, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (q < 1 || q > 40) return -5;
    FxScene fs;
    if (quantize_scene(s, q, &fs) != 0) return -4;
    return raster_fx(&fs, box, w, h, ss, q, buf);
}

int pipeline_render_fixed(const RScene *s, RPlace place, RBox win, int w, int h, int q, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    if (q < 1 || q > 40) return -5;
    fx cq, sq, sc, tx, ty;
    /* cos/sin 由 libm 算出后立即量化，此后不再出现浮点 */
    if (to_fx(cos(place.rot), q, &cq) || to_fx(sin(place.rot), q, &sq) ||
        to_fx(place.scale, q, &sc) || to_fx(place.tx, q, &tx) ||
        to_fx(place.ty, q, &ty)) return -4;
    FxScene fs;
    fs.npolys = s->npolys;
    for (int k = 0; k < s->npolys; ++k) {
        fs.polys[k].n = s->polys[k].n;
        for (int i = 0; i < s->polys[k].n; ++i) {
            fx px, py, rx, ry, sx2, sy2;
            if (to_fx(s->polys[k].pts[i].x, q, &px) != 0) return -4;
            if (to_fx(s->polys[k].pts[i].y, q, &py) != 0) return -4;
            if (fx_round_ck((fx2)px * cq - (fx2)py * sq, q, &rx) != 0) return -4;
            if (fx_round_ck((fx2)px * sq + (fx2)py * cq, q, &ry) != 0) return -4;
            if (fx_round_ck((fx2)rx * sc, q, &sx2) != 0) return -4;
            if (fx_round_ck((fx2)ry * sc, q, &sy2) != 0) return -4;
            fx2 gx = (fx2)sx2 + tx, gy = (fx2)sy2 + ty;
            if (gx >= ((fx2)1 << 62) || gx <= -((fx2)1 << 62)) return -4;
            if (gy >= ((fx2)1 << 62) || gy <= -((fx2)1 << 62)) return -4;
            fs.polys[k].x[i] = (fx)gx;
            fs.polys[k].y[i] = (fx)gy;
        }
    }
    return raster_fx(&fs, &win, w, h, 4, q, buf);
}
