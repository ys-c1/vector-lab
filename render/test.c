#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};

#ifndef RSNAP_DISABLE
static unsigned char *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    unsigned char *b = malloc((size_t)*len);
    if (fread(b, 1, (size_t)*len, f) != (size_t)*len) { free(b); fclose(f); return NULL; }
    fclose(f);
    return b;
}
static int write_file(const char *path, const unsigned char *b, long len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
#endif

#ifndef RSNAP_DISABLE
static void test_snapshot(void) {
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    RPlace place = {1.25, -2.5, 0.7, 1.5};
    RBox win = {0, 0, 10, 10};
    long off = -100;
    CK(snapshot_save("build/t.snap", &s, place, &win, 16, 16, &off) == SNAP_OK);
    CK(off == -1);
    long fsize = -1;
    unsigned char *raw = read_file("build/t.snap", &fsize);
    CK(raw != NULL);
    if (!raw) return;
    CK(fsize == RSNAP_HEADER_LEN + 4 + 4 * 16);
    CK(fsize == snapshot_size_bytes(&s));
    CK(memcmp(raw, RSNAP_MAGIC, 4) == 0);

    RScene s2;
    RPlace p2;
    RBox w2;
    int w, h;
    CK(snapshot_load("build/t.snap", &s2, &p2, &w2, &w, &h, &off) == SNAP_OK);
    CK(s2.npolys == 1 && s2.polys[0].n == 4);
    CK(memcmp(s2.polys[0].pts, sq, sizeof(sq)) == 0);
    CK(memcmp(&p2, &place, sizeof(place)) == 0);
    CK(memcmp(&w2, &win, sizeof(win)) == 0);
    CK(w == 16 && h == 16);

    /* 还原渲染必须与直接渲染逐像素相同。 */
    unsigned char a[256], b[256];
    CK(pipeline_render(&s, place, win, 16, 16, a) == 0);
    CK(pipeline_render(&s2, p2, w2, w, h, b) == 0);
    CK(memcmp(a, b, sizeof(a)) == 0);

    /* 坏魔数：拒绝，偏移 0。 */
    unsigned char *bad = malloc((size_t)fsize);
    memcpy(bad, raw, (size_t)fsize);
    bad[0] = 'X';
    CK(write_file("build/t_badmagic.snap", bad, fsize) == 0);
    CK(snapshot_load("build/t_badmagic.snap", &s2, &p2, &w2, &w, &h, &off) == SNAP_EMAGIC);
    CK(off == RSNAP_OFF_MAGIC);

    /* 版本号不符：拒绝，偏移 4。 */
    memcpy(bad, raw, (size_t)fsize);
    bad[7] = 0x02;
    CK(write_file("build/t_badver.snap", bad, fsize) == 0);
    CK(snapshot_load("build/t_badver.snap", &s2, &p2, &w2, &w, &h, &off) == SNAP_EVERSION);
    CK(off == RSNAP_OFF_VERSION);

    /* 字段截断：每个长度都必须拒绝；头部截断偏移=长度，
     * 记录区截断偏移=记录起点 RSNAP_OFF_RECORDS。 */
    for (long L = 0; L < fsize; ++L) {
        CK(write_file("build/t_trunc.snap", raw, L) == 0);
        int rc = snapshot_load("build/t_trunc.snap", &s2, &p2, &w2, &w, &h, &off);
        CK(rc == SNAP_ETRUNC);
        long want = (L < RSNAP_HEADER_LEN) ? L : RSNAP_OFF_RECORDS;
        CK(off == want);
    }

    /* 尾随字节：拒绝，偏移为正确文件长度。 */
    bad[fsize - 1] = raw[fsize - 1];
    unsigned char *trail = malloc((size_t)fsize + 3);
    memcpy(trail, raw, (size_t)fsize);
    trail[fsize] = trail[fsize + 1] = trail[fsize + 2] = 0;
    CK(write_file("build/t_trail.snap", trail, fsize + 3) == 0);
    CK(snapshot_load("build/t_trail.snap", &s2, &p2, &w2, &w, &h, &off) == SNAP_ETRAIL);
    CK(off == fsize);

    /* 非法字段：scale=0 与退化窗口。 */
    RPlace pz = place;
    pz.scale = 0.0;
    CK(snapshot_save("build/t_zero.snap", &s, pz, &win, 16, 16, &off) == SNAP_EINVAL);
    RBox bz = {0, 0, 0, 10};
    CK(snapshot_save("build/t_box.snap", &s, place, &bz, 16, 16, &off) == SNAP_EINVAL);
    free(raw);
    free(bad);
    free(trail);
}
#else
static void test_snapshot(void) {
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    RPlace place = {0, 0, 0, 1.0};
    RBox win = {0, 0, 10, 10};
    long off;
    CK(snapshot_save("build/x.snap", &s, place, &win, 16, 16, &off) == SNAP_EDISABLED);
    RScene s2;
    RPlace p2;
    RBox w2;
    int w, h;
    CK(snapshot_load("build/x.snap", &s2, &p2, &w2, &w, &h, &off) == SNAP_EDISABLED);
}
#endif

int main(void) {
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
    CK(scene_load_txt("scenes/s1_square.txt", &s) == 1);
    CK(s.npolys == 1 && s.polys[0].n == 4);
    test_snapshot();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
