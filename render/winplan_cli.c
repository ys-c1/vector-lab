/*
 * 贪心窗口规划驱动：
 *   g1..g4 四组场景各报窗口数、覆盖率、重叠率、耗时；
 *   g5 故意把重叠上限压到 0.30，必须拒绝并说明；
 *   g6 窗口足够大时全覆盖即停，且与旧整幅渲染逐字节相同；
 *   g7 固定窗口数对照（旧行为）与贪心对照。
 *
 * 用法：
 *   winplan_cli run <scene.txt> <out.pgm> <w> <h> [win_w win_h stride
 *                     target overlap_cap max_wins]
 *   winplan_cli legacy <scene.txt> <out.pgm> <w> <h> <scale>
 *
 * run 的默认参数为 40x40 / 步长 8 / 目标 0.98 / 重叠上限 0.60 / 256 个窗口。
 * 退出码：0 目标达成（FULL 或 TARGET）；3 重叠上限拒绝；4 目标未达；
 * 2 参数或 IO 错误。
 */
#define _POSIX_C_SOURCE 200809L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/winplan.h"
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

/* 旧路径：整幅包围盒单窗口，与 render/cli.c 完全相同的调用。 */
static int legacy_render(const RScene *s, RPlace place, const char *out_path,
                         int w, int h, int *on_px, double *ms) {
    RBox win;
    if (scene_bbox(s, &win) != 0) return -4;
    unsigned char *buf = (unsigned char *)malloc((size_t)w * h);
    if (!buf) return -5;
    double t0 = now_ms();
    if (pipeline_render(s, place, win, w, h, buf) != 0) { free(buf); return -6; }
    *ms = now_ms() - t0;
    if (export_pgm(out_path, buf, w, h) != 0) { free(buf); return -7; }
    *on_px = export_count_on(buf, w, h);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage:\n"
                "  %s run <scene.txt> <out.pgm> <w> <h> [win_w win_h stride"
                " target overlap_cap max_wins]\n"
                "  %s legacy <scene.txt> <out.pgm> <w> <h> <scale>\n",
                argv[0], argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "legacy") == 0) {
        if (argc < 7) { fprintf(stderr, "legacy needs scene out w h scale\n"); return 2; }
        RScene s;
        if (load_scene(argv[2], &s) != 0) return 2;
        int w = atoi(argv[4]), h = atoi(argv[5]);
        RPlace place = {0, 0, 0, atof(argv[6])};
        int on = 0; double ms = 0;
        int rc = legacy_render(&s, place, argv[3], w, h, &on, &ms);
        if (rc != 0) { fprintf(stderr, "legacy render failed (%d)\n", rc); return 2; }
        printf("legacy polys %d on %d ms %.3f\n", s.npolys, on, ms);
        return 0;
    }
    if (strcmp(argv[1], "run") != 0) { fprintf(stderr, "bad command\n"); return 2; }
    if (argc < 6) { fprintf(stderr, "run needs scene out w h\n"); return 2; }

    RScene s;
    if (load_scene(argv[2], &s) != 0) return 2;
    int w = atoi(argv[4]), h = atoi(argv[5]);
    if (w <= 0 || h <= 0) {
        fprintf(stderr, "reject: image %dx%d, width and height must be positive\n", w, h);
        return 2;
    }
    RWinPlanOpt o;
    winplan_default_opt(&o);
    double scale = 1.0;
    if (argc >= 7 && argv[6][0] != '-') o.win_w = atoi(argv[6]);
    if (argc >= 8 && argv[7][0] != '-') o.win_h = atoi(argv[7]);
    if (argc >= 9 && argv[8][0] != '-') o.stride = atoi(argv[8]);
    if (argc >= 10 && argv[9][0] != '-') o.target = atof(argv[9]);
    if (argc >= 11 && argv[10][0] != '-') o.overlap_cap = atof(argv[10]);
    if (argc >= 12 && argv[11][0] != '-') o.max_wins = atoi(argv[11]);
    for (int i = 6; i < argc; ++i) {
        if (strncmp(argv[i], "--fixed=", 8) == 0) o.fixed_count = atoi(argv[i] + 8);
        if (strncmp(argv[i], "--scale=", 8) == 0) scale = atof(argv[i] + 8);
    }

    if (winplan_candidate_count(w, h, &o) < 0) {
        fprintf(stderr,
                "reject: bad plan params (win %dx%d stride %d target %.4f "
                "cap %.4f max %d) for image %dx%d\n",
                o.win_w, o.win_h, o.stride, o.target, o.overlap_cap,
                o.max_wins, w, h);
        return 2;
    }

    RBox win;
    RPlace place = {0, 0, 0, scale};
    const RBox *bbox = NULL;
    if (scene_bbox(&s, &win) == 0) bbox = &win;
    if (scale == 0.0) {
        fprintf(stderr, "reject: scale must be nonzero, got 0\n");
        return 2;
    }

    /* 图形像素 = 整幅渲染的非零像素：规划与判分用同一口径。 */
    unsigned char *full = (unsigned char *)malloc((size_t)w * h);
    if (!full) return 2;
    double t_full0 = now_ms();
    int rc = pipeline_render(&s, place, *bbox, w, h, full);
    if (rc != 0) { free(full); fprintf(stderr, "render failed (%d)\n", rc); return 2; }
    double full_ms = now_ms() - t_full0;
    int on_px = export_count_on(full, w, h);

    RWinPlan plan;
    double t_plan0 = now_ms();
    int prc = winplan_plan(full, w, h, &o, &plan);
    double plan_ms = now_ms() - t_plan0;

    unsigned char *buf = (unsigned char *)malloc((size_t)w * h);
    if (!buf) { free(full); return 2; }
    double t_r0 = now_ms();
    int rrc = winplan_render(&s, place, *bbox, &plan, w, h, buf);
    double render_ms = now_ms() - t_r0;
    if (rrc != 0) { free(full); free(buf); fprintf(stderr, "winplan_render failed (%d)\n", rrc); return 2; }
    if (export_pgm(argv[3], buf, w, h) != 0) { free(full); free(buf); return 2; }

    /* 渲染结果必须与规划口径一致：点亮的像素是图形像素的子集，且个数等于
     * 规划里记录的 covered（窗口外没画任何东西）。 */
    int rendered_on = export_count_on(buf, w, h);
    int subset = 1;
    for (size_t i = 0; i < (size_t)w * h; ++i)
        if (buf[i] && !full[i]) { subset = 0; break; }
    int pixel_ok = subset && rendered_on == plan.covered;

    printf("scene polys=%d verts=%d image=%dx%d on_pixels=%d\n",
           s.npolys, scene_count_verts(&s), w, h, on_px);
    printf("plan win=%dx%d stride=%d target=%.4f overlap_cap=%.4f max_wins=%d\n",
           o.win_w, o.win_h, o.stride, o.target, o.overlap_cap, o.max_wins);
    printf("plan stop=%s nwins=%d covered=%d/%d coverage=%.6f overlap=%.6f\n",
           winplan_stop_str(plan.stop), plan.nwins, plan.covered, plan.on_pixels,
           plan.coverage, plan.overlap);
    printf("plan sum_area=%lld union_area=%lld\n", plan.sum_area, plan.union_area);
    for (int k = 0; k < plan.nwins; ++k)
        printf("win %d %d %d %d %d\n", k + 1, plan.wins[k].x, plan.wins[k].y,
               plan.wins[k].w, plan.wins[k].h);
    for (int k = 0; k < plan.nrejects; ++k)
        printf("reject #%d after=%d at (%d,%d) gain=%d overlap_would_be=%.6f "
               "(cap %.4f)\n",
               k + 1, plan.rejects[k].after_wins, plan.rejects[k].x,
               plan.rejects[k].y, plan.rejects[k].gain,
               plan.rejects[k].overlap, o.overlap_cap);
    if (plan.nrejects > WINPLAN_MAX_REJECTS)
        printf("reject detail truncated: %d rejected, first %d listed\n",
               plan.nrejects, WINPLAN_MAX_REJECTS);
    printf("timing full_ms=%.3f plan_ms=%.3f render_ms=%.3f total_ms=%.3f\n",
           full_ms, plan_ms, render_ms, full_ms + plan_ms + render_ms);
    printf("pixel_match=%s rendered_on=%d\n", pixel_ok ? "yes" : "no", rendered_on);

    /* 已完成规划但覆盖率仍低于目标：如实报告失败（例如 MAXWIN 兜底）。 */
    int under_target = plan.coverage + 1e-12 < o.target;
    if (under_target && prc == WINPLAN_OK)
        printf("target_met=no coverage=%.6f < target=%.4f\n",
               plan.coverage, o.target);
    else
        printf("target_met=yes coverage=%.6f >= target=%.4f\n",
               plan.coverage, o.target);

    free(full);
    free(buf);
    if (prc == WINPLAN_ERR_OVERLAP) return 3;
    if (prc == WINPLAN_ERR_TARGET) return 4;
    if (prc != WINPLAN_OK) return 2;
    if (under_target) return 4;
    return 0;
}
