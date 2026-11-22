/* 剔除阈值评估：手设阈值（现方案） vs 期望代价阈值（新方案）。
 *
 * 模型：错剔一个多边形损失其实测覆盖像素；漏剔一个零覆盖多边形折 lambda
 * 个缺块像素。期望缺块 E(T) = sum_{size<T} cov + lambda * #{size>=T, cov==0}。
 * 实测缺块：与参照图（不剔除渲染）逐像素比对，参照亮而本图灭的像素数。
 *
 * 用法：bench [lambda] [t_hand]    默认 lambda=0.5, t_hand=2.0
 * lambda < 0 会被拒绝并说明原因。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/raster.h"
#include "src/cull.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W 96
#define H 72
#define SS 4
#define REPS 40

static const RBox WIN = {0.0, 0.0, 96.0, 72.0};
static volatile unsigned int sink;
static int g_fails;

#define CHK(cond, msg) do { if (!(cond)) { ++g_fails; \
    printf("  [断言失败] %s\n", msg); } } while (0)

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void add_box(RScene *s, double x, double y, double wd, double ht) {
    RPoint p[4] = {{x, y}, {x + wd, y}, {x + wd, y + ht}, {x, y + ht}};
    scene_add_poly(s, p, 4);
}

/* 零覆盖细条：宽 1/16，x 落在 [f+15/16, f+1]，避开所有子采样列，
 * 尺寸 = 0.0625 * ht px^2，保证覆盖为 0。 */
static void add_sliver(RScene *s, double xf, double y, double ht) {
    add_box(s, xf + 0.9375, y, 0.0625, ht);
}

typedef struct {
    const char *name;
    RScene s;
    double sizes[32], covs[32];
    int m;                 /* 参与阈值决策的多边形数（size>0） */
    int nfree;             /* 免费剔除数（size==0） */
    unsigned char ref[W * H];
} Case;

static void prepare(Case *c, const char *name) {
    c->name = name;
    c->m = 0;
    c->nfree = 0;
    raster_fill(&c->s, &WIN, W, H, SS, c->ref);
    for (int k = 0; k < c->s.npolys; ++k) {
        double sz = cull_poly_size(&c->s.polys[k], &WIN, W, H);
        if (sz == 0.0) { c->nfree++; continue; }
        c->sizes[c->m] = sz;
        c->covs[c->m] = (double)raster_poly_coverage(&c->s.polys[k], &WIN, W, H, SS);
        c->m++;
    }
}

static double expected_cost(const Case *c, double T, double lambda) {
    double e = 0.0;
    for (int i = 0; i < c->m; ++i) {
        if (c->sizes[i] < T) e += c->covs[i];
        else if (c->covs[i] == 0.0) e += lambda;
    }
    return e;
}

static double hit_rate(const Case *c, double T) {
    int total = c->s.npolys, ok = c->nfree;  /* 免费剔除必正确 */
    if (!total) return 100.0;
    for (int i = 0; i < c->m; ++i) {
        int cull = c->sizes[i] < T;
        int should = c->covs[i] == 0.0;
        if (cull == should) ok++;
    }
    return 100.0 * ok / total;
}

typedef struct {
    double T, expected, hit, ms;
    int miss, extra;
} Row;

static void eval_row(const Case *c, double T, double lambda, int use_plain, Row *r) {
    unsigned char *buf = (unsigned char *)malloc(W * H);
    r->T = T;
    r->expected = expected_cost(c, T, lambda);
    r->hit = hit_rate(c, T);
    if (use_plain) raster_fill(&c->s, &WIN, W, H, SS, buf);
    else raster_fill_cull(&c->s, &WIN, W, H, SS, T, NULL, buf);
    r->miss = r->extra = 0;
    for (int i = 0; i < W * H; ++i) {
        if (c->ref[i] && !buf[i]) r->miss++;
        else if (!c->ref[i] && buf[i]) r->extra++;
    }
    double t0 = now_ms();
    for (int rep = 0; rep < REPS; ++rep) {
        if (use_plain) raster_fill(&c->s, &WIN, W, H, SS, buf);
        else raster_fill_cull(&c->s, &WIN, W, H, SS, T, NULL, buf);
        sink += buf[(rep * 997) % (W * H)];
    }
    r->ms = (now_ms() - t0) / REPS;
    free(buf);
}

static void print_row(const char *tag, const Row *r) {
    printf("  %-10s 阈值 %8.3f  期望缺块 %8.1f px  实测缺块 %4d px  命中率 %5.1f%%  耗时 %7.3f ms\n",
           tag, r->T, r->expected, r->miss, r->hit, r->ms);
}

/* ---- 四类场景 ---- */

