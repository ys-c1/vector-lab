#define _POSIX_C_SOURCE 199309L
/*
 * 序列化往返验证：
 *  1) 三个场景各自 直接渲染 vs 存盘->读回->渲染，逐像素比，差异必须为 0
 *  2) 报表：字段数、差异像素、往返耗时（50 次 save+load 平均）
 *  3) 拒读：漏字段 / 长度不符 / 版本不对 / magic 不对 / 尾部多余字节，均须拒绝并报字段名
 *  4) 大端线格式（RSBE）写一份再读回，渲染差异必须为 0 —— 证明字节序无关
 *  5) 模式开关切到 raw：走内存直拷老路径，出图与直接渲染逐字节相同
 */
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/serio.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define W 96
#define H 96
#define SS 4
#define ROUNDS 50

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int diff_pixels(const unsigned char *a, const unsigned char *b, int n) {
    int d = 0;
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) ++d;
    return d;
}

static int render(const RScene *s, unsigned char *buf) {
    RBox win;
    if (scene_bbox(s, &win) != 0) return -100;
    RPlace place = {0.75, -1.25, 0.3, 1.25};
    return pipeline_render(s, place, win, W, H, buf);
}

/* ---- 测试用本地按字节 writer（小端/大端），不依赖被测内部函数 ---- */
static void w_u16(FILE *f, uint16_t v, int be) {
    unsigned char p[2];
    if (be) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
    else    { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
    fwrite(p, 1, 2, f);
}
static void w_u32(FILE *f, uint32_t v, int be) {
    unsigned char p[4];
    for (int i = 0; i < 4; ++i)
        p[be ? (3 - i) : i] = (unsigned char)(v >> (8 * i));
    fwrite(p, 1, 4, f);
}
static void w_dbl(FILE *f, double d, int be) {
    uint64_t u;
    memcpy(&u, &d, 8);
    unsigned char p[8];
    for (int i = 0; i < 8; ++i)
        p[be ? (7 - i) : i] = (unsigned char)(u >> (8 * i));
    fwrite(p, 1, 8, f);
}
static void write_scene_file(const char *path, const RScene *s, int be) {
    FILE *f = fopen(path, "wb");
    fwrite(be ? "RSBE" : "RSLE", 1, 4, f);
    w_u16(f, SERIO_VERSION, be);
    w_u32(f, (uint32_t)s->npolys, be);
    for (int k = 0; k < s->npolys; ++k) {
        w_u32(f, (uint32_t)s->polys[k].n, be);
        for (int i = 0; i < s->polys[k].n; ++i) {
            w_dbl(f, s->polys[k].pts[i].x, be);
            w_dbl(f, s->polys[k].pts[i].y, be);
        }
    }
    fclose(f);
}

static int failures = 0;
#define CK(c) do { if (!(c)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void build_scenes(RScene out[3]) {
    /* 场景 1：单四边形，含负坐标与小数 */
    scene_init(&out[0]);
    RPoint sq[4] = {{-5.5, -2.25}, {4.25, -2.25}, {4.25, 6.75}, {-5.5, 6.75}};
    CK(scene_add_poly(&out[0], sq, 4) == 0);

    /* 场景 2：3 个多边形（三角形 / 四边形 / 五边形） */
    scene_init(&out[1]);
    RPoint tri[3] = {{-8.0, -7.5}, {0.125, -6.0}, {-3.75, 1.5}};
    RPoint quad[4] = {{1.5, -4.5}, {9.0, -3.0}, {7.25, 4.0}, {2.0, 3.75}};
    RPoint pent[5] = {{-2.0, 4.5}, {3.5, 5.25}, {4.75, 9.0}, {0.0, 10.5}, {-3.25, 8.0}};
    CK(scene_add_poly(&out[1], tri, 3) == 0);
    CK(scene_add_poly(&out[1], quad, 4) == 0);
    CK(scene_add_poly(&out[1], pent, 5) == 0);

    /* 场景 3：16 个 16 边形网格，全部小数坐标，压满批量路径 */
    scene_init(&out[2]);
    for (int k = 0; k < 16; ++k) {
        RPoint pts[16];
        double cx = (k % 4) * 7.5 - 11.25;
        double cy = (k / 4) * 7.5 - 7.5;
        double r = 2.125 + 0.0625 * (k % 3);
        for (int i = 0; i < 16; ++i) {
            double a = 2.0 * M_PI * i / 16.0 + 0.13 * k;
            pts[i].x = cx + r * cos(a);
            pts[i].y = cy + r * sin(a);
        }
        CK(scene_add_poly(&out[2], pts, 16) == 0);
    }
}

