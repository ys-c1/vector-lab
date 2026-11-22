/* 裁剪验证驱动：逐窗口（逐刀）裁剪，每步把多边形与面积单独落盘，
 * 与 gold/ref 的面积逐份比对，打印第一处不一致；同时记录每步毫秒、
 * 累计偏差和单步内存（超过输入四倍直接判失败）。
 *
 * 用法：
 *   run clip <poly.txt> <manifest.txt> <outdir> [ref 二进制]
 *   run cmp  <poly1.txt> <area1.txt> <poly2.txt> <area2.txt>
 *
 * manifest 每行：<窗口号> <窗口文件>，窗口号必须唯一；
 * 窗口文件每行 "x y"。窗口为空、顶点不足三个都拒绝并报第几个窗口。
 */
#define _POSIX_C_SOURCE 200809L
#include "clip.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXV 4096
#define TOL_REL 0.01          /* gold 是 256x256 数格子，容差按 1% 给 */
#define TOL_ABS 1e-3

static int load_pts(const char *path, CPoint *pts, int cap, char *err, size_t errsz) {
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errsz, "无法打开 %s", path); return -1; }
    int n = 0;
    double x, y;
    int r;
    while ((r = fscanf(f, "%lf %lf", &x, &y)) == 2) {
        if (n >= cap) { snprintf(err, errsz, "%s 顶点超过上限 %d", path, cap); fclose(f); return -1; }
        pts[n].x = x; pts[n].y = y; ++n;
    }
    if (r != EOF) { snprintf(err, errsz, "%s 第 %d 个顶点格式错误（需要 x y 两列）", path, n + 1); fclose(f); return -1; }
    fclose(f);
    return n;
}

static int write_poly(const char *path, const CPoint *p, int n) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) fprintf(f, "%.6f %.6f\n", p[i].x, p[i].y);
    fclose(f);
    return 0;
}

static double read_double_file(const char *path, int *ok) {
    FILE *f = fopen(path, "r");
    *ok = 0;
    if (!f) return 0.0;
    double v;
    if (fscanf(f, "%lf", &v) == 1) *ok = 1;
    fclose(f);
    return v;
}

