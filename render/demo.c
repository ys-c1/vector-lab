/* 四组场景的反解留边演示：每组报留边像素、被切像素数、耗时。
 * 被切像素数 = 变换后场景渲染到「目标窗口 ∪ 场景包围盒」（网格与目标窗口对齐）
 * 时，中心落在目标窗口外的亮像素数。修复后必须为零。 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/margin.h"
#include "src/raster.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W 200
#define H 150
#define PI 3.14159265358979323846

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r = { (p.x * c - p.y * s) * q.scale + q.tx,
                 (p.x * s + p.y * c) * q.scale + q.ty };
    return r;
}

static void apply_place(RScene *dst, const RScene *src, RPlace pl) {
    scene_init(dst);
    for (int k = 0; k < src->npolys; ++k) {
        RPoly tmp = src->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], pl);
        scene_add_poly(dst, tmp.pts, tmp.n);
    }
}

/* 中心落在 target 窗外的亮像素数；画布为 target 按像素精度整格外扩到包住包围盒 */
static int clipped_outside(const RScene *moved, const RBox *target, int w, int h) {
    RBox bb;
    if (scene_bbox(moved, &bb) != 0) return 0;
    double dx = (target->maxx - target->minx) / w;
    double dy = (target->maxy - target->miny) / h;
    int kl = bb.minx < target->minx ? (int)ceil((target->minx - bb.minx) / dx - 1e-9) : 0;
    int kb = bb.miny < target->miny ? (int)ceil((target->miny - bb.miny) / dy - 1e-9) : 0;
    int kr = bb.maxx > target->maxx ? (int)ceil((bb.maxx - target->maxx) / dx - 1e-9) : 0;
    int kt = bb.maxy > target->maxy ? (int)ceil((bb.maxy - target->maxy) / dy - 1e-9) : 0;
    int cw = kl + w + kr, ch = kb + h + kt;
    RBox u = { target->minx - kl * dx, target->miny - kb * dy,
               target->maxx + kr * dx, target->maxy + kt * dy };
    unsigned char *buf = (unsigned char*)calloc((size_t)cw * ch, 1);
    if (!buf) return -1;
    raster_fill(moved, &u, cw, ch, 4, buf);
    int n = 0;
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x)
            if (buf[y * cw + x] && (x < kl || x >= kl + w || y < kb || y >= kb + h)) ++n;
    free(buf);
    return n;
}

static int bad = 0;

static void run_group(const char *name, const RScene *s, RPlace pl) {
    RBox win = {0, 0, 100, 100};
    if (scene_bbox(s, &win) != 0) win = (RBox){0, 0, 100, 100}; /* 空场景用占位窗 */
    unsigned char *buf = (unsigned char*)malloc((size_t)W * H);
    RMargin m = {0, 0, 0, 0};
    double t0 = now_ms();
    int rc = pipeline_render_fit(s, pl, win, W, H, buf, &m);
    double ms = now_ms() - t0;
    if (rc == RMARGIN_EMPTY) {
        printf("%s: 空场景，无包围盒可解留边，耗时 %.3f ms\n", name, ms);
    } else if (rc == RMARGIN_REJECT) {
        printf("%s: 解出留边 L%d R%d B%d T%d px，超过画面一成（%d/%d px），拒绝重绘，耗时 %.3f ms\n",
               name, m.left, m.right, m.bottom, m.top, W / 10, H / 10, ms);
    } else if (rc == 0) {
        RScene moved;
        apply_place(&moved, s, pl);
        int before = clipped_outside(&moved, &win, W, H);
        RBox ex = margin_expand(&win, W, H, m);
        int after = clipped_outside(&moved, &ex, W, H);
        printf("%s: 留边 L%d R%d B%d T%d px，被切像素 修复前 %d / 修复后 %d，耗时 %.3f ms\n",
               name, m.left, m.right, m.bottom, m.top, before, after, ms);
        if (after != 0) { printf("  错误：修复后仍被切 %d 像素\n", after); bad = 1; }
    } else {
        printf("%s: 渲染失败 rc=%d\n", name, rc);
        bad = 1;
    }
    free(buf);
}

int main(void) {
    RPoint sq[4] = {{0,0},{100,0},{100,100},{0,100}};

    /* 组A：贴边方块，放大 1.05 并重定位，四边各探出 2.5 */
    RScene a;
    scene_init(&a);
    scene_add_poly(&a, sq, 4);
    run_group("组A 缩放1.05", &a, (RPlace){-2.5, -2.5, 0.0, 1.05});

    /* 组B：贴边方块绕中心旋转 10 度，四角探出 */
    RScene b;
    scene_init(&b);
    scene_add_poly(&b, sq, 4);
    double c = cos(10 * PI / 180), sn = sin(10 * PI / 180);
    run_group("组B 旋转10度", &b, (RPlace){50 - 50 * (c - sn), 50 - 50 * (c + sn), 10 * PI / 180, 1.0});

    /* 组C：空场景 */
    RScene e;
    scene_init(&e);
    run_group("组C 空场景", &e, (RPlace){0, 0, 0, 1.0});

    /* 组D：平移 15，右侧需留 30px，超过画面宽一成（20px），应拒绝 */
    RScene d;
    scene_init(&d);
    scene_add_poly(&d, sq, 4);
    run_group("组D 平移15", &d, (RPlace){15, 0, 0, 1.0});

    return bad;
}
