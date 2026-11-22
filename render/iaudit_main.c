/*
 * 四组场景的区间审计驱动：
 *   1 basic        单位正方形，普通旋转/缩放，健康基线
 *   2 bigcoords    坐标放大到 1e8，误差沿仿射链逐级放大
 *   3 cancel       窗口两端同量级相减（灾难性抵消），上界应超画面一成
 *   4 truncate     顶点坐标小到乘法下溢，区间被截断为零宽度，必须拒绝
 *
 * 每组按阶段（trig/affine/window/sample）报告：区间宽度、最坏偏差上界、
 * 经验偏差、包含性、像素上界，以及整组耗时。
 */
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/iaudit.h"
#include <math.h>
#include <stdio.h>

static void square(RScene *s, double ox, double oy, double side) {
    scene_init(s);
    RPoint sq[4] = {{ox, oy}, {ox + side, oy},
                    {ox + side, oy + side}, {ox, oy + side}};
    scene_add_poly(s, sq, 4);
}

static int print_report(const char *name, const IAReport *rep,
                        int w, int h) {
    printf("scene %-9s %dx%d  tenth=%.1fpx  time %.4f ms\n",
           name, w, h, rep->tenth_px, rep->elapsed_ms);
    if (rep->rejected) {
        printf("  REJECT at stage %s: %s\n",
               ia_stage_name(rep->fail_stage),
               iv_strerror(rep->fail_status));
        return 0;
    }
    int bad = 0;
    printf("  %-8s %14s %14s %14s %9s %10s\n",
           "stage", "iv_width", "bound", "worst(emp)", "contain",
           "bound(px)");
    for (int st = 0; st < IA_NSTAGES; ++st) {
        const IAStageResult *r = &rep->stages[st];
        printf("  %-8s %14.3e %14.3e %14.3e %9s %10.3e\n",
               ia_stage_name((IAStage)st), r->max_width, r->max_bound,
               r->worst_dev, r->contained ? "yes" : "NO", r->max_pixbd);
        if (!r->contained) {
            printf("  !! worst deviation escapes interval bound at %s\n",
                   ia_stage_name((IAStage)st));
            bad = 1;
        }
    }
    if (rep->over_tenth)
        printf("  WARN interval bound exceeds 10%% of screen "
               "(%.1fpx > %.1fpx)\n",
               rep->stages[IA_SAMPLE].max_pixbd, rep->tenth_px);
    return bad;
}

int main(void) {
    const int W = 64, H = 64;
    int rc = 0;

    /* 1 basic */
    {
        RScene s; square(&s, 0.0, 0.0, 10.0);
        RBox win = {0.0, 0.0, 10.0, 10.0};
        RPlace place = {0.0, 0.0, 0.5, 1.0};
        IAReport rep;
        if (iaudit_run(&s, place, &win, W, H, &rep) != 0) rc = 1;
        rc |= print_report("basic", &rep, W, H);
    }
    /* 2 bigcoords：1e8 量级坐标，旋转让每条链都走完整套运算；
          该量级 ulp 约 1.5e-8，仿射链末端误差被放大到数千 ulp，
          但仍未压到零宽度（截断留给场景 4）。 */
    {
        RScene s; square(&s, 1.0e8, 1.0e8, 1.0e8);
        RBox win = {0.0, 0.0, 3.0e8, 3.0e8};
        RPlace place = {0.0, 0.0, 0.5, 2.0};
        IAReport rep;
        if (iaudit_run(&s, place, &win, W, H, &rep) != 0) rc = 1;
        rc |= print_report("bigcoords", &rep, W, H);
    }
    /* 3 cancel：1e15 附近相差 10 的窗口两端，相减相对误差约 1.4e-2，
          采样点像素上界被推过画面一成（6.4px）。 */
    {
        RScene s; square(&s, 0.0, 0.0, 10.0);
        RBox win = {1.0e15, 1.0e15, 1.0e15 + 10.0, 1.0e15 + 10.0};
        RPlace place = {0.0, 0.0, 0.0, 1.0};
        IAReport rep;
        if (iaudit_run(&s, place, &win, W, H, &rep) != 0) rc = 1;
        rc |= print_report("cancel", &rep, W, H);
        if (!rep.over_tenth) {
            printf("  !! expected over-tenth warning did not fire\n");
            rc = 1;
        }
    }
    /* 4 truncate：顶点取最小次正规数的 2 倍（其 ±1 ulp 区间严格为正），
          再乘 0.25 缩放，精确乘积小于 DBL_TRUE_MIN 被舍成 0：
          区间在 affine 的乘法步被截断，整组必须拒绝并指出失败步骤。 */
    {
        double tiny = nextafter(4.9406564584124654e-324, INFINITY);
        RScene s; square(&s, tiny, tiny, tiny);
        RBox win = {0.0, 0.0, 1.0, 1.0};
        RPlace place = {0.0, 0.0, 0.0, 0.25};
        IAReport rep;
        int got = iaudit_run(&s, place, &win, W, H, &rep);
        rc |= print_report("truncate", &rep, W, H);
        if (!(got != 0 && rep.rejected &&
              rep.fail_status == IVL_TRUNC &&
              rep.fail_stage == IA_AFFINE)) {
            printf("  !! expected affine-stage truncation rejection\n");
            rc = 1;
        }
    }
    return rc;
}
