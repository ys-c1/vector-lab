#include "scene.h"
#include "pipeline.h"
#include "arrow.h"
#include "export.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 独立参照：折线在弧长 target 处的切线角（所在线段方向），不调用 arrow.c */
static double ref_tangent(const RPoint *p, int n, double target) {
    double acc = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        double len = sqrt(dx * dx + dy * dy);
        if (acc + len >= target || i + 2 == n) return atan2(dy, dx);
        acc += len;
    }
    return 0.0;
}

static double ang_err(double a, double b) {
    double d = a - b;
    return fabs(atan2(sin(d), cos(d)));
}

int main(void) {
    /* L 形路径：水平 30 + 垂直 20，总长 50 */
    RPoint lp[3] = {{0,0},{30,0},{30,20}};
    RArrowSet set;
    CK(arrow_layout(lp, 3, 0.1, &set) == ARROW_OK);
    CK(set.n == 5);                       /* 间距 10，居中：s = 5,15,25,35,45 */
    CK(fabs(set.a[0].x - 5.0) < 1e-9 && fabs(set.a[0].y) < 1e-9);
    CK(fabs(set.a[2].x - 25.0) < 1e-9 && fabs(set.a[2].y) < 1e-9);
    CK(fabs(set.a[3].x - 30.0) < 1e-9 && fabs(set.a[3].y - 5.0) < 1e-9);
    CK(fabs(set.a[4].x - 30.0) < 1e-9 && fabs(set.a[4].y - 15.0) < 1e-9);
    /* 朝向误差：上限半个弧度 */
    for (int k = 0; k < set.n; ++k) {
        double ref = ref_tangent(lp, 3, (k + 0.5) * 10.0);
        CK(ang_err(set.a[k].angle, ref) <= 0.5);
    }
    CK(fabs(set.a[0].angle) < 1e-9);                 /* 水平段朝 +x */
    CK(fabs(set.a[4].angle - M_PI / 2) < 1e-9);      /* 垂直段朝 +y */

    /* 回折路径：去程朝 +x，回程必须朝 -x */
    RPoint hp[4] = {{0,0},{60,0},{60,2},{0,2}};
    CK(arrow_layout(hp, 4, 0.05, &set) == ARROW_OK);
    CK(set.n == 6);                       /* 总长 122，间距 20：s = 10..110 */
    for (int k = 0; k < set.n; ++k) {
        double ref = ref_tangent(hp, 4, (k + 0.5) * 20.0);
        CK(ang_err(set.a[k].angle, ref) <= 0.5);
    }
    CK(fabs(set.a[0].angle) < 1e-9);                 /* 去程朝 +x */
    CK(fabs(fabs(set.a[5].angle) - M_PI) < 1e-9);    /* 回程末箭头朝 -x */

    /* 明确错误码 */
    CK(arrow_layout(lp, 3, 0.0, &set) == ARROW_EDENSITY);
    CK(arrow_layout(lp, 3, -2.0, &set) == ARROW_EDENSITY);
    RPoint zp[3] = {{3,3},{3,3},{3,3}};
    CK(arrow_layout(zp, 3, 0.1, &set) == ARROW_ELENGTH);
    CK(arrow_layout(NULL, 3, 0.1, &set) == ARROW_EARGS);
    CK(arrow_layout(lp, 1, 0.1, &set) == ARROW_EARGS);
    CK(arrow_layout(lp, 3, 100.0, &set) == ARROW_EFULL);

    /* 箭头关闭：与 pipeline_render 逐字节一致（窗口外扩，留出行间空隙） */
    RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    RBox b = {-5, -5, 15, 15};
    unsigned char buf1[16 * 16], buf2[16 * 16];
    RPlace pl = {0, 0, 0, 1.0};
    CK(pipeline_render(&s, pl, b, 16, 16, buf1) == 0);
    CK(pipeline_render_arrows(&s, pl, b, 16, 16, NULL, 0, 0.1, 2.0, buf2) == 0);
    CK(memcmp(buf1, buf2, sizeof buf1) == 0);
    memset(buf2, 0xAA, sizeof buf2);
    CK(pipeline_render_arrows(&s, pl, b, 16, 16, lp, 1, 0.1, 2.0, buf2) == 0);
    CK(memcmp(buf1, buf2, sizeof buf1) == 0);

    /* 箭头开启：返回 0 且亮像素增加（路径探出方形之外）；错误码透传 */
    RPoint path[2] = {{-3,5},{13,5}};
    CK(pipeline_render_arrows(&s, pl, b, 16, 16, path, 2, 0.2, 2.0, buf2) == 0);
    CK(export_count_on(buf2, 16, 16) > export_count_on(buf1, 16, 16));
    CK(pipeline_render_arrows(&s, pl, b, 16, 16, path, 2, 0.0, 2.0, buf2) == ARROW_EDENSITY);
    CK(pipeline_render_arrows(&s, pl, b, 16, 16, zp, 3, 0.1, 2.0, buf2) == ARROW_ELENGTH);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
