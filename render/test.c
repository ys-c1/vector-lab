#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void render_basics(void) {
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
}

static void meta_validate_cases(void) {
    RMeta good = { 32, 24, 12, 1, 5 };
    CK(meta_validate(&good) == 0);
    RMeta bad_w = { 0, 24, 12, 1, 5 };
    CK(meta_validate(&bad_w) == RMETA_E_VALUE);
    RMeta bad_h = { 32, -1, 12, 1, 5 };
    CK(meta_validate(&bad_h) == RMETA_E_VALUE);
    RMeta bad_v = { 32, 24, -3, 1, 5 };
    CK(meta_validate(&bad_v) == RMETA_E_VALUE);
    RMeta bad_s0 = { 32, 24, 12, 0, 5 };
    CK(meta_validate(&bad_s0) == RMETA_E_VALUE);
    RMeta bad_sneg = { 32, 24, 12, -7, 5 };
    CK(meta_validate(&bad_sneg) == RMETA_E_VALUE);
    RMeta bad_e = { 32, 24, 12, 1, -1 };
    CK(meta_validate(&bad_e) == RMETA_E_VALUE);
    CK(meta_validate(NULL) == RMETA_E_VALUE);
}

static void meta_roundtrip(const char *dir) {
    char path[256];
    snprintf(path, sizeof path, "%s/one.meta.json", dir);
    RMeta m = { 640, 480, 37, 42, 13 };
    CK(meta_write(path, &m) == 0);
    RMeta got;
    memset(&got, 0, sizeof got);
    CK(meta_read(path, &got) == 0);
    CK(got.width == 640 && got.height == 480 && got.vertices == 37);
    CK(got.sequence == 42 && got.elapsed_ms == 13);

    /* 写非法元数据必须被拒绝，且不留文件 */
    char p2[256];
    snprintf(p2, sizeof p2, "%s/refuse.json", dir);
    RMeta bad = { 10, 10, 4, 0, 1 };
    CK(meta_write(p2, &bad) == RMETA_E_VALUE);
    FILE *probe = fopen(p2, "rb");
    CK(probe == NULL);
}

static void meta_reject_corrupt(const char *dir) {
    struct { const char *name; const char *body; } cases[] = {
        { "missing",  "{\n  \"width\": 10,\n  \"height\": 10,\n  \"vertices\": 4,\n  \"elapsed_ms\": 1\n}\n" },
        { "float",    "{\n  \"width\": 10.0,\n  \"height\": 10,\n  \"vertices\": 4,\n  \"sequence\": 1,\n  \"elapsed_ms\": 1\n}\n" },
        { "str",      "{\n  \"width\": \"10\",\n  \"height\": 10,\n  \"vertices\": 4,\n  \"sequence\": 1,\n  \"elapsed_ms\": 1\n}\n" },
        { "order",    "{\n  \"height\": 10,\n  \"width\": 10,\n  \"vertices\": 4,\n  \"sequence\": 1,\n  \"elapsed_ms\": 1\n}\n" },
        { "neg",      "{\n  \"width\": 10,\n  \"height\": 10,\n  \"vertices\": 4,\n  \"sequence\": -1,\n  \"elapsed_ms\": 1\n}\n" },
        { "trailing", "{\n  \"width\": 10,\n  \"height\": 10,\n  \"vertices\": 4,\n  \"sequence\": 1,\n  \"elapsed_ms\": 1,\n}\n" },
        { "garbage",  "not json at all\n" },
        { "extra",    "{\n  \"width\": 10,\n  \"height\": 10,\n  \"vertices\": 4,\n  \"sequence\": 1,\n  \"elapsed_ms\": 1,\n  \"extra\": 9\n}\n" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        char path[256];
        snprintf(path, sizeof path, "%s/bad_%s.json", dir, cases[i].name);
        FILE *f = fopen(path, "wb");
        CK(f != NULL);
        if (f) { fputs(cases[i].body, f); fclose(f); }
        RMeta got;
        int rc = meta_read(path, &got);
        if (rc == 0) { printf("FAIL case %s should be rejected\n", cases[i].name); ++fails; }
        ++checks;
    }
}

static void write_seq(const char *path, long long last, long long peak) {
    FILE *f = fopen(path, "wb");
    CK(f != NULL);
    if (f) { fprintf(f, "%lld\n%lld\n", last, peak); fclose(f); }
}

static void seq_cases(const char *dir) {
    char path[256];
    snprintf(path, sizeof path, "%s/render.seq", dir);
    remove(path);
    long long last = -1, peak = -1, next = -1;
    /* 首次：无文件，序号从 1 开始，峰值同步抬升 */
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 0 && peak == 0);
    CK(meta_seq_next(path, last, peak, &next) == 0 && next == 1);
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 1 && peak == 1);
    CK(meta_seq_next(path, 1, 1, &next) == 0 && next == 2);
    CK(meta_seq_next(path, 2, 2, &next) == 0 && next == 3);
    /* 调用方认知与文件不一致（传参对不上持久状态）一律拒绝，文件不动 */
    CK(meta_seq_next(path, 1, 2, &next) == RMETA_E_ORDER);
    CK(meta_seq_next(path, 3, 99, &next) == RMETA_E_ORDER);
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 3 && peak == 3);
    /* 自相矛盾的入参 peak<last 在入口即拒 */
    CK(meta_seq_next(path, 99, 2, &next) == RMETA_E_VALUE);
    CK(meta_seq_next(path, 3, 3, &next) == 0 && next == 4);

    /* 外部把当前号拨小且留下高水位痕迹（last < peak）：解析放行、取号拒绝 */
    write_seq(path, 2, 4);
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 2 && peak == 4);
    CK(meta_seq_next(path, 2, 4, &next) == RMETA_E_ORDER);
    /* peak < last 的文件自相矛盾，解析即拒 */
    write_seq(path, 5, 3);
    CK(meta_seq_load(path, &last, &peak) == RMETA_E_PARSE);

    /* 损坏的序号文件必须拒绝 */
    FILE *f = fopen(path, "wb");
    CK(f != NULL);
    fputs("abc\n2\n", f);
    fclose(f);
    CK(meta_seq_load(path, &last, &peak) == RMETA_E_PARSE);
    f = fopen(path, "wb");
    fputs("4\n", f);          /* 缺第二行 */
    fclose(f);
    CK(meta_seq_load(path, &last, &peak) == RMETA_E_PARSE);

    /* 恢复正常并继续递增 */
    write_seq(path, 4, 4);
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 4 && peak == 4);
    CK(meta_seq_next(path, last, peak, &next) == 0 && next == 5);
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 5 && peak == 5);

    /* 上限处再取号必须拒绝，不得回绕，文件保持 */
    write_seq(path, 9007199254740992LL, 9007199254740992LL);
    CK(meta_seq_next(path, 9007199254740992LL, 9007199254740992LL, &next) == RMETA_E_ORDER);
    CK(meta_seq_load(path, &last, &peak) == 0 && last == 9007199254740992LL);
}

int main(void) {
    render_basics();
    meta_validate_cases();
    const char *dir = "build/tmeta";
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", dir, dir);
    if (system(cmd) != 0) { printf("FAIL setup\n"); return 1; }
    meta_roundtrip(dir);
    meta_reject_corrupt(dir);
    seq_cases(dir);
    CK(meta_now_ms() >= 0);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
