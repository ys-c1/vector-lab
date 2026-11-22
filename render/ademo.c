#define _POSIX_C_SOURCE 200809L
/*
 * 四类图的方差驱动自适应采样演示：
 *   A 轴对方形（窗口=包围盒，全部像素确定性覆盖 -> 零方差退化）
 *   B 旋转方形（斜直边，高方差边界像素）
 *   C 三角形（不同方向的斜边混合）
 *   D 正十六边形近似圆（弯曲边界，最长的边界带）
 * 另对 B 施加极窄目标宽度演示超上限拒绝。
 *
 * 用法：ademo [out_dir]
 */
#include "src/scene.h"
#include "src/adaptive.h"
#include "src/export.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W 96
#define H 96

static void add_square(RScene *s, double cx, double cy, double half, double rot) {
    RPoint pts[4];
    double c = cos(rot), sn = sin(rot);
    for (int i = 0; i < 4; ++i) {
        double gx = (i == 1 || i == 2) ? half : -half;
        double gy = (i >= 2) ? half : -half;
        pts[i].x = cx + gx * c - gy * sn;
        pts[i].y = cy + gx * sn + gy * c;
    }
    scene_add_poly(s, pts, 4);
}

static void add_triangle(RScene *s) {
    RPoint pts[3] = {{20, 20}, {85, 30}, {40, 90}};
    scene_add_poly(s, pts, 3);
}

static void add_disk(RScene *s) {
    RPoint pts[16];
    for (int i = 0; i < 16; ++i) {
        double a = 2.0 * M_PI * i / 16.0;
        pts[i].x = 50 + 35 * cos(a);
        pts[i].y = 50 + 35 * sin(a);
    }
    scene_add_poly(s, pts, 16);
}

static void report_line(const char *tag, const char *name,
                        const RAdaptCfg *cfg, const RAdaptReport *r) {
    if (r->status == RADAPT_E_REJECT) {
        printf("%-4s %-12s REJECT  pilot=%-3d nmax=%-5d var=%.4f conf=%.2f "
               "targetW=%.4f bestW@nmax=%.4f (%5.1f ms)\n",
               tag, name, r->pilot_n, cfg->nmax, r->var_hat, cfg->conf,
               cfg->width, r->width_pred, r->ms);
        return;
    }
    if (r->status != RADAPT_OK) {
        printf("%-4s %-12s ERROR status=%d\n", tag, name, r->status);
        return;
    }
    printf("%-4s %-12s %s n=%-4d var=%.4f predW=%.4f obsW=%.4f targetW=%.4f "
           "edge=%-4d worstp=%.3f %6.2f ms\n",
           tag, name, r->degenerate ? "DEGEN " : "OK    ",
           r->samples, r->var_hat, r->width_pred, r->width_obs,
           cfg->width, r->edge_pixels, r->worst_p, r->ms);
}

int main(int argc, char **argv) {
    const char *outdir = argc > 1 ? argv[1] : "build/out";
    char path[256];
    mkdir(outdir, 0755); /* 已存在时忽略 EEXIST */

    RScene scenes[4];
    for (int i = 0; i < 4; ++i) scene_init(&scenes[i]);
    /* A: 轴对方形，窗口恰为包围盒 [10,90]^2 => 每像素确定性结果 */
    RPoint sq[4] = {{10, 10}, {90, 10}, {90, 90}, {10, 90}};
    scene_add_poly(&scenes[0], sq, 4);
    add_square(&scenes[1], 50, 50, 36, 0.6);
    add_triangle(&scenes[2]);
    add_disk(&scenes[3]);

    RBox wins[4];
    wins[0] = (RBox){10, 10, 90, 90};          /* 紧贴包围盒：零方差 */
    for (int i = 1; i < 4; ++i) wins[i] = (RBox){0, 0, 100, 100};

    const char *names[4] = {"axis-square", "rot-square", "triangle", "disk16"};
    const char *tags[4]  = {"A", "B", "C", "D"};

    unsigned char *buf = (unsigned char*)malloc((size_t)W * H);
    if (!buf) return 5;

    printf("target: conf=0.95 interval full-width <= 0.10 (coverage units), nmax=4096\n");
    int rc = 0;
    for (int i = 0; i < 4; ++i) {
        RAdaptCfg cfg;
        radapt_cfg_default(&cfg);
        RAdaptReport r;
        int st = raster_adaptive(&scenes[i], &wins[i], W, H, &cfg, buf, &r);
        report_line(tags[i], names[i], &cfg, &r);
        if (st == RADAPT_OK) {
            snprintf(path, sizeof(path), "%s/%s.pgm", outdir, names[i]);
            if (export_pgm(path, buf, W, H) != 0) {
                fprintf(stderr, "write %s failed\n", path); rc = 4;
            }
            /* 达标断言：实测宽度必须不宽于目标（退化情形宽度为 0） */
            if (r.width_obs > cfg.width + 1e-12) {
                fprintf(stderr, "WIDTH NOT MET %s obs=%.4f target=%.4f\n",
                        names[i], r.width_obs, cfg.width);
                rc = 6;
            }
        } else {
            rc = 7;
        }
    }

    /* 拒绝路径：B 图，目标宽度压到 0.01，nmax=256，必拒 */
    RAdaptCfg tight;
    radapt_cfg_default(&tight);
    tight.width = 0.01;
    tight.nmax = 256;
    RAdaptReport rr;
    int st = raster_adaptive(&scenes[1], &wins[1], W, H, &tight, buf, &rr);
    report_line("B*", "rot-square-tight", &tight, &rr);
    if (st != RADAPT_E_REJECT || !rr.rejected) {
        fprintf(stderr, "expected rejection not triggered\n");
        rc = 8;
    } else {
        printf("    -> rejected: n needed for W=%.3f exceeds nmax=%d; "
               "best achievable width=%.4f\n", tight.width, tight.nmax, rr.width_pred);
    }

    free(buf);
    if (rc == 0) printf("all interval-width targets met; outputs in %s/\n", outdir);
    return rc;
}
