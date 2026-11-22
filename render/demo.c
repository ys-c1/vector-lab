/*
 * 二维参数反解演示：
 *   1. 四组场景（斜边四边形 / 尖角三角 / 细长条 / L 形），以 gold 等价
 *      的 (ss=8, bits=8) 导出作为参照，按 (ss, bits) 二维网格穷举反解；
 *      逐场景报告选中的组合、差异字节（必须为 0）、耗时与所有并列组合。
 *   2. 旧默认 (ss=4, bits=1) 与参照的差异字节（说明“两种设置差多少”）。
 *   3. 参数守卫：ss/bits 越界或区间倒置必须拒绝并写清字段。
 *   4. 关闭新逻辑：旧 pipeline_render 与 ps_render(4,1) 逐字节一致。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/paramsolve.h"
#include "src/export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 256
#define H 256

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

typedef struct { const char *name; double xy[64]; int n; } Case;

static void build(const Case *c, RScene *s) {
    RPoint p[64];
    for (int i = 0; i < c->n; ++i) { p[i].x = c->xy[2 * i]; p[i].y = c->xy[2 * i + 1]; }
    scene_init(s);
    scene_add_poly(s, p, c->n);
}

static const Case CASES[] = {
    { "diagonal 斜边四边形", {38.3,42.1, 201.7,71.4, 167.2,208.6, 7.9,175.5}, 4 },
    { "sharp 尖角三角",      {15.3,239.6, 33.7,15.4, 241.2,127.8}, 3 },
    { "sliver 细长条",       {24.4,24.4, 232.1,40.4, 228.3,52.6, 20.2,36.1}, 4 },
    { "lshape L 形(有并列)", {10.25,10.25, 88.8,10.25, 88.8,40.25,
                              70.25,40.25, 70.25,88.8, 10.25,88.8}, 6 },
};
#define NCASE ((int)(sizeof CASES / sizeof CASES[0]))

int main(void) {
    RPlace place = {0.0, 0.0, 0.0, 1.0};
    unsigned char *ref = malloc((size_t)W * H);
    unsigned char *buf = malloc((size_t)W * H);
    if (!ref || !buf) return 2;

    printf("== 四组场景：参照 = (ss=8, bits=8)，画布 %dx%d，ss/bits 合法域 [1,8] ==\n",
           W, H);
    int total_matches = 0;
    for (int ci = 0; ci < NCASE; ++ci) {
        RScene s;
        build(&CASES[ci], &s);
        RBox win;
        scene_bbox(&s, &win);

        /* 参照：gold 独立的 8x8 超采样 + 8 位覆盖率量化口径。 */
        if (ps_render(&s, place, win, W, H, 8, 8, ref) != 0) {
            printf("[%s] 参照渲染失败\n", CASES[ci].name);
            free(ref); free(buf);
            return 3;
        }
        /* 旧默认口径的导出，用于对比“两种设置差多少字节”。 */
        if (ps_render(&s, place, win, W, H, 4, 1, buf) != 0) return 3;
        long legacy_diff = ps_diff(buf, ref, W * H);

        double t0 = now_ms();
        PSResult r;
        int rc = ps_solve(&s, place, win, W, H, ref, PS_SS_MIN, PS_SS_MAX,
                          PS_BITS_MIN, PS_BITS_MAX, 1, &r);
        double solve_ms = now_ms() - t0;

        printf("\n[%s]\n", CASES[ci].name);
        printf("  旧默认(ss=4,bits=1) vs 参照: 差异 %ld 字节\n", legacy_diff);
        if (rc != PS_OK) {
            printf("  反解失败: %s (code=%d)\n", ps_strerror(rc), rc);
            continue;
        }
        total_matches += r.nmatch;
        printf("  命中组合 %d 组（差异均为 0 字节）:\n", r.nmatch);
        for (int i = 0; i < r.nmatch; ++i)
            printf("    ss=%d bits=%d diff=%ld\n",
                   r.matches[i].ss, r.matches[i].bits, r.matches[i].diff_bytes);
        printf("  -> 选中 ss=%d bits=%d  差异字节=%ld  耗时 %.2f ms  规则=%s%s\n",
               r.ss, r.bits, r.diff_bytes, solve_ms,
               r.prefer_min ? "min(ss,bits)" : "max(ss,bits)",
               r.ambiguous ? "（多组并列，见下方选取说明）" : "（唯一）");
        for (int ss = PS_SS_MIN; ss <= PS_SS_MAX; ++ss)
            if (r.coverage_ms[ss] >= 0.0)
                printf("       覆盖率渲染 ss=%d: %.2f ms\n", ss, r.coverage_ms[ss]);
    }

    printf("\n== 多组并列时怎么选 ==\n");
    printf("  并列组合导出的 PGM 逐字节相同，选哪组都不改变图像；\n");
    printf("  默认按成本最小选：ss 优先（子样本越少越快），其次 bits。\n");
    printf("  上例 L 形并列 (4,8)/(8,8)，选 (4,8)：ss 更小、同样 8 位、同样字节。\n");
    printf("  若需要确定唯一来源（例如审计“当初是哪组导出的”），单凭字节\n");
    printf("  无法区分，必须结合外部信息；本工具如实报告 ambiguous。\n");

    printf("\n== 参数守卫：越界/倒置必须拒绝并写清字段 ==\n");
    RScene s0;
    build(&CASES[0], &s0);
    RBox w0;
    scene_bbox(&s0, &w0);
    char field[PS_FIELD_MAX];
    int bad_ss[] = {0, -1, 9};
    for (int i = 0; i < 3; ++i) {
        int rc2 = ps_validate_ss(bad_ss[i], field, sizeof field);
        printf("  ss=%d -> code=%d field=%s %s\n", bad_ss[i], rc2, field,
               rc2 == PS_OK ? "(未拒绝!)" : "(已拒绝)");
    }
    int bad_bits[] = {0, -1, 9};
    for (int i = 0; i < 3; ++i) {
        int rc2 = ps_validate_bits(bad_bits[i], field, sizeof field);
        printf("  bits=%d -> code=%d field=%s %s\n", bad_bits[i], rc2, field,
               rc2 == PS_OK ? "(未拒绝!)" : "(已拒绝)");
    }
    PSResult rr;
    int rc2 = ps_solve(&s0, place, w0, W, H, ref, 5, 4, 1, 8, 1, &rr);
    printf("  ss 区间 5..4 -> code=%d %s\n", rc2, ps_strerror(rc2));
    rc2 = ps_solve(&s0, place, w0, W, H, ref, 1, 8, 6, 2, 1, &rr);
    printf("  bits 区间 6..2 -> code=%d %s\n", rc2, ps_strerror(rc2));

    printf("\n== 关闭新逻辑：旧路径逐字节不变 ==\n");
    for (int ci = 0; ci < NCASE; ++ci) {
        RScene s;
        build(&CASES[ci], &s);
        RBox win;
        scene_bbox(&s, &win);
        /* 旧管线固定 ss=4、any-hit；新模块的 (4,1) 必须完全相同。 */
        if (pipeline_render(&s, place, win, W, H, ref) != 0) continue;
        if (ps_render(&s, place, win, W, H, 4, 1, buf) != 0) continue;
        int same = memcmp(ref, buf, (size_t)W * H) == 0;
        printf("  %-20s pipeline_render vs ps_render(4,1): %s\n",
               CASES[ci].name, same ? "IDENTICAL" : "DIFF!!");
    }

    free(ref); free(buf);
    return 0;
}
