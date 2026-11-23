#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int render_and_write(const RScene *s, RPlace place, RBox win,
                            int w, int h, const char *out) {
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    int rc = pipeline_render(s, place, win, w, h, buf);
    if (rc != 0) { free(buf); return 6; }
    if (export_pgm(out, buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s->npolys,
           scene_count_verts(s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}

/* 原流程：scene.txt out.pgm w h scale（窗口取场景包围盒，无平移旋转）。 */
static int cmd_render_legacy(char **argv) {
    RScene s;
    if (scene_load_txt(argv[1], &s) < 0) {
        fprintf(stderr, "load scene failed\n");
        return 2;
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    RPlace place = {0.0, 0.0, 0.0, sc};
    return render_and_write(&s, place, win, w, h, argv[2]);
}

static int parse_win(int argc, char **argv, int first, const RScene *s,
                     RBox *win) {
    if (argc > first) {
        if (argc < first + 4) return -1;
        win->minx = atof(argv[first]);
        win->miny = atof(argv[first + 1]);
        win->maxx = atof(argv[first + 2]);
        win->maxy = atof(argv[first + 3]);
    } else if (scene_bbox(s, win) != 0) {
        return -1;
    }
    return 0;
}

/* snap scene.txt snap.bin w h scale [tx ty rot minx miny maxx maxy] */
static int cmd_snap(int argc, char **argv) {
    if (argc < 7 || argc > 14) {
        fprintf(stderr,
                "usage: %s snap scene.txt snap.bin w h scale "
                "[tx ty rot minx miny maxx maxy]\n", argv[0]);
        return 1;
    }
    RScene s;
    if (scene_load_txt(argv[2], &s) < 0) {
        fprintf(stderr, "load scene failed\n");
        return 2;
    }
    int w = atoi(argv[4]), h = atoi(argv[5]);
    RPlace place = {0.0, 0.0, 0.0, atof(argv[6])};
    if (argc >= 10) {
        place.tx = atof(argv[7]);
        place.ty = atof(argv[8]);
        place.rot = atof(argv[9]);
    }
    RBox win;
    if (parse_win(argc, argv, 10, &s, &win) != 0) return 4;
    long off;
    int rc = snapshot_save(argv[3], &s, place, &win, w, h, &off);
    if (rc != SNAP_OK) {
        fprintf(stderr, "snapshot save failed: %s (offset %ld)\n",
                snapshot_strerror(rc), off);
        return 8;
    }
    printf("snapshot %s %ld bytes\n", argv[3], snapshot_size_bytes(&s));
    return 0;
}

/* restore snap.bin out.pgm —— 场景/变换/窗口/宽高全部来自快照 */
static int cmd_restore(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s restore snap.bin out.pgm\n", argv[0]);
        return 1;
    }
    RScene s;
    RPlace place;
    RBox win;
    int w, h;
    long off;
    int rc = snapshot_load(argv[2], &s, &place, &win, &w, &h, &off);
    if (rc != SNAP_OK) {
        fprintf(stderr, "snapshot load rejected: %s at byte offset %ld\n",
                snapshot_strerror(rc), off);
        return 9;
    }
    return render_and_write(&s, place, win, w, h, argv[3]);
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "snap") == 0)
        return cmd_snap(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "restore") == 0)
        return cmd_restore(argc, argv);
    if (argc < 6) {
        fprintf(stderr,
                "usage: %s scene.txt out.pgm w h scale\n"
                "       %s snap scene.txt snap.bin w h scale "
                "[tx ty rot minx miny maxx maxy]\n"
                "       %s restore snap.bin out.pgm\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }
    return cmd_render_legacy(argv);
}