static void build_dense_small(RScene *s) {   /* 1 密集小件：可见小方块 + 亚像素碎屑 */
    scene_init(s);
    for (int k = 0; k < 12; ++k)
        add_box(s, 4 + (k % 6) * 15 - 0.15, 6 + (k / 6) * 30 - 0.15, 1.3, 1.3);
    for (int k = 0; k < 8; ++k) add_sliver(s, 8 + k * 10, 30, 12);
}

static void build_offscreen(RScene *s) {     /* 2 离屏为主：大件 + 大量窗口外 + 零覆盖细条 */
    scene_init(s);
    for (int k = 0; k < 6; ++k)
        add_box(s, 4 + (k % 3) * 30, 4 + (k / 3) * 36, 20, 20);
    for (int k = 0; k < 16; ++k)
        add_box(s, -80 - (k % 4) * 30, -80 - (k / 4) * 30, 10, 10);
    for (int k = 0; k < 6; ++k) add_sliver(s, 10 + k * 13, 10, 50);
}

static void build_mixed(RScene *s) {         /* 3 混合交错：尺寸横跨手设阈值两侧 */
    scene_init(s);
    for (int k = 0; k < 6; ++k) add_box(s, 6 + k * 14 - 0.15, 8, 1.3, 1.3);
    for (int k = 0; k < 4; ++k)
        add_box(s, 8 + (k % 2) * 40, 30 + (k / 2) * 20, 18, 18);
    for (int k = 0; k < 6; ++k) add_sliver(s, 5 + k * 15, 20, 12);
    for (int k = 0; k < 4; ++k) add_sliver(s, 12 + k * 20, 6, 60);
}

static void build_dominant(RScene *s) {      /* 4 主体加浮尘：一个主体 + 中等件 + 浮尘 */
    scene_init(s);
    add_box(s, 18, 6, 60, 60);
    add_box(s, 2, 2, 6, 6);
    add_box(s, 88, 2, 6, 6);
    add_box(s, 2, 64, 6, 6);
    for (int k = 0; k < 10; ++k) add_sliver(s, 3 + k * 9, 2, 44 + (k % 5) * 6);
    for (int k = 0; k < 5; ++k) add_box(s, 120 + k * 20, 100, 10, 10);
}

/* ---- 退化场景 ---- */

static void build_empty(RScene *s) { scene_init(s); }

static void build_all_off(RScene *s) {       /* 全部在窗口外 */
    scene_init(s);
    for (int k = 0; k < 8; ++k) add_box(s, 150 + k * 20, 150, 10, 10);
}

static void build_all_blank(RScene *s) {     /* 全零覆盖：参照图全黑 */
    scene_init(s);
    for (int k = 0; k < 8; ++k) add_sliver(s, 4 + k * 11, 6, 10 + k * 7);
}

static void build_uniform(RScene *s) {       /* 尺寸无区分度：全部为 1.0 px^2 */
    scene_init(s);
    add_box(s, 10, 10, 1, 1);
    add_box(s, 30, 10, 1, 1);
    add_box(s, 50, 10, 1, 1);
    add_sliver(s, 20, 10, 16);
    add_sliver(s, 40, 10, 16);
    add_sliver(s, 60, 10, 16);
}

/* 自动阈值 + 校准耗时（尺寸/覆盖测量 + 选阈值，一次性成本） */
static double calibrate(const Case *c, double lambda, double *out_T, double *out_cost, int *out_rc) {
    double t0 = now_ms();
    double sizes[32], covs[32];
    int m = 0;
    for (int k = 0; k < c->s.npolys; ++k) {
        double sz = cull_poly_size(&c->s.polys[k], &WIN, W, H);
        if (sz == 0.0) continue;
        sizes[m] = sz;
        covs[m] = (double)raster_poly_coverage(&c->s.polys[k], &WIN, W, H, SS);
        ++m;
    }
    int rc = cull_pick_threshold(sizes, covs, m, lambda, out_T, out_cost);
    if (rc == 0) {
        /* 与 prepare 的测量一致性校验 */
        CHK(m == c->m, "校准与评估的多边形数不一致");
    }
    *out_rc = rc;
    return now_ms() - t0;
}

