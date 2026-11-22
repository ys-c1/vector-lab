#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 退出码：1 用法；2 打开场景；3 场景非法；4 包围盒；5 内存；
 * 6 渲染失败；7 导出失败；8 缩放为零或负；9 缩放不是有限数。 */
int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [--blend|--hard|--off]\n", argv[0]);
        return 1;
    }
    /* --off 保留旧的整数层硬切（默认即旧路径）；--blend 启用小数层过渡；
     * --hard 走新代码但强制硬切，用来与 --blend 对比跳变。 */
    RZoomOpts zo = {-1, 0, 0};
    int use_zoom = 0;
    for (int i = 6; i < argc; ++i) {
        if (!strcmp(argv[i], "--blend")) { use_zoom = 1; zo.blend = 1; }
        else if (!strcmp(argv[i], "--hard")) { use_zoom = 1; zo.blend = 0; }
        else if (!strcmp(argv[i], "--off")) { use_zoom = 0; }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 1; }
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
    RScene s;
    scene_init(&s);
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
        if (scene_add_poly(&s, pts, n) != 0) { fclose(f); return 3; }
    }
    fclose(f);
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (!use_zoom) {
        if (!(sc > 0.0)) {
            fprintf(stderr, "scale %g 为零或负：取对数无定义，拒绝渲染\n", sc);
            free(buf);
            return 8;
        }
        if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    } else {
        RZoomReport rep;
        int rc = pipeline_render_zoom(&s, place, win, w, h, &zo, buf, &rep);
        if (rc == -2) {
            fprintf(stderr, "scale %g 为零或负：取对数无定义，拒绝渲染\n", sc);
            free(buf);
            return 8;
        }
        if (rc == -7) {
            fprintf(stderr, "scale %g 不是有限数，拒绝渲染\n", sc);
            free(buf);
            return 9;
        }
        if (rc != 0) { free(buf); return 6; }
        printf("level %.6f lo %d hi %d w %.4f%s\n", rep.level, rep.lo, rep.hi, rep.w,
               rep.exact ? " exact" : "");
    }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
