#define _POSIX_C_SOURCE 199309L
#include "gamma.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void set_field(char *field, size_t cap, const char *name) {
    if (!field || cap == 0) return;
    snprintf(field, cap, "%s", name);
}

int gamma_validate(double g, char *field, size_t cap) {
    if (!isfinite(g) || g <= 0.0) {
        set_field(field, cap, "gamma");
        return GAM_E_GAMMA;
    }
    return GAM_OK;
}

unsigned char gamma_encode_level(int level, double g) {
    if (level <= 0) return 0;
    if (level >= 255) return 255;
    if (g == 1.0) return (unsigned char)level;
    double y = 255.0 * pow((double)level / 255.0, 1.0 / g);
    if (!(y > 0.0)) return 0;
    if (y >= 255.0) return 255;
    return (unsigned char)(long)(y + 0.5);
}

int gamma_apply(const unsigned char *in, int w, int h, double g,
                unsigned char *out, char *field, size_t cap) {
    if (!in || !out || w <= 0 || h <= 0) {
        set_field(field, cap, !in ? "in" : (!out ? "out" : "w"));
        return GAM_E_ARG;
    }
    int rc = gamma_validate(g, field, cap);
    if (rc != GAM_OK) return rc;
    /* 逐级查表（256 次幂运算），逐像素只做一次查表。 */
    unsigned char tab[256];
    for (int v = 0; v < 256; ++v) tab[v] = gamma_encode_level(v, g);
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i) out[i] = tab[in[i]];
    return GAM_OK;
}

void gamma_hist(const unsigned char *buf, int w, int h, size_t hist[256]) {
    memset(hist, 0, 256 * sizeof(size_t));
    if (!buf || w <= 0 || h <= 0) return;
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; ++i) hist[buf[i]]++;
}

/* 残差：CDF RMS。参照 CDF 只依赖参照直方图；源直方图每次按 g 重映射。
 * 用整数累加 + 一次除法，避免浮点累积顺序影响 plateau 判定。 */
double gamma_residual_hist(const size_t *src, size_t sn,
                           const size_t *ref, size_t rn, double g) {
    if (!src || !ref || sn == 0 || rn == 0) return NAN;
    size_t mapped[256];
    memset(mapped, 0, sizeof mapped);
    for (int s = 0; s < 256; ++s) {
        if (src[s]) mapped[gamma_encode_level(s, g)] += src[s];
    }
    double acc_s = 0.0, acc_r = 0.0, sum2 = 0.0;
    for (int v = 0; v < 256; ++v) {
        acc_s += (double)mapped[v] / (double)sn;
        acc_r += (double)ref[v] / (double)rn;
        double d = acc_s - acc_r;
        sum2 += d * d;
    }
    return sqrt(sum2 / 256.0);
}

/* 单一灰度级检测：返回该灰度级；否则 -1。 */
static int flat_level_hist(const size_t *h, size_t n) {
    if (!h || n == 0) return -1;
    int lv = -1;
    for (int v = 0; v < 256; ++v) {
        if (h[v] == 0) continue;
        if (lv >= 0) return -1;
        lv = v;
    }
    return lv;
}

/* 残差求值计数（通过 ctx 传递）。 */
typedef struct {
    const size_t *src;
    size_t sn;
    const size_t *ref;
    size_t rn;
    int evals;
} FitCtx;

static double R(FitCtx *c, double g) {
    c->evals++;
    return gamma_residual_hist(c->src, c->sn, c->ref, c->rn, g);
}

/* 定宽 log 网格扫描 [a,b]，返回最好点。若残差到 0 提前收手（0 已是最优）。 */
static double scan(FitCtx *c, double a, double b, int n, double *best_r) {
    double best_g = a;
    *best_r = R(c, a);
    for (int k = 1; k <= n; ++k) {
        double g = a * pow(b / a, (double)k / (double)n);
        double r = R(c, g);
        if (r < *best_r) { *best_r = r; best_g = g; }
        if (*best_r == 0.0) break;
    }
    return best_g;
}

/* 平台边界：z 处残差为 0。sign=-1 向左、+1 向右，在 log(g) 上倍增找首个
 * 非零点，再二分到相对宽度 1e-12。返回仍为 0 的最外侧点。 */
static double plateau_edge(FitCtx *c, double z, int sign) {
    const double LO = GAM_GAMMA_MIN, HI = GAM_GAMMA_MAX;
    double d = 1e-9;
    double a = z;
    for (int i = 0; i < 200; ++i) {
        double b = z * exp(sign * d);
        if (b < LO) b = LO;
        if (b > HI) b = HI;
        if (b == a) return a;
        if (R(c, b) == 0.0) {
            a = b;
            if (a == LO || a == HI) return a;
            d *= 2.0;
        } else {
            /* 在 (a, b) 内二分：a 零、b 非零。 */
            for (int k = 0; k < 200; ++k) {
                double m = sqrt(a * b);
                if (!(m > a && m < b)) break;
                if (R(c, m) == 0.0) a = m; else b = m;
            }
            return a;
        }
    }
    return a;
}

