/* 逐刀裁剪账目驱动。
 *
 * 目的：两刀连着裁和一刀裁对不上时，把每一刀之后的面积落盘，用账目
 * 反推第一个窗口真正生效的区域，再和独立方法直接算出的区域比面积。
 *
 * 用法：
 *   ledger plan <plan.txt> <outdir>
 *
 * plan 每行：<序列名> <多边形文件> <窗口清单文件>
 * 多边形/清单路径相对 plan 所在目录；清单里的窗口路径相对清单所在目录。
 * 清单每行：<窗口号> <窗口文件>，每行 "x y" 为一个顶点。
 *
 * 每个序列落盘：
 *   <outdir>/<序列>/steps/stepNN_poly.txt|_area.txt   第 NN 刀之后的主体多边形
 *   <outdir>/<序列>/eff/stepNN_poly.txt|_area.txt     第一窗口按同一序列逐刀
 *                                                     裁剩的有效区域（反推账目）
 *   <outdir>/<序列>/direct/stepNN_poly.txt|_area.txt  独立方法直接求出的
 *                                                     有效区域
 *   <outdir>/<序列>/direct/stepNN_oneshot_*           一次裁（用反推区域）
 *   <outdir>/report.tsv                               每步账目
 *   <outdir>/summary.tsv                              每序列汇总
 *
 * 账目检查（相对偏差 > 1e-6 即失败并停在该步）：
 *   1) 连续裁剪的主体面积 vs 用反推有效区域一次裁出的面积；
 *   2) 反推有效区域面积 vs 独立直接求出的区域面积。
 *
 * 退出码：0 全部通过；2 输入被拒；3 账目不一致；4 落盘失败。
 */
#define _POSIX_C_SOURCE 200809L
#include "clip.h"
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define TOL_REL 1e-6
#define MAX_WINS 256
#define MAX_SEQS 64
#define PATH_MAX_LEN 1200
#define ERR_MAX 1536

typedef struct {
    int id;
    char file[512];
    CPoint pts[CLIP_MAX_VERTS];
    int n;
} Window;

typedef struct {
    char name[128];
    char poly[512];
    char manifest[512];
} SeqEntry;

static double signed_area(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static double rel_diff(double a, double b) {
    double m = fmax(fabs(a), fabs(b));
    if (m == 0.0) return 0.0;
    return fabs(a - b) / m;
}

static void path_dir(const char *path, char *dir, size_t n) {
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(dir, n, "."); return; }
    size_t len = (size_t)(slash - path);
    if (len == 0) len = 1;
    if (len >= n) len = n - 1;
    memcpy(dir, path, len);
    dir[len] = '\0';
}

/* 由碎片拼路径；超长视为落盘失败，不静默截断。 */
static int join_path(char *out, size_t n, const char *a, const char *b,
                     const char *c, const char *d) {
    int w = snprintf(out, n, "%s%s%s%s", a, b, c, d);
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

/* 相对路径按 base 所在目录解析；拼出来的路径必须能完整放进 out。 */
static int resolve_near(const char *base, const char *rel, char *out, size_t n) {
    if (rel[0] == '/') {
        if (strlen(rel) >= n) return -1;
        snprintf(out, n, "%s", rel);
        return 0;
    }
    char dir[PATH_MAX_LEN];
    path_dir(base, dir, sizeof dir);
    int w = snprintf(out, n, "%s/%s", dir, rel);
    return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX_LEN];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof tmp) return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* 拼接诊断文本；路径过长时给出截断提示而不是溢出。 */
static void errmsg(char *err, size_t errsz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= errsz) {
        const char *tail = "...（路径过长，已截断）";
        size_t tl = strlen(tail);
        if (errsz > tl + 1) memcpy(err + errsz - tl - 1, tail, tl + 1);
    }
}

