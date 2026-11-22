/* 按目标面积反解线宽的演示：四组路径各报宽度、覆盖面积、误差、耗时。
 * 需编译开关 STROKE_AREA_FIT；`make demo-area` 已带。
 *
 * 覆盖面积有两套独立算法互相核对：
 *   1. 反解时用的逐行胶囊并集积分（stroke_area_measure）
 *   2. 本文件里的蒙特卡洛采样（与 Reed 无关的随机实现，仅用于核验）
 * 二者相对偏差超过 1% 时在报告中标 MISMATCH。 */
#define _POSIX_C_SOURCE 199309L
#include "stroke_area.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SA_PI 3.14159265358979323846
#define TARGET 1200.0        /* 目标覆盖面积 */
#define W_MIN   0.01
#define W_MAX   400.0
#define MC_SAMPLES 3000000
#define ERR_LIMIT 0.01        /* 面积误差上限：1% */

typedef struct {
    const char *name;
    int closed;
    int self_expected;
    int n;
    SPoint pts[16];
} Case;

/* 蒙特卡洛面积：在与输入无关的包围盒里均匀撒点，落入任一胶囊体即计数。
 * 胶囊体 = 到线段距离 <= h 的点集；圆头端点天然包含。 */
static double mc_area(const SPoint *p, int n, int closed, double width, unsigned seed) {
    double h = width * 0.5;
    double xmin = p[0].x, xmax = p[0].x, ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; ++i) {
        if (p[i].x < xmin) xmin = p[i].x;
        if (p[i].x > xmax) xmax = p[i].x;
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    xmin -= h; xmax += h; ymin -= h; ymax += h;
    int m = closed ? n : n - 1;
    unsigned s = seed | 1u;
    long hit = 0;
    for (long k = 0; k < MC_SAMPLES; ++k) {
        s = s * 1664525u + 1013904223u;
        double ux = (double)(s >> 8) / (double)(1u << 24);
        s = s * 1664525u + 1013904223u;
        double uy = (double)(s >> 8) / (double)(1u << 24);
        double x = xmin + ux * (xmax - xmin);
        double y = ymin + uy * (ymax - ymin);
        for (int i = 0; i < m; ++i) {
            SPoint a = p[i], b = p[(i + 1) % n];
            double dx = b.x - a.x, dy = b.y - a.y;
            double l2 = dx * dx + dy * dy;
            double t = l2 > 0.0 ? ((x - a.x) * dx + (y - a.y) * dy) / l2 : 0.0;
            if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
            double ex = x - (a.x + dx * t), ey = y - (a.y + dy * t);
            if (ex * ex + ey * ey <= h * h) { ++hit; break; }
        }
    }
    return (double)hit / (double)MC_SAMPLES * (xmax - xmin) * (ymax - ymin);
}

static const char *verdict(const SAreaFit *f, double mc, double *mc_rel) {
    *mc_rel = mc > 0.0 ? fabs(mc - f->coverage) / f->coverage : 1.0;
    if (f->rel_err > ERR_LIMIT) return "REJECT err>1%";
    if (*mc_rel > ERR_LIMIT) return "REJECT mc-mismatch";
    return "ok";
}

static int run_case(const Case *c, int idx) {
    SAreaFit f;
    int rc = stroke_area_fit(c->pts, c->n, c->closed, TARGET, W_MIN, W_MAX, &f);
    printf("== 路径 %d: %s（%s，%d 点）==\n", idx + 1, c->name,
           c->closed ? "闭合" : "开放", c->n);
    if (rc != STROKE_AREA_OK) {
        printf("  拒绝 rc=%d: %s\n", rc, stroke_area_strerror(rc));
        if (f.self_intersecting)
            printf("  自交: 段 %d 与段 %d，交点 (%.4f, %.4f)\n",
                   f.self_seg_a, f.self_seg_b, f.self_x, f.self_y);
        return 1;
    }
    double mc = mc_area(c->pts, c->n, c->closed, f.width, 0x9e3779b9u + (unsigned)idx);
    double mc_rel = 0.0;
    const char *v = verdict(&f, mc, &mc_rel);
    printf("  宽度        %.6f%s\n", f.width,
           f.used_measurement ? "（实测反解）" : "（二次模型）");
    printf("  覆盖面积    %.4f\n", f.coverage);
    printf("  面积误差    %.4f%%（上限 1.00%%）%s\n", f.rel_err * 100.0,
           f.rel_err <= ERR_LIMIT ? "" : "  <-- 超限");
    printf("  耗时        %.3f ms\n", f.ms);
    printf("  中心线长    %.4f，二次项 k=%.6f\n", f.length, f.quad);
    if (f.model_width > 0.0)
        printf("  模型宽度    %.6f（模型面积 %.4f）\n", f.model_width, f.model_area);
    if (f.used_measurement)
        printf("  实测迭代    %d 次\n", f.iterations);
    if (f.self_intersecting) {
        printf("  自交       是：段 %d 与段 %d，交点 (%.4f, %.4f)，已改走实测反解\n",
               f.self_seg_a, f.self_seg_b, f.self_x, f.self_y);
    } else {
        printf("  自交       否\n");
    }
    printf("  蒙特卡洛    %.4f（与覆盖面积相对差 %.3f%%）%s\n", mc, mc_rel * 100.0,
           mc_rel <= ERR_LIMIT ? "" : "  <-- MISMATCH");
    printf("  结论        %s\n", v);
    return strcmp(v, "ok") == 0 ? 0 : 2;
}

int main(void) {
    Case cases[] = {
        { "直角折线", 0, 0, 3, {{0,0},{30,0},{30,20}} },
        { "闭合矩形", 1, 0, 4, {{0,0},{40,0},{40,20},{0,20}} },
        { "自交蝴蝶结", 0, 1, 4, {{0,0},{30,20},{30,0},{0,20}} },
        { "闭合凹多边形（L 形）", 1, 0, 6,
          {{0,0},{50,0},{50,20},{20,20},{20,50},{0,50}} },
    };
    int ncase = (int)(sizeof(cases) / sizeof(cases[0]));
    int bad = 0;
    printf("目标覆盖面积 %.2f，宽度范围 [%.2f, %.2f]\n\n", TARGET, W_MIN, W_MAX);
    for (int i = 0; i < ncase; ++i) {
        bad += run_case(&cases[i], i);
        printf("\n");
    }
    printf("%d 组路径，%d 组未通过\n", ncase, bad);
    return bad ? 1 : 0;
}
