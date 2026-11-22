/* 窗口旋转反解驱动：读场景 -> 取轴对齐包围盒 -> 生成窗口 -> 解最大角度 ->
 * 按该角度旋转窗口采样 -> 报角度、空角像素、耗时。
 *
 * 用法：
 *   winrot_cli solve <bw> <bh> <win_w> <win_h> [cap_deg]
 *   winrot_cli run   <scene.txt|bw,bh@cx,cy> <win_w> <win_h> <out.pgm|->
 *                    <w> <h> [ss] [cap_deg] [--allow-over-cap]
 *                    [--theta <deg>]
 *
 * run 在解超上限（默认 45 度）时拒绝渲染，退出码 3；包围盒退化成点/线
 * 退出码 4；两种情况都在 stderr 说明原因。--allow-over-cap 仅用于诊断，
 * 不作为报告结果。--theta=<deg> 强制指定角度（诊断用，例如 --theta=0
 * 复现空角，或扫过解出的角度验证边界）。
 * 以 "bw,bh@cx,cy" 形式给的合成包围盒用于单独验证求解器，此时没有图形
 * 可采样，不渲染。
 */
#define _POSIX_C_SOURCE 200809L
#include "src/scene.h"
#include "src/winrot.h"
#include "src/export.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "open scene failed: %s\n", path); return -1; }
    scene_init(s);
    char tag[16];
    while (fscanf(f, "%15s", tag) == 1) {
        if (tag[0] != 'p') break;
        RPoint pts[64];
        int n = 0;
        while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) {
            ++n;
            int c = fgetc(f);
            if (c == '\n' || c == EOF) break;
            ungetc(c, f);
        }
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -2; }
    }
    fclose(f);
    return s->npolys > 0 ? 0 : -3;
}

static int parse_bbox_arg(const char *arg, double *bw, double *bh, double *cx,
                          double *cy) {
    char tail;
    if (sscanf(arg, "%lf,%lf@%lf,%lf%c", bw, bh, cx, cy, &tail) != 4) return -1;
    return 0;
}