static int load_points(const char *path, CPoint *pts, int cap, int *out_n,
                       char *err, size_t errsz) {
    FILE *f = fopen(path, "r");
    if (!f) { errmsg(err, errsz, "无法打开 %s", path); return -1; }
    int n = 0, r;
    double x, y;
    while ((r = fscanf(f, "%lf %lf", &x, &y)) == 2) {
        if (n >= cap) {
            errmsg(err, errsz, "%s 顶点超过上限 %d", path, cap);
            fclose(f);
            return -1;
        }
        pts[n].x = x;
        pts[n].y = y;
        ++n;
    }
    fclose(f);
    if (r != EOF) { errmsg(err, errsz, "%s 有无法解析的顶点行", path); return -1; }
    *out_n = n;
    return 0;
}

static double coord_scale(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        s = fmax(s, fabs(p[i].x));
        s = fmax(s, fabs(p[i].y));
    }
    return s;
}

static int window_convex(const CPoint *p, int n) {
    double scale = coord_scale(p, n);
    double eps = 1e-12 * (1.0 + scale * scale);
    double s = signed_area(p, n);
    if (fabs(s) <= eps) return 0;
    double sign = s > 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n], c = p[(i + 2) % n];
        double cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (cr * sign < -eps) return 0;
        if (a.x == b.x && a.y == b.y) return 0;
    }
    return 1;
}

static int load_manifest(const char *path, Window *wins, int cap, int *out_n,
                         char *err, size_t errsz) {
    FILE *f = fopen(path, "r");
    if (!f) { errmsg(err, errsz, "无法打开窗口清单 %s", path); return -1; }
    int n = 0, lineno = 0;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        ++lineno;
        char *s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '\0' || *s == '\n' || *s == '\r' || *s == '#') continue;
        int id;
        char file[512];
        if (sscanf(s, "%d %511s", &id, file) != 2) {
            snprintf(err, errsz, "清单第 %d 行格式错误（需要 <窗口号> <窗口文件>）", lineno);
            fclose(f);
            return -1;
        }
        if (n >= cap) { snprintf(err, errsz, "窗口数量超过上限 %d", cap); fclose(f); return -1; }
        for (int i = 0; i < n; ++i) {
            if (wins[i].id == id) {
                snprintf(err, errsz, "第 %d 个窗口（窗口号 %d）与第 %d 个窗口窗口号重复",
                         n + 1, id, i + 1);
                fclose(f);
                return -1;
            }
        }
        wins[n].id = id;
        if (strlen(file) >= sizeof wins[n].file) {
            snprintf(err, errsz, "清单第 %d 行窗口文件路径过长", lineno);
            fclose(f);
            return -1;
        }
        snprintf(wins[n].file, sizeof wins[n].file, "%s", file);
        ++n;
    }
    fclose(f);
    if (n == 0) { snprintf(err, errsz, "窗口清单里没有任何窗口"); return -1; }

    for (int i = 0; i < n; ++i) {
        char wpath[PATH_MAX_LEN];
        char msg[1024];
        if (resolve_near(path, wins[i].file, wpath, sizeof wpath) != 0) {
            snprintf(err, errsz, "第 %d 个窗口（窗口号 %d）窗口文件路径过长", i + 1, wins[i].id);
            return -1;
        }
        int wn = 0;
        if (load_points(wpath, wins[i].pts, CLIP_MAX_VERTS, &wn, msg, sizeof msg) != 0) {
            errmsg(err, errsz, "第 %d 个窗口（窗口号 %d）：%s", i + 1, wins[i].id, msg);
            return -1;
        }
        if (wn == 0) {
            snprintf(err, errsz, "第 %d 个窗口（窗口号 %d）为空", i + 1, wins[i].id);
            return -1;
        }
        if (wn < 3) {
            snprintf(err, errsz, "第 %d 个窗口（窗口号 %d）顶点不足三个（实际 %d 个）",
                     i + 1, wins[i].id, wn);
            return -1;
        }
        if (!window_convex(wins[i].pts, wn)) {
            snprintf(err, errsz, "第 %d 个窗口（窗口号 %d）退化或不是凸多边形",
                     i + 1, wins[i].id);
            return -1;
        }
        wins[i].n = wn;
    }
    *out_n = n;
    return 0;
}

