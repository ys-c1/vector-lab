#include "src/scene.h"
#include "src/scene_file.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void write_tmp(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);
}

static void test_scene_file(void) {
    const char *p = "build/_ut.scene";
    SceneFile sf; SFError e;

    write_tmp(p, "version 1\ncanvas 12 10 2.0\npoly\n0 0\n4 0\n4 4\n0 4\nend\n");
    CK(scene_file_load(p, &sf, &e) == SF_OK);
    CK(sf.version == 1 && sf.width == 12 && sf.height == 10 && sf.scale == 2.0);
    CK(sf.scene.npolys == 1 && scene_count_verts(&sf.scene) == 4);

    write_tmp(p, "canvas 12 10 1.0\npoly\n0 0\n1 0\n1 1\nend\n");
    CK(scene_file_load(p, &sf, &e) == SF_ERR_VERSION && e.line > 0);

    write_tmp(p, "version 2\ncanvas 12 10 1.0\npoly\n0 0\n1 0\n1 1\nend\n");
    CK(scene_file_load(p, &sf, &e) == SF_ERR_VERSION);

    write_tmp(p, "version 1\nfrob 1\n");
    CK(scene_file_load(p, &sf, &e) == SF_ERR_SYNTAX);
    /* 动态字段名必须由错误结构自持，不能是悬垂指针 */
    CK(e.field != NULL && e.field[0] == 'f' && e.field[1] == 'r');

    write_tmp(p, "version 1\npoly\n0 0\n1 0\n1 1\nend\n");
    CK(scene_file_load(p, &sf, &e) == SF_ERR_MISSING);

    write_tmp(p, "version 1\ncanvas 0 10 1.0\npoly\n0 0\n1 0\n1 1\nend\n");
    CK(scene_file_load(p, &sf, &e) == SF_ERR_VALUE);

    write_tmp(p, "version 1\ncanvas 10 10 1.0\npoly\n0 0\n1 0\nend\n");
    CK(scene_file_load(p, &sf, &e) == SF_ERR_VALUE);

    CK(scene_file_load("build/_no_such_file.scene", &sf, &e) == SF_ERR_FILE);
    CK(e.line == 0);

    remove(p);
}

int main(void) {
    RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    CK(scene_count_verts(&s) == 4);
    RBox b;
    CK(scene_bbox(&s, &b) == 0);
    CK(b.minx == 0 && b.maxx == 10);
    unsigned char buf[16 * 16];
    CK(raster_fill(&s, &b, 16, 16, 4, buf) == 0);
    int on = export_count_on(buf, 16, 16);
    CK(on > 240 && on < 260);
    RPlace p = {0, 0, 0, 1.0};
    CK(pipeline_render(&s, p, b, 16, 16, buf) == 0);
    RPlace zero = {0, 0, 0, 0.0};
    CK(pipeline_render(&s, zero, b, 16, 16, buf) == -2);
    RBox bad = {0, 0, 0, 0};
    CK(pipeline_render(&s, p, bad, 16, 16, buf) == -3);
    CK(scene_add_poly(&s, sq, 2) == -1);
    test_scene_file();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