int gamma_fit_hist(const size_t *src, size_t sn,
                   const size_t *ref, size_t rn, GammaFit *out) {
    if (!src || !ref || !out) return GAM_E_ARG;
    if (sn == 0 || rn == 0) return GAM_E_ARG;
    memset(out, 0, sizeof *out);
    double t0 = now_ms();

    int fs = flat_level_hist(src, sn);
    int fr = flat_level_hist(ref, rn);
    if (fs >= 0 || fr >= 0) {
        out->flat = 1;
        out->ms = now_ms() - t0;
        if (fs >= 0) {
            out->flat_level = fs;
            set_field(out->err_field, sizeof out->err_field, "src");
        } else {
            out->flat_level = fr;
            set_field(out->err_field, sizeof out->err_field, "ref");
        }
        return GAM_E_FLAT;
    }

    FitCtx c = { src, sn, ref, rn, 0 };
    const double LO = GAM_GAMMA_MIN, HI = GAM_GAMMA_MAX;

    /* 1. 粗扫。 */
    double best_r;
    double g = scan(&c, LO, HI, GAM_COARSE, &best_r);

    /* 2. 逐级加密。 */
    if (best_r > 0.0) {
        double lg_lo = log(LO), lg_hi = log(HI);
        double span = lg_hi - lg_lo;
        /* 以当前最优为中心，窗口宽度 = 2 * 上一轮格距。 */
        double half = span / GAM_COARSE;
        double a = exp(fmax(lg_lo, log(g) - half));
        double b = exp(fmin(lg_hi, log(g) + half));
        for (int rnd = 0; rnd < GAM_ZOOM_ROUNDS && best_r > 0.0; ++rnd) {
            g = scan(&c, a, b, GAM_ZOOM, &best_r);
            if (best_r == 0.0) break;
            double step = (log(b) - log(a)) / GAM_ZOOM;
            a = exp(fmax(lg_lo, log(g) - step));
            b = exp(fmin(lg_hi, log(g) + step));
            if (!(b > a)) break;
        }
    }

    out->gamma = g;
    out->residual = best_r;
    out->lo = out->hi = g;
    out->at_lo = (g <= LO * (1.0 + 1e-12) && best_r > 0.0);
    out->at_hi = (g >= HI * (1.0 - 1e-12) && best_r > 0.0);

    /* 3. 零残差平台：取几何中心（离两侧量化边界最远）。 */
    if (best_r == 0.0) {
        double lo = plateau_edge(&c, g, -1);
        double hi = plateau_edge(&c, g, +1);
        double mid = sqrt(lo * hi);
        /* 中心应仍在平台内；浮点误差兜底回退到已确认的零点。 */
        if (R(&c, mid) != 0.0) mid = g;
        if (mid < lo) { double t = lo; lo = mid; mid = t; }
        if (mid > hi) { double t = hi; hi = mid; mid = t; }
        out->gamma = mid;
        out->lo = lo;
        out->hi = hi;
        out->at_lo = (lo <= LO);
        out->at_hi = (hi >= HI);
    }
    out->evals = c.evals;
    out->ms = now_ms() - t0;
    return GAM_OK;
}

int gamma_fit(const unsigned char *src, const unsigned char *ref,
              int w, int h, GammaFit *out) {
    if (!src || !ref || !out) return GAM_E_ARG;
    if (w <= 0 || h <= 0) return GAM_E_ARG;
    size_t hs[256], hr[256];
    gamma_hist(src, w, h, hs);
    gamma_hist(ref, w, h, hr);
    size_t n = (size_t)w * (size_t)h;
    return gamma_fit_hist(hs, n, hr, n, out);
}

/* ---- PGM 读取（P5, maxval 255；支持 # 注释） ---- */

static int pgm_token(FILE *f, char *tok, size_t cap) {
    int c;
    do {
        c = fgetc(f);
        if (c == '#') {
            while (c != '\n' && c != EOF) c = fgetc(f);
            if (c == EOF) return -1;
            c = fgetc(f);
        }
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    if (c == EOF) return -1;
    size_t n = 0;
    while (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != EOF) {
        if (n + 1 >= cap) return -1;
        tok[n++] = (char)c;
        c = fgetc(f);
    }
    tok[n] = 0;
    return 0;
}

int gamma_load_pgm(const char *path, unsigned char **buf, int *w, int *h,
                   char *field, size_t cap) {
    if (!path || !buf || !w || !h) {
        set_field(field, cap, "path");
        return GAM_E_ARG;
    }
    *buf = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_field(field, cap, "path");
        return GAM_E_IO;
    }
    char tok[32];
    int W, H, maxv;
    if (pgm_token(f, tok, sizeof tok) != 0 || strcmp(tok, "P5") != 0 ||
        pgm_token(f, tok, sizeof tok) != 0 || (W = atoi(tok)) <= 0 ||
        pgm_token(f, tok, sizeof tok) != 0 || (H = atoi(tok)) <= 0 ||
        pgm_token(f, tok, sizeof tok) != 0 || (maxv = atoi(tok)) != 255) {
        fclose(f);
        set_field(field, cap, "path");
        return GAM_E_PGM;
    }
    /* pgm_token 已吃掉 maxval 后的分隔空白，下一字节即像素数据。 */
    size_t n = (size_t)W * (size_t)H;
    unsigned char *b = (unsigned char *)malloc(n);
    if (!b) {
        fclose(f);
        set_field(field, cap, "path");
        return GAM_E_IO;
    }
    if (fread(b, 1, n, f) != n) {
        free(b);
        fclose(f);
        set_field(field, cap, "path");
        return GAM_E_PGM;
    }
    fclose(f);
    *buf = b;
    *w = W;
    *h = H;
    return GAM_OK;
}