static const char *scode(int c) {
    switch (c) {
        case SERIO_E_MAGIC:   return "MAGIC";
        case SERIO_E_VERSION: return "VERSION";
        case SERIO_E_LENGTH:  return "LENGTH";
        case SERIO_E_MISSING: return "MISSING";
        case SERIO_E_IO:      return "IO";
        case SERIO_E_ARG:     return "ARG";
        default:              return "?";
    }
}

static void expect_reject(const char *path, int want) {
    RScene got;
    SerioErr e;
    int rc = scene_load_ex(path, &got, &e);
    int ok = (rc == want);
    CK(ok);
    printf("  reject %-28s rc=%d %-7s field=%-22s %s\n",
           path, rc, scode(rc), e.field[0] ? e.field : "-",
           ok ? "ok" : "*** UNEXPECTED ***");
}

int main(void) {
    serio_init();
    int initial = serio_get_mode();
    const char *env = getenv("RENDER_SERIO");
    int want_initial = (env && (!strcmp(env, "raw") || !strcmp(env, "0"))) ? 0 : 1;
    CK(initial == want_initial);
    printf("initial mode from RENDER_SERIO=%s: %s\n",
           env ? env : "(unset)", initial ? "explicit-bytes" : "raw-memcpy");
    /* 显式格式各节固定走新路径；老路径在结尾单独切换验证 */
    serio_set_mode(1);
    RScene sc[3];
    build_scenes(sc);
    const char *names[3] = {"square", "multi-poly", "dense-grid"};
    size_t npix = (size_t)W * H;
    unsigned char *direct = (unsigned char*)malloc(npix);
    unsigned char *via    = (unsigned char*)malloc(npix);

    printf("== 显式按字节序列化（默认路径，线格式小端 RSLE）==\n");
    printf("%-11s %6s %6s %8s %10s %14s\n",
           "scene", "polys", "verts", "fields", "diff-px", "rt-avg(ms)");
    for (int t = 0; t < 3; ++t) {
        char bin[64], pgm[64], dpgm[64];
        snprintf(bin, sizeof bin, "build/%s.bin", names[t]);
        snprintf(pgm, sizeof pgm, "build/%s.pgm", names[t]);
        snprintf(dpgm, sizeof dpgm, "build/%s.direct.pgm", names[t]);

        CK(render(&sc[t], direct) == 0);
        CK(export_pgm(dpgm, direct, W, H) == 0);

        /* 计时：ROUNDS 次 save+load（真实文件 IO） */
        double t0 = now_ms();
        RScene back;
        for (int r = 0; r < ROUNDS; ++r) {
            CK(scene_save(&sc[t], bin) == SERIO_OK);
            CK(scene_load(bin, &back) == SERIO_OK);
        }
        double dt = (now_ms() - t0) / ROUNDS;

        CK(render(&back, via) == 0);
        int d = diff_pixels(direct, via, (int)npix);
        CK(d == 0);
        CK(export_pgm(pgm, via, W, H) == 0);

        int fields = serio_field_count(&sc[t]);
        CK(fields == 3 + sc[t].npolys + 2 * scene_count_verts(&sc[t]));
        printf("%-11s %6d %6d %8d %10d %14.4f\n",
               names[t], sc[t].npolys, scene_count_verts(&sc[t]),
               fields, d, dt);
    }

    /* 大端线格式：手写 RSBE，读回渲染必须零差异 —— 字节序无关的直接证据 */
    printf("\n== 大端载荷（RSBE）读回 ==\n");
    for (int t = 0; t < 3; ++t) {
        char be[64];
        snprintf(be, sizeof be, "build/%s.be.bin", names[t]);
        write_scene_file(be, &sc[t], 1);
        RScene back;
        SerioErr e;
        int rc = scene_load_ex(be, &back, &e);
        CK(rc == SERIO_OK);
        CK(render(&sc[t], direct) == 0);
        CK(render(&back, via) == 0);
        int d = diff_pixels(direct, via, (int)npix);
        CK(d == 0);
        printf("  %-11s RSBE load ok, diff-px=%d\n", names[t], d);
    }

    /* 拒读用例：漏字段 / 长度不符 / 版本不对 / magic 不对 / 多余字节 */
    printf("\n== 拒读用例（必须报错并给出字段名）==\n");
    /* 合法小文件做底料 */
    RScene one;
    scene_init(&one);
    RPoint tri[3] = {{0, 0}, {3.5, 0}, {1.25, 2.75}};
    CK(scene_add_poly(&one, tri, 3) == 0);
    write_scene_file("build/ok.bin", &one, 0);

    /* a) 漏字段：截断到第一个点的 x 写到一半 */
    FILE *f = fopen("build/ok.bin", "rb");
    fseek(f, 0, SEEK_END);
    long full = ftell(f);
    unsigned char *blob = (unsigned char*)malloc((size_t)full);
    rewind(f);
    CK(fread(blob, 1, (size_t)full, f) == (size_t)full);
    fclose(f);
    f = fopen("build/trunc.bin", "wb");
    fwrite(blob, 1, 18, f); /* 4 magic + 2 ver + 4 npolys + 4 poly.n + 4/8 of pts[0].x */
    fclose(f);
    free(blob);
    expect_reject("build/trunc.bin", SERIO_E_MISSING);

    /* b) 长度不符：poly[0].n 声明 99（超出 3..64） */
    f = fopen("build/badlen.bin", "wb");
    fwrite("RSLE", 1, 4, f); w_u16(f, 1, 0); w_u32(f, 1, 0); w_u32(f, 99, 0);
    fclose(f);
    expect_reject("build/badlen.bin", SERIO_E_LENGTH);

    /* c) 版本不对 */
    f = fopen("build/badver.bin", "wb");
    fwrite("RSLE", 1, 4, f); w_u16(f, 99, 0); w_u32(f, 0, 0);
    fclose(f);
    expect_reject("build/badver.bin", SERIO_E_VERSION);

    /* d) magic 不对 */
    f = fopen("build/badmagic.bin", "wb");
    fwrite("XXXX", 1, 4, f); w_u16(f, 1, 0); w_u32(f, 0, 0);
    fclose(f);
    expect_reject("build/badmagic.bin", SERIO_E_MAGIC);

    /* e) 尾部多余字节：声明长度与实际载荷不符 */
    write_scene_file("build/ok.bin", &one, 0);
    FILE *g = fopen("build/ok.bin", "rb");
    fseek(g, 0, SEEK_END);
    long osz = ftell(g);
    unsigned char *ob = (unsigned char*)malloc((size_t)osz);
    rewind(g);
    CK(fread(ob, 1, (size_t)osz, g) == (size_t)osz);
    fclose(g);
    f = fopen("build/trail.bin", "wb");
    fwrite(ob, 1, (size_t)osz, f);
    unsigned char extra = 0x55;
    fwrite(&extra, 1, 1, f);
    fclose(f);
    free(ob);
    expect_reject("build/trail.bin", SERIO_E_LENGTH);

    /* npolys 越界同样按长度不符拒绝 */
    f = fopen("build/badnp.bin", "wb");
    fwrite("RSLE", 1, 4, f); w_u16(f, 1, 0); w_u32(f, 33, 0);
    fclose(f);
    expect_reject("build/badnp.bin", SERIO_E_LENGTH);

    /* 老路径：内存直拷，开关关掉后出图字节不变 */
    printf("\n== 老路径（内存直拷，RENDER_SERIO=raw / serio_set_mode(0)）==\n");
    serio_set_mode(0);
    CK(serio_get_mode() == 0);
    for (int t = 0; t < 3; ++t) {
        char raw[64];
        snprintf(raw, sizeof raw, "build/%s.raw", names[t]);
        RScene back;
        CK(scene_save(&sc[t], raw) == SERIO_OK);
        FILE *rf = fopen(raw, "rb");
        fseek(rf, 0, SEEK_END);
        long rsz = ftell(rf);
        fclose(rf);
        CK(rsz == (long)sizeof(RScene));
        CK(scene_load(raw, &back) == SERIO_OK);
        CK(render(&sc[t], direct) == 0);
        CK(render(&back, via) == 0);
        int d = diff_pixels(direct, via, (int)npix);
        CK(d == 0);

        /* 直拷文件不含 RSLE 头，证明两条路径确实不同 */
        unsigned char head[4] = {0};
        rf = fopen(raw, "rb");
        CK(fread(head, 1, 4, rf) == 4);
        fclose(rf);
        CK(memcmp(head, "RSLE", 4) != 0);
        printf("  %-11s raw blob=%ld bytes (sizeof RScene=%zu), diff-px=%d\n",
               names[t], rsz, sizeof(RScene), d);
    }
    /* 直拷路径遇到长度不符也拒绝 */
    f = fopen("build/short.raw", "wb");
    fwrite("x", 1, 1, f);
    fclose(f);
    expect_reject("build/short.raw", SERIO_E_LENGTH);

    serio_set_mode(initial);
    printf("\n%s\n", failures ? "ROUNDTRIP FAIL" : "ALL ROUNDTRIP CHECKS PASSED (diff pixels = 0)");
    free(direct);
    free(via);
    return failures ? 1 : 0;
}