/* ---- 独立实现：直接求多个凸多边形的交集（候选点 + 凸包），
 * 不走 Sutherland–Hodgman，用来当“直接裁出来”的对照。 ---- */

static int cmp_xy(const void *pa, const void *pb) {
    const CPoint *a = (const CPoint *)pa, *b = (const CPoint *)pb;
    if (a->x < b->x) return -1;
    if (a->x > b->x) return 1;
    if (a->y < b->y) return -1;
    if (a->y > b->y) return 1;
    return 0;
}

static double cross_o(CPoint o, CPoint a, CPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int point_in_convex(const CPoint *p, int n, CPoint q, double eps) {
    double s = signed_area(p, n);
    double sign = s >= 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        double cr = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
        if (cr * sign < -eps) return 0;
    }
    return 1;
}

static int point_in_all(CPoint q, const Window *wins, int nw, double eps) {
    for (int i = 0; i < nw; ++i)
        if (!point_in_convex(wins[i].pts, wins[i].n, q, eps)) return 0;
    return 1;
}

static int seg_meet(CPoint a, CPoint b, CPoint c, CPoint d, CPoint *out) {
    double rx = b.x - a.x, ry = b.y - a.y;
    double sx = d.x - c.x, sy = d.y - c.y;
    double den = rx * sy - ry * sx;
    if (fabs(den) < 1e-12) return 0;
    double qx = c.x - a.x, qy = c.y - a.y;
    double t = (qx * sy - qy * sx) / den;
    double u = (qx * ry - qy * rx) / den;
    if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9) return 0;
    out->x = a.x + rx * t;
    out->y = a.y + ry * t;
    return 1;
}

static int hull_build(CPoint *p, int n, CPoint *h, double eps) {
    if (n <= 0) return 0;
    qsort(p, (size_t)n, sizeof(CPoint), cmp_xy);
    int m = 0;
    double dedupe = eps > 0.0 ? sqrt(eps) : 0.0;
    for (int i = 0; i < n; ++i) {
        if (m > 0 && fabs(p[i].x - p[m - 1].x) <= dedupe &&
            fabs(p[i].y - p[m - 1].y) <= dedupe) continue;
        p[m++] = p[i];
    }
    if (m <= 2) {
        for (int i = 0; i < m; ++i) h[i] = p[i];
        return m;
    }
    int k = 0;
    for (int i = 0; i < m; ++i) {
        while (k >= 2 && cross_o(h[k - 2], h[k - 1], p[i]) <= eps) --k;
        h[k++] = p[i];
    }
    int lower = k + 1;
    for (int i = m - 2; i >= 0; --i) {
        while (k >= lower && cross_o(h[k - 2], h[k - 1], p[i]) <= eps) --k;
        h[k++] = p[i];
    }
    return k - 1;
}

/* 返回交集面积；poly/outn 可为 NULL。out 用 malloc，调用者 free。 */
static int direct_region(const Window *wins, int nw, CPoint **poly_out, int *outn,
                         double *area_out) {
    size_t cap = 0;
    double scale = 0.0;
    for (int i = 0; i < nw; ++i) {
        cap += (size_t)wins[i].n;
        scale = fmax(scale, coord_scale(wins[i].pts, wins[i].n));
        for (int j = i + 1; j < nw; ++j)
            cap += (size_t)wins[i].n * (size_t)wins[j].n;
    }
    cap += 4;
    CPoint *cand = (CPoint *)malloc(cap * sizeof(CPoint));
    CPoint *hull = (CPoint *)malloc((cap + 2) * sizeof(CPoint));
    if (!cand || !hull) { free(cand); free(hull); return -1; }

    double eps = 1e-9 * (1.0 + scale * scale);
    size_t m = 0;
    for (int i = 0; i < nw; ++i)
        for (int k = 0; k < wins[i].n; ++k)
            if (point_in_all(wins[i].pts[k], wins, nw, eps)) cand[m++] = wins[i].pts[k];

    for (int i = 0; i < nw; ++i)
        for (int j = i + 1; j < nw; ++j)
            for (int a = 0; a < wins[i].n; ++a)
                for (int b = 0; b < wins[j].n; ++b) {
                    CPoint x;
                    if (seg_meet(wins[i].pts[a], wins[i].pts[(a + 1) % wins[i].n],
                                 wins[j].pts[b], wins[j].pts[(b + 1) % wins[j].n], &x) &&
                        point_in_all(x, wins, nw, eps))
                        cand[m++] = x;
                }

    int hn = hull_build(cand, (int)m, hull, eps);
    double area = 0.0;
    int out_count = 0;
    if (hn >= 3) {
        area = fabs(signed_area(hull, hn));
        out_count = hn;
    }
    free(cand);
    if (outn) *outn = out_count;
    if (area_out) *area_out = area;
    if (poly_out) {
        if (out_count == 0) { free(hull); *poly_out = NULL; }
        else *poly_out = hull;
    } else {
        free(hull);
    }
    return 0;
}