static void run_case(Case *c, double lambda, double t_hand, int strict) {
    printf("%s：%d 个多边形（免费剔除 %d，参与阈值决策 %d）\n",
           c->name, c->s.npolys, c->nfree, c->m);
    if (c->m == 0) {
        double T, cost;
        int rc;
        calibrate(c, lambda, &T, &cost, &rc);
        CHK(rc == -2, "无可决策对象时 cull_pick_threshold 应返回 -2");
        Row off;
        eval_row(c, 0.0, lambda, 1, &off);
        printf("  退化：无可决策对象（%s），阈值无意义，按不剔除渲染；实测缺块 %d px\n",
               c->s.npolys == 0 ? "空场景" : "全部免费剔除", off.miss);
        CHK(off.miss == 0 && off.extra == 0, "退化场景输出必须与参照一致");
        return;
    }
    double T_auto, cost_auto;
    int rc;
    double t_calib = calibrate(c, lambda, &T_auto, &cost_auto, &rc);
    CHK(rc == 0, "自动阈值选择应成功");
    if (rc != 0) return;

    Row off, hand, aut;
    eval_row(c, 0.0, lambda, 1, &off);
    eval_row(c, t_hand, lambda, 0, &hand);
    eval_row(c, T_auto, lambda, 0, &aut);
    print_row("不剔除", &off);
    print_row("手设阈值", &hand);
    print_row("期望代价", &aut);
    printf("  （自动阈值校准 %.2f ms，一次性；手设阈值 T=%.3f，lambda=%.2f）\n",
           t_calib, t_hand, lambda);

    CHK(cost_auto == aut.expected, "期望缺块与阈值选择器输出不一致");
    CHK(aut.expected < hand.expected, "期望缺块必须低于手设阈值方案");
    CHK(aut.miss == 0 && aut.extra == 0, "新方案与参照图逐像素比必须无缺块");
    CHK(aut.miss <= hand.miss, "实测缺块不得多于手设阈值方案");
    if (strict) {
        CHK(hand.expected > 0.0, "对照场景里手设方案应当确实有代价");
    }
}

int main(int argc, char **argv) {
    double lambda = argc > 1 ? atof(argv[1]) : 0.5;
    double t_hand = argc > 2 ? atof(argv[2]) : 2.0;

    printf("剔除阈值评估：窗口 %.0fx%.0f，%dx%d 超采样，每方案渲染 %d 次取平均\n",
           WIN.maxx - WIN.minx, WIN.maxy - WIN.miny, SS, SS, REPS);
    printf("代价模型：期望缺块 = 错剔像素 + lambda * 漏剔数\n\n");

    if (lambda < 0.0) {
        printf("拒绝：代价权重 lambda=%.2f 为负。负权重会把漏剔记成负代价，\n"
               "期望缺块随剔除量单调下降，最优解退化为全部剔除，模型失效。\n", lambda);
        return 2;
    }

    /* 负权重拒绝演示（用场景 1 的统计数据） */
    {
        Case demo;
        build_dense_small(&demo.s);
        prepare(&demo, "负权重校验");
        double T, cost;
        int rc = cull_pick_threshold(demo.sizes, demo.covs, demo.m, -0.5, &T, &cost);
        printf("代价权重校验：lambda=-0.5 -> %s (rc=%d)。负权重把漏剔记成负代价，\n"
               "期望缺块随剔除量单调下降，最优退化为全部剔除，模型失效，故拒绝。\n\n",
               rc == -1 ? "已拒绝" : "未拒绝", rc);
        CHK(rc == -1, "负权重必须被拒绝");
    }

    typedef void (*builder)(RScene *);
    const char *names[4] = {
        "场景1 密集小件（可见小方块 1.69px^2 + 零覆盖碎屑 0.75px^2）",
        "场景2 离屏为主（大件 400px^2 + 16 个窗口外 + 零覆盖细条 3.1px^2）",
        "场景3 混合交错（尺寸 0.75/1.69/3.75/324 px^2 横跨手设阈值）",
        "场景4 主体加浮尘（主体 3600px^2 + 中等件 + 浮尘 2.75~4.25px^2）",
    };
    builder builders[4] = {build_dense_small, build_offscreen, build_mixed, build_dominant};
    printf("==== 四类场景 ====\n");
    for (int i = 0; i < 4; ++i) {
        Case c;
        builders[i](&c.s);
        prepare(&c, names[i]);
        run_case(&c, lambda, t_hand, 1);
        printf("\n");
    }

    printf("==== 退化场景（单独报告，不参与上面对比） ====\n");
    {
        Case d1, d2, d3, d4;
        build_empty(&d1.s);
        prepare(&d1, "D1 空场景");
        run_case(&d1, lambda, t_hand, 0);
        build_all_off(&d2.s);
        prepare(&d2, "D2 全部在窗口外");
        run_case(&d2, lambda, t_hand, 0);
        build_all_blank(&d3.s);
        prepare(&d3, "D3 全零覆盖（参照图全黑）");
        run_case(&d3, lambda, t_hand, 0);
        build_uniform(&d4.s);
        prepare(&d4, "D4 尺寸无区分度（所有多边形同为 1.0 px^2，阈值无法区分，退化为全剔/全留二选一）");
        run_case(&d4, lambda, t_hand, 0);
    }

    printf("\n%s（%d 项断言失败）\n", g_fails ? "存在失败" : "全部通过", g_fails);
    return g_fails ? 1 : 0;
}
