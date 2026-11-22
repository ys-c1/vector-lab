#define _POSIX_C_SOURCE 199309L
#include "keyframe.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 四段动作：都是四段（五帧）的平面运动，每段都有明显转向，
 * 中间点要同时照顾前后两段的加速度。levels=4 时每段三个内部节点、
 * 中点正好落在节点上。 */
typedef struct {
    const char *name;
    KeyFrame k[5];
} Clip;

static Clip clips[4];

static void init_clips(void) {
    const KeyFrame sq[5] = {{0,0,0},{1,10,0},{2,10,10},{3,0,10},{4,0,0}};
    const KeyFrame arc[5] = {{0,0,0},{1,20,5},{2,30,25},{3,20,45},{4,0,50}};
    /* 正五边形，从正上方起逆时针；闭合成最后一段回到起点。 */
    KeyFrame pent[5];
    for (int i = 0; i < 5; ++i) {
        double a = -M_PI / 2.0 + 2.0 * M_PI * i / 5.0;
        pent[i].t = i;
        pent[i].x = 20.0 * cos(a);
        pent[i].y = 20.0 * sin(a);
    }
    /* 60° 之字形：左、右上、右下、左上、左下。 */
    const KeyFrame zig[5] = {
        {0,0,0},{1,10,0},
        {2,5,8.660254037844387},{3,-5,8.660254037844387},{4,-10,0}
    };
    clips[0].name = "square";
    clips[1].name = "arc";
    clips[2].name = "pent";
    clips[3].name = "zig";
    for (int i = 0; i < 5; ++i) {
        clips[0].k[i] = sq[i];
        clips[1].k[i] = arc[i];
        clips[2].k[i] = pent[i];
        clips[3].k[i] = zig[i];
    }
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec * 1e-3;
}

/* 独立核对：按节点表用 long double 重算某段作用量份额，与库内结果
 * 比对。公式与库内相同（速度差分 + 中心差分 + 梯形权），但用 80 位
 * 浮点，差异只应来自舍入。段内自由节点全计入，两端关键帧节点被相邻
 * 两段共用，各记一半。 */
static double hand_action(const KeyPlan *p, int from_key) {
    int L = p->levels;
    int lo = from_key * L, hi = (from_key + 1) * L;
    long double S = 0.0L;
    for (int k = lo; k <= hi; ++k) {
        if (k <= 0 || k >= p->nnodes - 1) continue;
        long double hL = (long double)(p->nodes[k].t - p->nodes[k-1].t);
        long double hR = (long double)(p->nodes[k+1].t - p->nodes[k].t);
        long double s = 2.0L / (hL + hR);
        long double w = 0.5L * (hL + hR);
        long double ax = s * (((long double)p->nodes[k+1].x - p->nodes[k].x) / hR
                            - ((long double)p->nodes[k].x - p->nodes[k-1].x) / hL);
        long double ay = s * (((long double)p->nodes[k+1].y - p->nodes[k].y) / hR
                            - ((long double)p->nodes[k].y - p->nodes[k-1].y) / hL);
        long double term = w * (ax * ax + ay * ay);
        S += (k == lo || k == hi) ? 0.5L * term : term;
    }
    return (double)S;
}