/* ---- 落盘 ---- */

static int write_poly(const char *path, const CPoint *p, int n) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) fprintf(f, "%.17g %.17g\n", p[i].x, p[i].y);
    fclose(f);
    return 0;
}

static int write_area(const char *path, double a) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%.12f\n", a);
    fclose(f);
    return 0;
}

static int write_pair(const char *poly, const char *area, const CPoint *p, int n, double a) {
    if (write_poly(poly, p, n) != 0) return -1;
    if (write_area(area, a) != 0) return -1;
    return 0;
}

/* ---- 单序列 ---- */

static int run_sequence(const char *plan_path, const char *outroot,
                        const SeqEntry *seq, FILE *rpt, FILE *sum) {
    char seqdir[PATH_MAX_LEN], dir[PATH_MAX_LEN], poly_path[PATH_MAX_LEN];
    if (join_path(seqdir, sizeof seqdir, outroot, "/", seq->name, "") != 0 ||
        join_path(dir, sizeof dir, seqdir, "/steps", "", "") != 0) {
        fprintf(stderr, "拒绝：序列 %s 输出路径过长\n", seq->name);
        return 4;
    }
    if (mkdir_p(dir) != 0) return 4;
    if (join_path(dir, sizeof dir, seqdir, "/eff", "", "") != 0 ||
        mkdir_p(dir) != 0) return 4;
    if (join_path(dir, sizeof dir, seqdir, "/direct", "", "") != 0 ||
        mkdir_p(dir) != 0) return 4;

    char err[ERR_MAX] = {0};
    if (resolve_near(plan_path, seq->poly, poly_path, sizeof poly_path) != 0) {
        fprintf(stderr, "拒绝：序列 %s 多边形路径过长\n", seq->name);
        return 2;
    }
    CPoly base;
    if (load_points(poly_path, base.pts, CLIP_MAX_VERTS, &base.count, err, sizeof err) != 0) {
        fprintf(stderr, "拒绝：序列 %s：%s\n", seq->name, err);
        return 2;
    }
    if (base.count < 3) {
        fprintf(stderr, "拒绝：序列 %s：主体多边形顶点不足三个（实际 %d 个）\n",
                seq->name, base.count);
        return 2;
    }

    char manifest_path[PATH_MAX_LEN];
    if (resolve_near(plan_path, seq->manifest, manifest_path, sizeof manifest_path) != 0) {
        fprintf(stderr, "拒绝：序列 %s 窗口清单路径过长\n", seq->name);
        return 2;
    }
    Window *wins = (Window *)calloc(MAX_WINS, sizeof(Window));
    if (!wins) return 4;
    int nw = 0;
    if (load_manifest(manifest_path, wins, MAX_WINS, &nw, err, sizeof err) != 0) {
        fprintf(stderr, "拒绝：序列 %s：%s\n", seq->name, err);
        free(wins);
        return 2;
    }

    /* 基线：第 0 刀之前的主体。 */
    char pbuf[PATH_MAX_LEN], abuf[PATH_MAX_LEN];
    double base_area = fabs(signed_area(base.pts, base.count));
    if (join_path(pbuf, sizeof pbuf, seqdir, "/steps/step00_poly.txt", "", "") != 0 ||
        join_path(abuf, sizeof abuf, seqdir, "/steps/step00_area.txt", "", "") != 0 ||
        write_pair(pbuf, abuf, base.pts, base.count, base_area) != 0) {
        fprintf(stderr, "落盘失败：%s\n", seqdir);
        free(wins);
        return 4;
    }

    printf("== 序列 %s（%d 个窗口，主体 %d 个顶点，面积 %.6f）\n",
           seq->name, nw, base.count, base_area);

    CPoly cur = base;
    CPoly eff;
    eff.count = wins[0].n;
    for (int i = 0; i < wins[0].n; ++i) eff.pts[i] = wins[0].pts[i];

    double total_ms = 0.0, final_eff = 0.0, final_direct = 0.0, final_area = 0.0;
    double final_rel_seq = 0.0, final_rel_eff = 0.0;

    for (int k = 0; k < nw; ++k) {
        int step = k + 1;
        double t0 = now_ms();
        /* 已裁空的区域继续参与账目：面积恒为 0，不算错误。 */
        CPoly nxt = cur;
        int rc = 0;
        if (cur.count >= 3) {
            rc = clip_convex(cur.pts, cur.count, wins[k].pts, wins[k].n, &nxt);
        } else {
            nxt.count = 0;
        }
        if (k > 0) {
            CPoly neff = eff;
            int rc2 = 0;
            if (eff.count >= 3) {
                rc2 = clip_convex(eff.pts, eff.count, wins[k].pts, wins[k].n, &neff);
            } else {
                neff.count = 0;
            }
            if (rc == 0) rc = rc2;
            if (rc2 == 0) eff = neff;
        }
        double t1 = now_ms();
        double clip_ms = t1 - t0;

        char stepbuf[32];
        snprintf(stepbuf, sizeof stepbuf, "step%02d", step);
        if (join_path(pbuf, sizeof pbuf, seqdir, "/steps/", stepbuf, "_poly.txt") != 0 ||
            join_path(abuf, sizeof abuf, seqdir, "/steps/", stepbuf, "_area.txt") != 0) {
            fprintf(stderr, "落盘失败：序列 %s 第 %d 步路径过长\n", seq->name, step);
            free(wins);
            return 4;
        }

        if (rc != 0) {
            /* 合法凸窗口下不应发生，若发生就是账目本身中断，指到这一步。 */
            fprintf(stderr,
                    "账目对不上：序列 %s 第 %d 步（窗口号 %d）裁剪失败（返回 %d）\n",
                    seq->name, step, wins[k].id, rc);
            fprintf(rpt, "%s\t%d\t%d\t%d\t-\t%.12f\t-\t-\t-\t-\t-\t%.3f\t%.3f\t%s\n",
                    seq->name, step, wins[k].id, wins[k].n,
                    fabs(signed_area(cur.pts, cur.count)), clip_ms, 0.0, "CLIP_FAILED");
            fprintf(sum, "%s\t%d\t-\t-\t-\t-\t-\t%.3f\tFAILED\n",
                    seq->name, nw, total_ms);
            free(wins);
            return 3;
        }
        cur = nxt;
        double subj_area = fabs(clip_area(&cur));
        if (write_pair(pbuf, abuf, cur.pts, cur.count, subj_area) != 0) {
            fprintf(stderr, "落盘失败：%s\n", pbuf);
            free(wins);
            return 4;
        }

        double t2 = now_ms();
        double eff_area = fabs(clip_area(&eff));

        CPoint *dpoly = NULL;
        int dn = 0;
        double direct_area = 0.0;
        if (direct_region(wins, step, &dpoly, &dn, &direct_area) != 0) {
            fprintf(stderr, "落盘失败：直接区域计算内存不足\n");
            free(wins);
            return 4;
        }

        CPoly oneshot;
        int ons = 0;
        double oneshot_area = 0.0;
        oneshot.count = 0;
        if (eff.count >= 3) {
            if (clip_convex(base.pts, base.count, eff.pts, eff.count, &oneshot) == 0) {
                ons = oneshot.count;
                oneshot_area = fabs(clip_area(&oneshot));
            }
        }
        double t3 = now_ms();
        double check_ms = t3 - t2;

        if (join_path(pbuf, sizeof pbuf, seqdir, "/eff/", stepbuf, "_poly.txt") != 0 ||
            join_path(abuf, sizeof abuf, seqdir, "/eff/", stepbuf, "_area.txt") != 0 ||
            write_pair(pbuf, abuf, eff.pts, eff.count, eff_area) != 0) {
            free(dpoly); free(wins);
            fprintf(stderr, "落盘失败：%s\n", pbuf);
            return 4;
        }
        if (join_path(pbuf, sizeof pbuf, seqdir, "/direct/", stepbuf, "_poly.txt") != 0 ||
            join_path(abuf, sizeof abuf, seqdir, "/direct/", stepbuf, "_area.txt") != 0 ||
            write_pair(pbuf, abuf, dpoly, dn, direct_area) != 0) {
            free(dpoly); free(wins);
            fprintf(stderr, "落盘失败：%s\n", pbuf);
            return 4;
        }
        if (join_path(pbuf, sizeof pbuf, seqdir, "/direct/", stepbuf, "_oneshot_poly.txt") != 0 ||
            join_path(abuf, sizeof abuf, seqdir, "/direct/", stepbuf, "_oneshot_area.txt") != 0 ||
            write_pair(pbuf, abuf, oneshot.pts, ons, oneshot_area) != 0) {
            free(dpoly); free(wins);
            fprintf(stderr, "落盘失败：%s\n", pbuf);
            return 4;
        }
        free(dpoly);

        double rel_seq = rel_diff(subj_area, oneshot_area);
        double rel_eff = rel_diff(eff_area, direct_area);
        total_ms += clip_ms + check_ms;
        final_eff = eff_area;
        final_direct = direct_area;
        final_area = subj_area;
        final_rel_seq = rel_seq;
        final_rel_eff = rel_eff;

        const char *status = "OK";
        int bad = (rel_seq > TOL_REL || rel_eff > TOL_REL);
        if (bad) status = rel_seq > TOL_REL ? "MISMATCH_SEQ_VS_ONESHOT"
                                            : "MISMATCH_EFF_VS_DIRECT";
        fprintf(rpt,
                "%s\t%d\t%d\t%d\t%d\t%.12f\t%.12f\t%.12f\t%.12f\t%.3e\t%.3e\t%.3f\t%.3f\t%s\n",
                seq->name, step, wins[k].id, wins[k].n, cur.count,
                subj_area, eff_area, direct_area, oneshot_area,
                rel_seq, rel_eff, clip_ms, check_ms, status);
        printf("  第 %d 步 窗口 %d: 主体面积 %.6f，有效窗口 %.6f，直接区域 %.6f，"
               "一次裁 %.6f，本步裁剪 %.3f ms，账目核算 %.3f ms\n",
               step, wins[k].id, subj_area, eff_area, direct_area, oneshot_area,
               clip_ms, check_ms);

        if (bad) {
            if (rel_seq > TOL_REL) {
                fprintf(stderr,
                        "账目对不上：序列 %s 第 %d 步（窗口号 %d）连续裁剪面积 %.12f "
                        "与一次裁面积 %.12f 相对偏差 %.3e，超过 %.0e\n",
                        seq->name, step, wins[k].id, subj_area, oneshot_area,
                        rel_seq, TOL_REL);
            } else {
                fprintf(stderr,
                        "账目对不上：序列 %s 第 %d 步（窗口号 %d）反推有效区域面积 %.12f "
                        "与直接裁剪区域面积 %.12f 相对偏差 %.3e，超过 %.0e\n",
                        seq->name, step, wins[k].id, eff_area, direct_area,
                        rel_eff, TOL_REL);
            }
            fprintf(sum, "%s\t%d\t%.12f\t%.12f\t%.12f\t%.3e\t%.3e\t%.3f\tFAILED\n",
                    seq->name, nw, subj_area, eff_area, direct_area,
                    rel_eff, rel_seq, total_ms);
            free(wins);
            return 3;
        }
    }

    fprintf(sum, "%s\t%d\t%.12f\t%.12f\t%.12f\t%.3e\t%.3e\t%.3f\tOK\n",
            seq->name, nw, final_area, final_eff, final_direct,
            final_rel_eff, final_rel_seq, total_ms);
    printf("  反推有效区域面积 %.6f，直接裁剪区域面积 %.6f，相对偏差 %.3e（%.6f ppm）\n",
           final_eff, final_direct, final_rel_eff, final_rel_eff * 1e6);
    printf("  最终主体面积 %.6f，总耗时 %.3f ms\n", final_area, total_ms);
    free(wins);
    return 0;
}

