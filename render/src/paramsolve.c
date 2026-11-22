#define _POSIX_C_SOURCE 199309L
#include "paramsolve.h"
#include "pipeline.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 与 pipeline.c 的 move() 完全相同的仿射放置，保证新路径与旧路径同几何。 */
static RPoint ps_move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

/* 与 raster.c 的 inside() 完全相同的射线奇偶判定。复制而非引用，是为了
 * 让旧的 raster_fill 一行不动；两者一致性由 test.c 的逐字节比对守住。 */
static int ps_inside(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void set_field(char *field, size_t cap, const char *name) {
    if (!field || cap == 0) return;
    snprintf(field, cap, "%s", name);
}

int ps_validate_ss(int ss, char *field, size_t cap) {
    if (ss < PS_SS_MIN || ss > PS_SS_MAX) { set_field(field, cap, "ss"); return PS_E_SS; }
    return PS_OK;
}

int ps_validate_bits(int bits, char *field, size_t cap) {
    if (bits < PS_BITS_MIN || bits > PS_BITS_MAX) { set_field(field, cap, "bits"); return PS_E_BITS; }
    return PS_OK;
}

unsigned char ps_quantize(int cov, int ss, int bits) {
    if (ss < 1) ss = 1;
    int den = ss * ss;
    if (cov < 0) cov = 0;
    if (cov > den) cov = den;
    if (bits <= 1) return cov > 0 ? 255 : 0;          /* any-hit，旧路径口径 */
    int levels = (1 << bits) - 1;                     /* bits<=8，无溢出 */
    int level = (int)(((long)cov * levels) / den);    /* 向下取整，与 gold 一致 */
    return (unsigned char)((level * 255 + levels / 2) / levels);
}

/* 计算覆盖率：每个像素 ss*ss 个子样本里命中的个数（0..ss*ss）。
 * 格点公式与 raster_fill 完全相同；差别只是这里计数而非 any-hit 提前收手。 */
static int ps_coverage(const RScene *s, const RBox *box, int w, int h,
                       int ss, unsigned short *cov) {
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    for (int k = 0; k < s->npolys; ++k)
                        if (ps_inside(&s->polys[k], px, py)) { ++hit; break; }
                }
            cov[y * w + x] = (unsigned short)hit;
        }
    return 0;
}

int ps_render(const RScene *s, RPlace place, RBox win,
              int w, int h, int ss, int bits, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0 || s->npolys <= 0) return PS_E_ARG;
    char f[PS_FIELD_MAX];
    int rc = ps_validate_ss(ss, f, sizeof f);
    if (rc != PS_OK) return rc;
    rc = ps_validate_bits(bits, f, sizeof f);
    if (rc != PS_OK) return rc;
    if (place.scale == 0.0) return PS_E_ARG;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return PS_E_ARG;

    /* 先在原场景上算覆盖率（几何与 pipeline_render 的 move 一致，只是
     * 变换作用在子样本点上等价于作用在多边形顶点上——仿射可交换）。 */
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = ps_move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return PS_E_ARG;
    }
    unsigned short *cov = (unsigned short *)malloc((size_t)w * h * sizeof *cov);
    if (!cov) return PS_E_MEM;
    ps_coverage(&moved, &win, w, h, ss, cov);
    for (int i = 0; i < w * h; ++i) buf[i] = ps_quantize(cov[i], ss, bits);
    free(cov);
    return PS_OK;
}

long ps_diff(const unsigned char *a, const unsigned char *b, int n) {
    long d = 0;
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) ++d;
    return d;
}