int main(void) {
    init_clips();
    const int levels = 4;

    printf("最小作用量：加速度平方沿时间积分 S = ∫|q''|^2 dt，"
           "每段 levels=%d 个间隔（3 个内部节点）\n\n", levels);
    printf("%-8s %-6s %10s %10s %10s %9s %9s %10s\n",
           "动作", "段", "中间点 x", "中间点 y", "作用量S", "线性S", "降幅%",
           "手算S差");

    int bad = 0;
    double worst_hand = 0.0;
    FILE *csv = fopen("keyframe_spans.csv", "w");
    if (csv) fprintf(csv, "clip,span,from,to,mid_t,mid_x,mid_y,action,linear_action,reduction\n");

    for (int c = 0; c < 4; ++c) {
        KeyPlan p;
        int e = -1;
        int rc = key_solve(clips[c].k, 5, levels, KEY_F_MIN_ACTION, &p, &e);
        if (rc != KEY_OK) {
            printf("%-8s rc=%d (%s) err_at=%d\n", clips[c].name, rc,
                   key_strerror(rc), e);
            bad = 1;
            continue;
        }
        if (p.total_reduction < 0.30) {
            printf("%-8s 整段降幅 %.3f%% 低于三成\n", clips[c].name,
                   100.0 * p.total_reduction);
            bad = 1;
        }
        for (int i = 0; i < p.nspans; ++i) {
            const KeySpan *sp = &p.spans[i];
            if (sp->reduction < 0.30) {
                printf("%-8s 第 %d 段降幅 %.3f%% 低于三成\n",
                       clips[c].name, i, 100.0 * sp->reduction);
                bad = 1;
            }
            double hand = hand_action(&p, i);
            double diff = fabs(hand - sp->action);
            if (diff > worst_hand) worst_hand = diff;
            printf("%-8s 段%-4d %10.6f %10.6f %10.6f %10.6f %9.3f %10.3e\n",
                   clips[c].name, i, sp->mid.x, sp->mid.y, sp->action,
                   sp->linear_action, 100.0 * sp->reduction, diff);
            if (csv)
                fprintf(csv, "%s,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                        clips[c].name, i, sp->from, sp->to, sp->mid.t,
                        sp->mid.x, sp->mid.y, sp->action, sp->linear_action,
                        sp->reduction);
        }
        double hand_total = 0.0;
        for (int i = 0; i < p.nspans; ++i) hand_total += hand_action(&p, i);
        /* 单次求解耗时（重复 2000 次取平均）。 */
        const int reps = 2000;
        double t0 = now_us();
        for (int r = 0; r < reps; ++r) {
            KeyPlan tmp;
            (void)key_solve(clips[c].k, 5, levels, KEY_F_MIN_ACTION, &tmp, 0);
        }
        double us_each = (now_us() - t0) / reps;
        printf("%-8s 合计 %31.6f %10.6f %9.3f%%   单次 %.3f us\n\n",
               clips[c].name, p.total_action, p.total_linear_action,
               100.0 * p.total_reduction, us_each);
        if (fabs(hand_total - p.total_action) > 1e-9) {
            printf("  合计手算偏差 %.3e 超限\n", fabs(hand_total - p.total_action));
            bad = 1;
        }
    }
    if (csv) {
        fclose(csv);
        printf("逐段表已导出 keyframe_spans.csv\n\n");
    }

    /* 耗时对比：四个动作各解一次为一组，重复 2000 组取平均；
     * 线性路径同测。 */
    const int reps = 2000;
    double t0 = now_us();
    for (int r = 0; r < reps; ++r) {
        KeyPlan p;
        for (int c = 0; c < 4; ++c)
            (void)key_solve(clips[c].k, 5, levels, KEY_F_MIN_ACTION, &p, 0);
    }
    double us_smooth = (now_us() - t0) / reps / 4.0;
    t0 = now_us();
    for (int r = 0; r < reps; ++r) {
        KeyPlan p;
        for (int c = 0; c < 4; ++c)
            (void)key_solve(clips[c].k, 5, levels, 0u, &p, 0);
    }
    double us_linear = (now_us() - t0) / reps / 4.0;

    /* 两帧退化：中点就是线性中点，作用量为零。 */
    KeyFrame two[2] = {{0,0,0},{4,8,6}};
    KeyPlan p2;
    int e2 = -1;
    int rc2 = key_solve(two, 2, levels, KEY_F_MIN_ACTION, &p2, &e2);
    printf("两帧退化：rc=%d degenerate=%d 中点(%.6f,%.6f) 作用量 %.3e/%.3e\n",
           rc2, p2.degenerate, p2.spans[0].mid.x, p2.spans[0].mid.y,
           p2.total_action, p2.total_linear_action);

    /* 拒绝路径回显（演示错误信息与帧号）。 */
    KeyFrame rev[3] = {{0,0,0},{2,5,0},{1,10,0}};
    KeyFrame hole[3] = {{0,0,0},{1,5,0},{2,NAN,0}};
    KeyPlan pr;
    int er = -1;
    int r1 = key_solve(rev, 3, levels, KEY_F_MIN_ACTION, &pr, &er);
    printf("时间倒置：rc=%d err_at=%d (%s)\n", r1, er, key_strerror(r1));
    int r2 = key_solve(hole, 3, levels, KEY_F_MIN_ACTION, &pr, &er);
    printf("端点缺失：rc=%d err_at=%d (%s)\n", r2, er, key_strerror(r2));

    printf("\n耗时/次：最小作用量 %.3f us，线性插值 %.3f us（%.1fx）\n",
           us_smooth, us_linear,
           us_linear > 0.0 ? us_smooth / us_linear : 0.0);
    printf("手算作用量最大偏差 %.3e\n", worst_hand);

    return bad ? 1 : 0;
}
