#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/snap.h"
#include "src/export.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char tag[16];
    scene_init(s);
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
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -3; }
    }
    fclose(f);
    return s->npolys > 0 ? 0 : -2;
}

static int opt_d(int argc, char **argv, const char *name, double *out) {
    size_t len = strlen(name);
    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], name, len) && argv[i][len] == '=') {
            *out = atof(argv[i] + len + 1);
            return 1;
        }
    return 0;
}

static int opt_i(int argc, char **argv, const char *name, int *out) {
    double v;
    if (!opt_d(argc, argv, name, &v)) return 0;
    *out = (int)v;
    return 1;
}

static int opt_flag(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], name)) return 1;
    return 0;
}

static const char *input_kind_name(int k) {
    return k == 1 ? "point" : (k == 2 ? "collinear" : "general");
}

int main(int argc, char **argv) {
    const char *pos[5] = {0, 0, 0, 0, 0};
    int npos = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;
        if (npos < 5) pos[npos] = argv[i];
        ++npos;
    }
    if (npos < 1) {
        fprintf(stderr,
            "usage: %s scene.txt [out.pgm] [w h scale] [options]\n"
            "  (positional form `scene out.pgm w h scale` 与旧版一致)\n"
            "  --snap          启用顶点吸附；默认关闭，关时输出与浮点管线逐字节相同\n"
            "  --step=X        固定步长（场景单位），跳过反解\n"
            "  --tol=X         面积相对误差预算，默认 1e-3\n"
            "  --ox= --oy=     网格锚点，默认取场景包围盒最小角\n"
            "  --maxm=N        反解阶梯最大倍数，默认 64\n"
            "  --ladder        打印步长-误差对照表（含被拒步长）\n"
            "  --w= --h= --scale=  与位置参数等价，便于混用\n",
            argv[0]);
        return 1;
    }
    int w = 96, h = 96, max_m = 64, ladder = 0;
    double scale = 1.0, step = -1.0, ox = 0.0, oy = 0.0, tol = 1e-3;
    if (pos[2]) w = atoi(pos[2]);
    if (pos[3]) h = atoi(pos[3]);
    if (pos[4]) scale = atof(pos[4]);
    opt_i(argc, argv, "--w", &w);
    opt_i(argc, argv, "--h", &h);
    opt_i(argc, argv, "--maxm", &max_m);
    opt_d(argc, argv, "--scale", &scale);
    opt_d(argc, argv, "--step", &step);
    opt_d(argc, argv, "--tol", &tol);
    int have_ox = opt_d(argc, argv, "--ox", &ox);
    int have_oy = opt_d(argc, argv, "--oy", &oy);
    int snap = opt_flag(argc, argv, "--snap");
    ladder = opt_flag(argc, argv, "--ladder");
    const char *out_path = pos[1];
    if (w <= 0 || h <= 0) { fprintf(stderr, "bad size\n"); return 1; }

    RScene s;
    if (load_scene(pos[0], &s) != 0) { fprintf(stderr, "bad scene\n"); return 2; }

    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) {
        fprintf(stderr, "scene bbox is empty in at least one axis; nothing to raster\n");
        return 4;
    }

    /* 旧路径：纯浮点，不碰任何吸附代码。 */
    if (!snap) {
        unsigned char *buf = malloc((size_t)w * (size_t)h);
        if (!buf) return 5;
        RPlace place = {0.0, 0.0, 0.0, scale};
        if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
        if (out_path && export_pgm(out_path, buf, w, h) != 0) { free(buf); return 7; }
        printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s),
               export_count_on(buf, w, h));
        free(buf);
        return 0;
    }

    /* 吸附路径：先放置，再在放置后的坐标上按输出像素计网格。 */
    RScene placed;
    RPlace place = {0.0, 0.0, 0.0, scale};
    if (pipeline_place(&s, place, &placed) != 0) return 6;
    RBox pwin;
    if (scene_bbox(&placed, &pwin) != 0) return 4;
    double px = (pwin.maxx - pwin.minx) / (double)w;
    double py = (pwin.maxy - pwin.miny) / (double)h;
    if (!(px > 0.0 && py > 0.0)) { fprintf(stderr, "zero pitch\n"); return 5; }
    if (!have_ox) ox = pwin.minx;
    if (!have_oy) oy = pwin.miny;

    SnapSolve sol;
    RScene snapped;
    double t0 = now_ms();
    if (step > 0.0) {
        SnapEval ev;
        int rc = snap_eval(&placed, step, step, ox, oy, 1, 1, &ev, &snapped);
        if (rc < 0) return 6;
        memset(&sol, 0, sizeof(sol));
        sol.chosen = ev;
        sol.rc = ev.first_bad >= 0 ? 2 : (ev.err <= tol ? 0 : 1);
        sol.evaluated = 1;
        if (ev.first_bad >= 0) {
            sol.have_rejected = 1;
            sol.rejected_hx = ev.hx;
            sol.rejected_hy = ev.hy;
            sol.rejected_poly = ev.first_bad;
            sol.rejected_status = ev.status[ev.first_bad];
        }
    } else if (snap_solve(&placed, px, py, ox, oy, tol, max_m, &sol, &snapped) < 0) {
        return 6;
    }
    double t1 = now_ms();

    unsigned char *buf = malloc((size_t)w * (size_t)h);
    if (!buf) return 5;
    double t2 = now_ms();
    if (raster_fill(&snapped, &pwin, w, h, 4, buf) != 0) { free(buf); return 6; }
    double t3 = now_ms();
    if (out_path && export_pgm(out_path, buf, w, h) != 0) { free(buf); return 7; }

    const SnapEval *e = &sol.chosen;
    if (sol.rc == 2) {
        printf("snap rejected no-step-applies eval=%d time_ms=%.3f "
               "reason=every candidate step collapses some polygon\n",
               sol.evaluated, t1 - t0);
    } else {
        const char *verdict = sol.rc == 0 ? "ok" : "over-budget";
        printf("snap %s step_x=%.9g step_y=%.9g kind=%s m=%d "
               "pitch_x=%.9g pitch_y=%.9g "
               "area0=%.6f area1=%.6f relerr=%.3e worst=%.3e@%d "
               "eval=%d time_ms=%.3f on=%d\n",
               verdict, e->hx, e->hy,
               e->coarse ? "pitch*m" : "pitch/m", e->m, e->px, e->py,
               e->area0, e->area1, e->err, e->worst_err, e->worst_poly,
               sol.evaluated, t1 - t0, export_count_on(buf, w, h));
    }
    printf("timing solve_ms=%.3f raster_ms=%.3f total_ms=%.3f\n",
           t1 - t0, t3 - t2, t3 - t0);
    if (sol.rc == 1) printf("note over-budget: no step met tol=%.3e; returned minimum-error candidate\n", tol);
    if (sol.have_rejected)
        printf("reject step_x=%.9g step_y=%.9g poly=%d reason=%s (%s)\n",
               sol.rejected_hx, sol.rejected_hy, sol.rejected_poly,
               snap_status_name(sol.rejected_status),
               step > 0.0 ? "step given by --step; rejected, image not trusted"
                          : "lattice too coarse; every collapsing candidate was skipped");
    for (int k = 0; k < e->npolys; ++k) {
        int st = e->status[k];
        if (st == SNAP_OK) continue;
        if (st == SNAP_INPUT_DEGEN)
            printf("note poly=%d status=input-degenerate kind=%s "
                   "(reported separately, excluded from area error)\n",
                   k, input_kind_name(e->input_kind[k]));
        else
            printf("note poly=%d status=%s kind=%s\n", k, snap_status_name(st),
                   input_kind_name(e->input_kind[k]));
    }
    for (int k = 0; k < e->npolys; ++k) {
        int st = e->status[k];
        if (st == SNAP_DEG_POINT || (st == SNAP_INPUT_DEGEN && e->input_kind[k] == 1))
            printf("point-like poly=%d %s\n", k,
                   st == SNAP_INPUT_DEGEN ? "(input already a point)"
                                          : "(collapsed to a point by chosen step)");
    }
    if (sol.all_input_degenerate)
        printf("note scene all-input-degenerate: snapped at h=pitch, no area error defined\n");
    for (int k = 0; k < e->npolys; ++k) {
        const SnapCollapse *c = &sol.collapse[k];
        if (!c->count) continue;
        printf("collapse poly=%d steps_x=[%.9g,%.9g] steps_y=[%.9g,%.9g] "
               "samples=%d finest_reason=%s\n", k,
               c->min_hx, c->max_hx, c->min_hy, c->max_hy,
               c->count, snap_status_name(c->finest_status));
        if (c->point_count)
            printf("degenerate-point poly=%d steps_x=[%.9g,%.9g] steps_y=[%.9g,%.9g] "
                   "samples=%d (collapses to a single point at these coarse steps)\n",
                   k, c->point_min_hx, c->point_max_hx,
                   c->point_min_hy, c->point_max_hy, c->point_count);
    }

    if (ladder) {
        SnapLadder lad;
        if (snap_ladder(&placed, px, py, ox, oy, max_m, &lad) == 0) {
            printf("ladder rows=%d\n", lad.n);
            for (int i = 0; i < lad.n; ++i) {
                const SnapEval *r = &lad.rows[i];
                char tail[128] = "";
                if (r->first_bad >= 0)
                    snprintf(tail, sizeof(tail), " reject=%s@%d",
                             snap_status_name(r->status[r->first_bad]), r->first_bad);
                printf("  ladder %s m=%d step_x=%.9g step_y=%.9g relerr=%.6e%s\n",
                       r->coarse ? "pitch*" : "pitch/", r->m, r->hx, r->hy,
                       r->first_bad >= 0 ? 0.0 : r->err, tail);
            }
        }
    }

    free(buf);
    return sol.rc == 2 ? 8 : 0;
}