int ps_solve(const RScene *s, RPlace place, RBox win, int w, int h,
             const unsigned char *ref,
             int ss_lo, int ss_hi, int bits_lo, int bits_hi,
             int prefer_min, PSResult *out) {
    if (!s || !ref || !out || w <= 0 || h <= 0 || s->npolys <= 0) return PS_E_ARG;
    char f[PS_FIELD_MAX];
    int rc = ps_validate_ss(ss_lo, f, sizeof f);
    if (rc != PS_OK) return rc;
    rc = ps_validate_ss(ss_hi, f, sizeof f);
    if (rc != PS_OK) return rc;
    if (ss_lo > ss_hi) { set_field(f, sizeof f, "ss"); return PS_E_RANGE; }
    rc = ps_validate_bits(bits_lo, f, sizeof f);
    if (rc != PS_OK) return rc;
    rc = ps_validate_bits(bits_hi, f, sizeof f);
    if (rc != PS_OK) return rc;
    if (bits_lo > bits_hi) { set_field(f, sizeof f, "bits"); return PS_E_RANGE; }
    if (place.scale == 0.0) return PS_E_ARG;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return PS_E_ARG;

    memset(out, 0, sizeof *out);
    out->chosen = -1;
    out->prefer_min = prefer_min ? 1 : 0;
    for (int i = 0; i <= PS_SS_MAX; ++i) out->coverage_ms[i] = -1.0;

    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = ps_move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return PS_E_ARG;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)w * h);
    unsigned short *cov = (unsigned short *)malloc((size_t)w * h * sizeof *cov);
    if (!buf || !cov) { free(buf); free(cov); return PS_E_MEM; }

    double t0 = now_ms();
    long best_diff = -1;

    for (int ss = ss_lo; ss <= ss_hi; ++ss) {
        double tc = now_ms();
        ps_coverage(&moved, &win, w, h, ss, cov);
        out->coverage_ms[ss] = now_ms() - tc;

        for (int bits = bits_lo; bits <= bits_hi; ++bits) {
            double tr = now_ms();
            for (int i = 0; i < w * h; ++i) buf[i] = ps_quantize(cov[i], ss, bits);
            long d = ps_diff(buf, ref, w * h);
            double ms = now_ms() - tr;
            if (best_diff < 0 || d < best_diff) best_diff = d;
            if (d != 0) continue;
            if (out->nmatch < PS_MAX_MATCHES) {
                PSCombo *c = &out->matches[out->nmatch++];
                c->ss = ss; c->bits = bits; c->diff_bytes = 0; c->render_ms = ms;
            }
        }
    }
    out->total_ms = now_ms() - t0;
    free(buf); free(cov);

    if (out->nmatch == 0) {
        out->diff_bytes = best_diff < 0 ? 0 : best_diff;
        return PS_E_NOMATCH;
    }

    /* 多组一致时的选取：按成本（ss 优先，其次 bits）取最小或最大。
     * 由于各组都逐字节一致，选哪个都不影响导出字节；这里只决定“报哪组”。 */
    int pick = 0;
    for (int i = 1; i < out->nmatch; ++i) {
        const PSCombo *a = &out->matches[i], *b = &out->matches[pick];
        int less = (a->ss != b->ss) ? (a->ss < b->ss) : (a->bits < b->bits);
        int want = prefer_min ? less : !less;
        int same = (a->ss == b->ss && a->bits == b->bits);
        if (want && !same) pick = i;
    }
    out->chosen = pick;
    out->ss = out->matches[pick].ss;
    out->bits = out->matches[pick].bits;
    out->diff_bytes = 0;
    out->ambiguous = out->nmatch > 1;
    return PS_OK;
}

/* 跳过空白与 # 注释，返回下一个非空字符（已 ungetc 回退）。 */
static int ps_skip(FILE *f) {
    int c;
    for (;;) {
        c = fgetc(f);
        if (c == '#') { while (c != EOF && c != '\n') c = fgetc(f); continue; }
        if (c == EOF) return EOF;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') continue;
        return c;
    }
}

/* 读一个十进制整数；前置字符 c 已从流中取出。 */
static int ps_read_int(FILE *f, int c, int *out) {
    if (c < '0' || c > '9') return -1;
    long v = 0;
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (v > 1000000000L) return -1;
        c = fgetc(f);
    }
    if (c != EOF) ungetc(c, f);
    *out = (int)v;
    return 0;
}

int ps_load_pgm(const char *path, unsigned char **buf, int *w, int *h,
                char *field, size_t cap) {
    if (!path || !buf || !w || !h) { set_field(field, cap, "path"); return PS_E_ARG; }
    *buf = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { set_field(field, cap, "path"); return PS_E_IO; }

    int c = fgetc(f);
    if (c != 'P') { fclose(f); set_field(field, cap, "path"); return PS_E_PGM; }
    c = fgetc(f);
    if (c != '5') { fclose(f); set_field(field, cap, "path"); return PS_E_PGM; }

    int W = 0, H = 0, maxval = 0;
    c = ps_skip(f);
    if (ps_read_int(f, c, &W) != 0) { fclose(f); set_field(field, cap, "path"); return PS_E_PGM; }
    c = ps_skip(f);
    if (ps_read_int(f, c, &H) != 0) { fclose(f); set_field(field, cap, "path"); return PS_E_PGM; }
    c = ps_skip(f);
    if (ps_read_int(f, c, &maxval) != 0) { fclose(f); set_field(field, cap, "path"); return PS_E_PGM; }

    /* maxval 之后必须恰好一个空白字符，紧接着是原始像素数据。 */
    c = fgetc(f);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\v' && c != '\f') {
        fclose(f); set_field(field, cap, "path"); return PS_E_PGM;
    }
    if (W <= 0 || H <= 0 || maxval != 255) {
        fclose(f); set_field(field, cap, "path"); return PS_E_PGM;
    }
    size_t n = (size_t)W * (size_t)H;
    unsigned char *p = (unsigned char *)malloc(n);
    if (!p) { fclose(f); return PS_E_MEM; }
    if (fread(p, 1, n, f) != n) { free(p); fclose(f); set_field(field, cap, "path"); return PS_E_PGM; }
    fclose(f);
    *buf = p; *w = W; *h = H;
    return PS_OK;
}

const char *ps_strerror(int code) {
    switch (code) {
    case PS_OK: return "ok";
    case PS_E_ARG: return "参数非法（空指针/尺寸/缩放）";
    case PS_E_SS: return "超采样数 ss 越界（合法域 1..8）";
    case PS_E_BITS: return "位深 bits 越界（合法域 1..8）";
    case PS_E_RANGE: return "参数区间倒置（lo > hi）";
    case PS_E_SIZE: return "参照图尺寸与 w,h 不符";
    case PS_E_NOMATCH: return "合法域内没有任何组合能逐字节复现参照";
    case PS_E_PGM: return "PGM 格式错误/截断/maxval 非 255";
    case PS_E_IO: return "PGM 打不开";
    case PS_E_MEM: return "内存分配失败";
    default: return "未知错误";
    }
}