static const char *status_meaning(int st) {
    switch (st) {
    case WINROT_DEGEN_POINT: return "bbox is a point";
    case WINROT_DEGEN_LINE: return "bbox is a line";
    case WINROT_NOFIT: return "window larger than bbox at zero rotation";
    case WINROT_OVER_CAP: return "angle exceeds cap";
    default: return "ok";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage:\n"
                "  %s solve <bw> <bh> <win_w> <win_h> [cap_deg]\n"
                "  %s run <scene.txt> <win_w> <win_h> <out.pgm|-> <w> <h>"
                " [ss] [cap_deg] [--allow-over-cap]\n", argv[0], argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "solve") == 0) {
        if (argc < 6) return 1;
        double bw = atof(argv[2]), bh = atof(argv[3]);
        double ww = atof(argv[4]), wh = atof(argv[5]);
        double cap = argc >= 7 ? atof(argv[6]) : 45.0;
        WinRotSolve r;
        int st = winrot_solve(bw, bh, ww, wh, cap, &r);
        printf("status=%s bbox=%.6f x %.6f win=%.6f x %.6f cap=%.6f "
               "theta=%.9f width=%.9f height=%.9f binding=%s\n",
               winrot_status_str(st), bw, bh, ww, wh, cap, r.theta_deg,
               r.theta_width_deg, r.theta_height_deg,
               winrot_binding_str(r.binding));
        return st == WINROT_OK ? 0 : 2;
    }
    if (strcmp(argv[1], "run") != 0) { fprintf(stderr, "bad command\n"); return 1; }

    if (argc < 8) {
        fprintf(stderr, "run needs <scene> <win_w> <win_h> <out> <w> <h>\n");
        return 1;
    }
    double sbw = 0, sbh = 0, scx = 0, scy = 0;
    int synthetic = (parse_bbox_arg(argv[2], &sbw, &sbh, &scx, &scy) == 0);
    const char *scene_path = argv[2];
    double win_w = atof(argv[3]), win_h = atof(argv[4]);
    const char *out_path = argv[5];
    int w = atoi(argv[6]), h = atoi(argv[7]);
    int ss = 4;
    double cap = 45.0;
    if (argc >= 9 && argv[8][0] != '-') ss = atoi(argv[8]);
    if (argc >= 10 && argv[9][0] != '-') cap = atof(argv[9]);
    int allow_over = 0, have_theta = 0;
    double forced_theta = 0.0;
    for (int i = 8; i < argc; ++i) {
        if (strcmp(argv[i], "--allow-over-cap") == 0) allow_over = 1;
        if (strncmp(argv[i], "--theta=", 8) == 0) {
            forced_theta = atof(argv[i] + 8);
            have_theta = 1;
        }
    }

    RScene s;
    scene_init(&s);
    double bw, bh, cx, cy;
    if (synthetic) {
        bw = sbw; bh = sbh; cx = scx; cy = scy;
    } else {
        if (load_scene(scene_path, &s) != 0) return 2;
        RBox bb;
        if (scene_bbox(&s, &bb) != 0) return 2;
        bw = bb.maxx - bb.minx; bh = bb.maxy - bb.miny;
        cx = (bb.minx + bb.maxx) * 0.5; cy = (bb.miny + bb.maxy) * 0.5;
    }

    double t_solve0 = now_ms();
    WinRotSolve r;
    int st = winrot_solve(bw, bh, win_w, win_h, cap, &r);
    double solve_ms = now_ms() - t_solve0;

    printf("scene %s bbox %.6f x %.6f (%.6f,%.6f) win %.6f x %.6f px %dx%d ss %d\n",
           scene_path, bw, bh, cx, cy, win_w, win_h, w, h, ss);
    printf("  status %s (%s) binding %s theta %.9f deg (width %.9f, height %.9f)\n",
           winrot_status_str(st), status_meaning(st),
           winrot_binding_str(r.binding), r.theta_deg,
           r.theta_width_deg, r.theta_height_deg);
    if (have_theta) {
        printf("  forced theta %.9f deg (diagnostic override)\n", forced_theta);
        r.theta_deg = forced_theta;
        st = WINROT_OK;
    }

    if (st != WINROT_OK) {
        if (!(allow_over && st == WINROT_OVER_CAP)) {
            if (st == WINROT_OVER_CAP)
                fprintf(stderr,
                        "REJECT: exceeds angle cap: solved theta %.9f deg > "
                        "cap %.6f deg (binding %s); not rendering\n",
                        r.theta_deg, cap, winrot_binding_str(r.binding));
            else
                fprintf(stderr, "REJECT: %s: %s; not rendering\n",
                        winrot_status_str(st), status_meaning(st));
            return st == WINROT_OVER_CAP ? 3 : 4;
        }
        fprintf(stderr, "WARNING: %s, rendering anyway for diagnosis\n",
                winrot_status_str(st));
    }
    if (synthetic) {
        printf("  synthetic bbox only: solver verified, no geometry to sample\n");
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (!buf) return 5;
    double t_rd0 = now_ms();
    if (winrot_render(&s, cx, cy, win_w, win_h, r.theta_deg, w, h, ss, buf) != 0) {
        free(buf);
        return 6;
    }
    double render_ms = now_ms() - t_rd0;
    int corner = winrot_count_corner_empty(buf, w, h);
    int empty = winrot_count_empty(buf, w, h);
    int k = winrot_corner_box(w, h);

    if (strcmp(out_path, "-") != 0 && export_pgm(out_path, buf, w, h) != 0) {
        free(buf);
        return 7;
    }
    printf("  angle_deg %.6f corner_box %d corner_empty_px %d empty_px %d/%d "
           "solve_ms %.3f render_ms %.3f total_ms %.3f\n",
           r.theta_deg, k, corner, empty, w * h, solve_ms, render_ms,
           solve_ms + render_ms);
    free(buf);
    return st == WINROT_OK ? 0 : 3;
}