static int load_plan(const char *plan, SeqEntry *seqs, int cap, int *out_n,
                     char *err, size_t errsz) {
    FILE *f = fopen(plan, "r");
    if (!f) { errmsg(err, errsz, "无法打开 plan %s", plan); return -1; }
    int n = 0, lineno = 0;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        ++lineno;
        char *s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '\0' || *s == '\n' || *s == '\r' || *s == '#') continue;
        SeqEntry e;
        if (sscanf(s, "%127s %511s %511s", e.name, e.poly, e.manifest) != 3) {
            snprintf(err, errsz, "plan 第 %d 行格式错误（需要 <序列名> <多边形> <清单>）", lineno);
            fclose(f);
            return -1;
        }
        if (n >= cap) { snprintf(err, errsz, "序列数超过上限 %d", cap); fclose(f); return -1; }
        seqs[n++] = e;
    }
    fclose(f);
    if (n == 0) { snprintf(err, errsz, "plan 里没有任何序列"); return -1; }
    *out_n = n;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4 || strcmp(argv[1], "plan") != 0) {
        fprintf(stderr, "用法: %s plan <plan.txt> <outdir>\n", argv[0]);
        return 2;
    }
    const char *plan_path = argv[2];
    const char *outroot = argv[3];
    char err[ERR_MAX] = {0};

    SeqEntry seqs[MAX_SEQS];
    int ns = 0;
    if (load_plan(plan_path, seqs, MAX_SEQS, &ns, err, sizeof err) != 0) {
        fprintf(stderr, "拒绝：%s\n", err);
        return 2;
    }
    if (mkdir_p(outroot) != 0) { fprintf(stderr, "落盘失败：无法创建 %s\n", outroot); return 4; }

    char path[PATH_MAX_LEN];
    if (join_path(path, sizeof path, outroot, "/report.tsv", "", "") != 0) {
        fprintf(stderr, "落盘失败：输出路径过长\n");
        return 4;
    }
    FILE *rpt = fopen(path, "w");
    if (!rpt) { fprintf(stderr, "落盘失败：无法写 %s\n", path); return 4; }
    if (join_path(path, sizeof path, outroot, "/summary.tsv", "", "") != 0) {
        fclose(rpt);
        fprintf(stderr, "落盘失败：输出路径过长\n");
        return 4;
    }
    FILE *sum = fopen(path, "w");
    if (!sum) { fclose(rpt); fprintf(stderr, "落盘失败：无法写 %s\n", path); return 4; }

    fprintf(rpt, "seq\tstep\twindow_id\twindow_verts\tsubject_verts\tsubject_area\t"
                 "effective_area\tdirect_area\toneshot_area\trel_seq_vs_oneshot\t"
                 "rel_eff_vs_direct\tclip_ms\tcheck_ms\tstatus\n");
    fprintf(sum, "seq\twindows\tfinal_subject_area\tback_derived_eff_area\t"
                 "direct_eff_area\trel_eff_vs_direct\trel_final_vs_oneshot\ttotal_ms\tstatus\n");

    int rc = 0;
    for (int i = 0; i < ns; ++i) {
        rc = run_sequence(plan_path, outroot, &seqs[i], rpt, sum);
        if (rc != 0) break;
    }
    fclose(rpt);
    fclose(sum);
    if (rc == 0) {
        printf("全部 %d 组序列对账通过：每步面积、反推区域与直接区域均已落盘。\n", ns);
    }
    return rc;
}
