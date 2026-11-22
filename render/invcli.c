#include "src/scene.h"
#include "src/invsolve.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>

/* 反解缩放命令行；旧固定缩放入口 cli.c 保持不动。
 * 用法：invcli scene.txt out.pgm w h target_density [scalemax]
 * 省略 scalemax 时仅受窗口边界约束。 */
static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -2;
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
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -3; }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s scene.txt out.pgm w h target_density [scalemax]\n",
                argv[0]);
        return 1;
    }
    int code = 1;
    RScene s;
    unsigned char *buf = NULL, *fullbuf = NULL;
    int rc = load_scene(argv[1], &s);
    if (rc != 0) { fprintf(stderr, "load scene failed\n"); return -rc; }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double target = atof(argv[5]);
    double cap = argc >= 7 ? atof(argv[6]) : 0.0;

    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    buf = (unsigned char*)malloc((size_t)w * (size_t)h);
    fullbuf = (unsigned char*)malloc((size_t)(w + 8) * (h + 8));
    if (!buf || !fullbuf) { code = 5; goto done; }

    InvResult r;
    rc = inverse_solve(&s, &win, w, h, target, cap, &r, buf, fullbuf);
    if (rc == -7) {
        fprintf(stderr, "REJECT degenerate: total area is zero\n");
        code = 7; goto done;
    }
    if (rc == -8) {
        fprintf(stderr,
                "REJECT target density %.6f exceeds scale upper bound %.17g\n",
                target, r.scale_max);
        printf("scale_max=%.17g edge_lost=N/A\n", r.scale_max);
        code = 8; goto done;
    }
    if (rc != 0) { fprintf(stderr, "inverse_solve failed: %d\n", rc); code = 6; goto done; }

    if (export_pgm(argv[2], buf, w, h) != 0) { code = 7; goto done; }
    printf("degenerate=%d degenerate_polys=%d area=%.6f\n",
           r.degenerate, r.degenerate_polys, r.area);
    printf("scale=%.17g scale_max=%.17g on=%ld density=%.6f target=%.6f\n",
           r.scale, r.scale_max, r.on_pixels, r.density, target);
    printf("edge_lost=%ld iters=%d elapsed_ms=%.3f\n",
           r.edge_lost, r.iters, r.elapsed_ms);
    if (r.edge_lost != 0) {
        fprintf(stderr, "EDGE LOSS detected: %ld pixels\n", r.edge_lost);
        code = 9; goto done;
    }
    code = 0;
done:
    free(buf);
    free(fullbuf);
    return code;
}