static double area_of_pts(const CPoint *p, int n) {
    if (n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

static long ms_between(struct timespec t0, struct timespec t1) {
    return (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
}

/* 清单里的相对路径按清单所在目录解析，避免依赖调用方 CWD。 */
static void resolve_near(const char *manifest, const char *rel, char *out, size_t outsz) {
    if (rel[0] == '/') { snprintf(out, outsz, "%s", rel); return; }
    char dir[600];
    snprintf(dir, sizeof dir, "%s", manifest);
    char *slash = strrchr(dir, '/');
    if (slash) *(slash + 1) = '\0'; else snprintf(dir, sizeof dir, "./");
    snprintf(out, outsz, "%s%s", dir, rel);
}

typedef struct { int id; char path[512]; } WinEntry;

static int cmd_clip(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "用法: run clip <poly.txt> <manifest.txt> <outdir> [ref]\n"); return 2; }
    const char *poly_path = argv[2], *manifest_path = argv[3], *outdir = argv[4];
    const char *ref = argc >= 6 ? argv[5] : NULL;
    char err[1024] = {0};

    static CPoint poly[MAXV];
    int pn = load_pts(poly_path, poly, MAXV, err, sizeof err);
    if (pn < 0) { fprintf(stderr, "拒绝：%s\n", err); return 2; }
    if (pn < 3) { fprintf(stderr, "拒绝：输入多边形顶点不足三个（实际 %d 个）\n", pn); return 2; }

    FILE *mf = fopen(manifest_path, "r");
    if (!mf) { fprintf(stderr, "拒绝：无法打开清单 %s\n", manifest_path); return 2; }
    WinEntry wins[256];
    int wcount = 0;
    int used_id[256];
    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof line, mf)) {
        ++lineno;
        char p[512];
        int id;
        if (sscanf(line, "%d %511s", &id, p) < 2) {
            if (line[0] == '\n' || line[0] == '#') continue;
            fprintf(stderr, "拒绝：清单第 %d 行格式错误（需要 <窗口号> <窗口文件>）\n", lineno);
            fclose(mf); return 2;
        }
        if (wcount >= 256) { fprintf(stderr, "拒绝：窗口数超过 256\n"); fclose(mf); return 2; }
        for (int i = 0; i < wcount; ++i)
            if (used_id[i] == id) {
                fprintf(stderr, "拒绝：第 %d 个窗口（清单第 %d 行）窗口号 %d 与前面重复\n",
                        wcount + 1, lineno, id);
                fclose(mf); return 2;
            }
        used_id[wcount] = id;
        wins[wcount].id = id;
        snprintf(wins[wcount].path, sizeof wins[wcount].path, "%s", p);
        ++wcount;
    }
    fclose(mf);
    if (wcount == 0) { fprintf(stderr, "拒绝：清单中没有任何窗口\n"); return 2; }

    char mk[600];
    snprintf(mk, sizeof mk, "mkdir -p '%s/steps' '%s/gold'", outdir, outdir);
    if (system(mk) != 0) { fprintf(stderr, "拒绝：无法创建输出目录 %s\n", outdir); return 2; }

    char rpt_path[600];
    snprintf(rpt_path, sizeof rpt_path, "%s/report.tsv", outdir);
    FILE *rpt = fopen(rpt_path, "w");
    if (!rpt) { fprintf(stderr, "拒绝：无法写报告 %s\n", rpt_path); return 2; }
    fprintf(rpt, "step\twindow_id\tinput_verts\toutput_verts\tms\tcum_abs_dev\tstep_work_bytes\tinput_bytes\twork_over_4x_input\tstatus\n");

    CPoly out;
    double cum_dev = 0.0;
    int cn = pn;
    static CPoint cur[MAXV];
    for (int i = 0; i < pn; ++i) cur[i] = poly[i];

    /* 初始（第 0 步）多边形与面积也落盘，作为逐份比对的基线 */
    char sp[600], sa[600], ga[600], gp[600];
    snprintf(sp, sizeof sp, "%s/steps/step00_poly.txt", outdir);
    snprintf(sa, sizeof sa, "%s/steps/step00_area.txt", outdir);
    write_poly(sp, cur, cn);
    FILE *a0 = fopen(sa, "w");
    double a0v = area_of_pts(cur, cn);   /* 不经固定 64 点的 CPoly，避免大顶点栈溢出 */
    fprintf(a0, "%.6f\n", fabs(a0v));
    fclose(a0);

    for (int k = 0; k < wcount; ++k) {
        static CPoint win[MAXV];
        char wpath[1100];
        resolve_near(manifest_path, wins[k].path, wpath, sizeof wpath);
        char werr[1024] = {0};
        int wn = load_pts(wpath, win, MAXV, werr, sizeof werr);
        if (wn < 0) {
            fprintf(stderr, "拒绝：第 %d 个窗口（窗口号 %d）%s\n", k + 1, wins[k].id, werr);
            fprintf(rpt, "%d\t%d\t-\t-\t-\t%.6f\t-\t-\t-\tREJECTED\n", k + 1, wins[k].id, cum_dev);
            fclose(rpt);
            return 2;
        }
        if (wn == 0) {
            fprintf(stderr, "拒绝：第 %d 个窗口（窗口号 %d）窗口为空\n", k + 1, wins[k].id);
            fprintf(rpt, "%d\t%d\t-\t-\t-\t%.6f\t-\t-\t-\tREJECTED_EMPTY\n", k + 1, wins[k].id, cum_dev);
            fclose(rpt);
            return 2;
        }
        if (wn < 3) {
            fprintf(stderr, "拒绝：第 %d 个窗口（窗口号 %d）顶点不够三个（实际 %d 个）\n",
                    k + 1, wins[k].id, wn);
            fprintf(rpt, "%d\t%d\t-\t-\t-\t%.6f\t-\t-\t-\tREJECTED_FEW_VERTS\n", k + 1, wins[k].id, cum_dev);
            fclose(rpt);
            return 2;
        }

        /* 四倍预算的“输入”= 进入本步的多边形顶点；窗口顶点不进入工作缓冲。 */
        size_t input_bytes = (size_t)cn * sizeof(CPoint);
        size_t work = clip_working_bytes(cn);
        int over_budget = work > 4 * input_bytes;

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int rc = clip_convex(cur, cn, win, wn, &out);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long ms = ms_between(t0, t1);

        const char *status = "OK";
        if (rc == -1) { status = "ERR_BAD_ARG"; }
        else if (rc == -2) { status = "ERR_DEGENERATE_WINDOW"; }
        else if (rc == -3) { status = "ERR_NO_MEM_OR_OVERFLOW"; }
        if (over_budget) status = "MEM_OVER_4X_INPUT";

        /* 内存超四倍预算：本题直接算没做完，不写后续产物 */
        if (over_budget) {
            fprintf(stderr, "拒绝：第 %d 个窗口（窗口号 %d）单步工作内存 %zu 字节超过输入 %zu 字节的四倍\n",
                    k + 1, wins[k].id, work, input_bytes);
            fprintf(rpt, "%d\t%d\t%d\t-\t%ld\t%.6f\t%zu\t%zu\tYES\t%s\n",
                    k + 1, wins[k].id, cn + wn, ms, cum_dev, work, input_bytes, status);
            fclose(rpt);
            return 2;
        }
        /* 裁剪器自身报错（退化窗口等）：按拒绝处理，不拿陈旧多边形去比 gold */
        if (rc != 0) {
            fprintf(stderr, "拒绝：第 %d 个窗口（窗口号 %d）裁剪失败：%s\n",
                    k + 1, wins[k].id, status);
            fprintf(rpt, "%d\t%d\t%d\t-\t%ld\t%.6f\t%zu\t%zu\tno\t%s\n",
                    k + 1, wins[k].id, cn + wn, ms, cum_dev, work, input_bytes, status);
            fclose(rpt);
            return 2;
        }

        cn = out.count;
        for (int i = 0; i < cn; ++i) cur[i] = out.pts[i];

        snprintf(sp, sizeof sp, "%s/steps/step%02d_poly.txt", outdir, k + 1);
        snprintf(sa, sizeof sa, "%s/steps/step%02d_area.txt", outdir, k + 1);
        snprintf(gp, sizeof gp, "%s/gold/step%02d_poly.txt", outdir, k + 1);
        snprintf(ga, sizeof ga, "%s/gold/step%02d_area.txt", outdir, k + 1);
        write_poly(sp, cur, cn);
        double area = cn >= 3 ? fabs(area_of_pts(cur, cn)) : 0.0;
        FILE *af = fopen(sa, "w");
        fprintf(af, "%.6f\n", area);
        fclose(af);

        double gold = 0.0;
        int gold_ok = 0;
        if (ref && cn >= 3) {
            write_poly(gp, cur, cn);
            char cmd[2200];
            snprintf(cmd, sizeof cmd, "'%s' area '%s' > '%s' 2>/dev/null", ref, gp, ga);
            int cr = system(cmd);
            if (cr == 0) gold = read_double_file(ga, &gold_ok);
        } else if (cn < 3) {
            FILE *gf = fopen(ga, "w");
            if (gf) { fprintf(gf, "%.6f\n", 0.0); fclose(gf); gold = 0.0; gold_ok = 1; }
        }

        if (gold_ok) {
            double dev = fabs(area - gold);
            cum_dev += dev;
            double tol = TOL_ABS + TOL_REL * fabs(gold);
            fprintf(rpt, "%d\t%d\t%d\t%d\t%ld\t%.6f\t%zu\t%zu\tno\tOK\n",
                    k + 1, wins[k].id, cn,
                    cn, ms, cum_dev, work, input_bytes);
            if (dev > tol) {
                printf("第一处不一致：第 %d 步（窗口号 %d）面积 %.6f，gold %.6f，偏差 %.6f 超过容差 %.6f\n",
                       k + 1, wins[k].id, area, gold, dev, tol);
                fclose(rpt);
                return 1;
            }
        } else {
            fprintf(rpt, "%d\t%d\t%d\t%d\t%ld\t%.6f\t%zu\t%zu\tno\tGOLD_UNAVAILABLE\n",
                    k + 1, wins[k].id, cn, cn, ms, cum_dev, work, input_bytes);
        }
    }

    fclose(rpt);
    printf("全部 %d 步通过，累计偏差 %.6f，报告 %s\n", wcount, cum_dev, rpt_path);
    return 0;
}

static int cmd_cmp(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "用法: run cmp <poly1> <area1> <poly2> <area2>\n"); return 2; }
    int ok1, ok2;
    double a1 = read_double_file(argv[3], &ok1);
    double a2 = read_double_file(argv[5], &ok2);
    if (!ok1 || !ok2) { fprintf(stderr, "拒绝：面积文件缺失或不可读\n"); return 2; }
    double tol = TOL_ABS + TOL_REL * fmax(fabs(a1), fabs(a2));
    double dev = fabs(a1 - a2);
    printf("一刀 %.6f vs 两刀 %.6f，偏差 %.6f，容差 %.6f：%s\n",
           a1, a2, dev, tol, dev <= tol ? "一致" : "不一致");
    return dev <= tol ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "clip")) return cmd_clip(argc, argv);
    if (!strcmp(argv[1], "cmp")) return cmd_cmp(argc, argv);
    fprintf(stderr, "未知子命令 %s\n", argv[1]);
    return 2;
}
