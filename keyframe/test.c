#include "keyframe.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 三角形 (0,0)-(10,0)-(10,10)，每段一个中间点（levels=2）。
 * 两段都是直边转折，中间点只需把拐角抹圆；作用量与中点都可手算：
 * 手工最小化 S 得每段中点 (5.8333,-0.8333) 与 (10.8333,4.1667)，
 * 总作用量 266.6667，线性插值 400，降幅 33.33%。 */
static KeyFrame corner[3] = {{0,0,0},{1,10,0},{2,10,10}};

static void test_midpoint_formula(void) {
    KeyPlan p;
    int e = -1;
    CK(key_solve(corner, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_OK);
    CK(p.nnodes == 5 && p.nspans == 2);
    CK(fabs(p.spans[0].mid.x - 35.0/6.0) < 1e-12);
    CK(fabs(p.spans[0].mid.y - (-5.0/6.0)) < 1e-12);
    CK(fabs(p.spans[1].mid.x - 65.0/6.0) < 1e-12);
    CK(fabs(p.spans[1].mid.y - 25.0/6.0) < 1e-12);
    CK(fabs(p.total_action - 800.0/3.0) < 1e-12);
    CK(fabs(p.total_linear_action - 400.0) < 1e-12);
    CK(fabs(p.total_reduction - 1.0/3.0) < 1e-12);
    /* 关键帧原样保留 */
    CK(p.nodes[0].x == 0 && p.nodes[0].y == 0);
    CK(p.nodes[2].x == 10 && p.nodes[2].y == 0);
    CK(p.nodes[4].x == 10 && p.nodes[4].y == 10);
}

/* 直线运动没有可优化的加速度：最小作用量解必须与线性插值逐位相同，
 * 作用量为零，降幅为零。 */
static void test_straight_line(void) {
    KeyFrame line[3] = {{0,0,0},{2,10,0},{4,20,0}};
    KeyPlan p;
    CK(key_solve(line, 3, 4, KEY_F_MIN_ACTION, &p, 0) == KEY_OK);
    CK(p.total_action < 1e-24);
    CK(p.total_linear_action < 1e-24);
    CK(p.total_reduction == 0.0);
    for (int j = 0; j < p.nnodes; ++j) {
        KeySample s;
        CK(key_sample_at(&p, p.nodes[j].t, &s) == 0);
        CK(s.x == p.nodes[j].x && s.y == p.nodes[j].y);
    }
}

/* 时间不均匀时，节点仍按段内比例 t0 + (t1-t0)*r/levels 铺开；
 * 关键帧时刻原样落在段首节点上。 */
static void test_nonuniform_time(void) {
    KeyFrame k[4] = {{0,0,0},{0.7,10,0},{2.0,10,10},{3.1,0,10}};
    KeyPlan p;
    CK(key_solve(k, 4, 4, KEY_F_MIN_ACTION, &p, 0) == KEY_OK);
    CK(p.nnodes == 13);
    CK(p.nodes[0].t == 0.0);
    CK(p.nodes[4].t == 0.7);
    CK(p.nodes[8].t == 2.0);
    CK(p.nodes[12].t == 3.1);
    CK(p.nodes[1].t > 0.0 && p.nodes[1].t < 0.7);
    CK(p.total_action < p.total_linear_action);
}

/* 平滑解确实比线性插值的作用量低：整段与每一段都要严格更低，
 * 且总降幅不低于三成（这里取正方形折线，实测约 62%）。 */
static void test_reduction_threshold(void) {
    KeyFrame k[5] = {{0,0,0},{1,10,0},{2,10,10},{3,0,10},{4,0,0}};
    KeyPlan p;
    CK(key_solve(k, 5, 4, KEY_F_MIN_ACTION, &p, 0) == KEY_OK);
    CK(p.total_reduction > 0.30);
    for (int i = 0; i < p.nspans; ++i) {
        CK(p.spans[i].action < p.spans[i].linear_action);
        CK(p.spans[i].reduction > 0.30);
    }
    /* 段作用量之和等于整段作用量 */
    double sum = 0.0, lsum = 0.0;
    for (int i = 0; i < p.nspans; ++i) {
        sum += p.spans[i].action;
        lsum += p.spans[i].linear_action;
    }
    CK(fabs(sum - p.total_action) < 1e-9);
    CK(fabs(lsum - p.total_linear_action) < 1e-9);
}

/* 关闭新逻辑（线性插值）：中间点就是两端关键帧的中点，
 * 与作用量计算无关。 */
static void test_linear_flag(void) {
    KeyPlan p;
    CK(key_solve(corner, 3, 2, 0, &p, 0) == KEY_OK);
    CK(p.spans[0].mid.x == 5.0 && p.spans[0].mid.y == 0.0);
    CK(p.spans[1].mid.x == 10.0 && p.spans[1].mid.y == 5.0);
    CK(p.total_action == p.total_linear_action);
    CK(p.total_reduction == 0.0);
    for (int i = 0; i < p.nspans; ++i) CK(p.spans[i].reduction == 0.0);
    /* 关闭时全部节点都是线性节点 */
    CK(p.nodes[1].x == 5.0 && p.nodes[1].y == 0.0);
    CK(p.nodes[3].x == 10.0 && p.nodes[3].y == 5.0);
}

/* 时间倒置：t 相等与递减都要拒绝，err_at 指向第一个出错帧。 */
static void test_time_reversed(void) {
    KeyFrame rev[3] = {{0,0,0},{2,5,0},{1,10,0}};
    KeyPlan p;
    int e = -1;
    CK(key_solve(rev, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_TIME);
    CK(e == 2);
    CK(strstr(key_strerror(KEY_E_TIME), "strictly increasing") != NULL);

    KeyFrame eq[3] = {{0,0,0},{1,5,0},{1,10,0}};
    CK(key_solve(eq, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_TIME);
    CK(e == 2);

    KeyFrame neg[3] = {{0,0,0},{-1,5,0},{1,10,0}};
    CK(key_solve(neg, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_TIME);
    CK(e == 1);
}

/* 端点缺失：首帧或末帧坐标非有限，给出 KEY_E_ENDPOINT 与帧号；
 * 中间帧非有限走 KEY_E_NONFINITE。 */
static void test_endpoint_missing(void) {
    KeyFrame head[3] = {{0,NAN,0},{1,5,0},{2,10,0}};
    KeyPlan p;
    int e = -1;
    CK(key_solve(head, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_ENDPOINT);
    CK(e == 0);

    KeyFrame tail[3] = {{0,0,0},{1,5,0},{2,INFINITY,0}};
    CK(key_solve(tail, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_ENDPOINT);
    CK(e == 2);

    KeyFrame mid[3] = {{0,0,0},{1,NAN,0},{2,10,0}};
    CK(key_solve(mid, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_NONFINITE);
    CK(e == 1);

    /* 端点时刻非有限同样按端点缺失处理 */
    KeyFrame htime[3] = {{NAN,0,0},{1,5,0},{2,10,0}};
    CK(key_solve(htime, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_ENDPOINT);
    CK(e == 0);
}

/* 只有两帧：退化处理——不求解，节点与中点都取线性插值，
 * 两轨作用量为零，降幅按 0 报告，err 正常。 */
static void test_two_frames_degenerate(void) {
    KeyFrame two[2] = {{0,0,0},{4,8,6}};
    KeyPlan p;
    int e = -1;
    CK(key_solve(two, 2, 4, KEY_F_MIN_ACTION, &p, &e) == KEY_OK);
    CK(p.degenerate == 1);
    CK(p.nspans == 1 && p.spans[0].degenerate == 1);
    CK(p.total_action == 0.0 && p.total_linear_action == 0.0);
    CK(p.total_reduction == 0.0);
    CK(p.spans[0].mid.t == 2.0);
    CK(fabs(p.spans[0].mid.x - 4.0) < 1e-15);
    CK(fabs(p.spans[0].mid.y - 3.0) < 1e-15);
    /* 中间节点就是按比例插值 */
    CK(p.nnodes == 5);
    for (int j = 0; j <= 4; ++j) {
        double f = 0.25 * j;
        CK(fabs(p.nodes[j].x - 8.0 * f) < 1e-15);
        CK(fabs(p.nodes[j].y - 6.0 * f) < 1e-15);
    }
    /* 多帧时 degenerate 必须为 0 */
    KeyPlan q;
    CK(key_solve(corner, 3, 2, KEY_F_MIN_ACTION, &q, 0) == KEY_OK);
    CK(q.degenerate == 0 && q.spans[0].degenerate == 0);
}

/* 每段节点数从 2 到 16 全部可解：矩阵带宽与 Cholesky 在高 levels
 * 下也要稳定（levels > 3 时相邻自由节点之间隔着同一段的两个节点）。 */
static void test_all_levels(void) {
    for (int L = KEY_MIN_LEVELS; L <= KEY_MAX_LEVELS; ++L) {
        KeyPlan p;
        int e = -1;
        int rc = key_solve(corner, 3, L, KEY_F_MIN_ACTION, &p, &e);
        CK(rc == KEY_OK);
        if (rc != KEY_OK) continue;
        CK(p.nnodes == (3 - 1) * L + 1);
        CK(p.total_action < p.total_linear_action);
        /* 线性基线在同一节点布局下重算一遍：整段作用量必须等于
         * 各段线性作用量之和（与 levels 无关地成立）。 */
        double sum = 0.0;
        for (int i = 0; i < p.nspans; ++i) sum += p.spans[i].linear_action;
        CK(fabs(sum - p.total_linear_action) < 1e-9);
    }
}

/* 参数与容量错误码 */
static void test_arg_errors(void) {
    KeyPlan p;
    int e = -1;
    CK(key_solve(NULL, 3, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_ARG);
    CK(key_solve(corner, 3, 2, KEY_F_MIN_ACTION, NULL, &e) == KEY_E_ARG);
    CK(key_solve(corner, 1, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_FEW);
    CK(e == 1);
    CK(key_solve(corner, 3, 1, KEY_F_MIN_ACTION, &p, &e) == KEY_E_LEVELS);
    CK(key_solve(corner, 3, KEY_MAX_LEVELS + 1, KEY_F_MIN_ACTION, &p, &e) == KEY_E_LEVELS);
    CK(key_solve(corner, 3, 2, 0x8u, &p, &e) == KEY_E_FLAGS);
    CK(key_solve(corner, 3, 2, KEY_F_MIN_ACTION, &p, NULL) == KEY_OK);

    /* 16 帧 × 16 级 = 241 节点，正好等于 KEY_MAX_NODES */
    KeyFrame many[KEY_MAX_KEYS];
    for (int i = 0; i < KEY_MAX_KEYS; ++i) {
        many[i].t = i;
        many[i].x = (i % 2) ? 10.0 : 0.0;
        many[i].y = i * 1.0;
    }
    CK(key_solve(many, KEY_MAX_KEYS, KEY_MAX_LEVELS, KEY_F_MIN_ACTION, &p, &e) == KEY_OK);
    CK(p.nnodes == KEY_MAX_NODES);
    CK((KEY_MAX_KEYS - 1) * KEY_MAX_LEVELS + 1 == KEY_MAX_NODES);

    KeyFrame over[KEY_MAX_KEYS + 1];
    for (int i = 0; i <= KEY_MAX_KEYS; ++i) {
        over[i].t = i;
        over[i].x = i;
        over[i].y = 0;
    }
    CK(key_solve(over, KEY_MAX_KEYS + 1, 2, KEY_F_MIN_ACTION, &p, &e) == KEY_E_FEW);
    CK(e == KEY_MAX_KEYS + 1);
}

/* 时间取样：节点之间线性插值，两端外钳到端点。 */
static void test_sample_at(void) {
    KeyPlan p;
    CK(key_solve(corner, 3, 2, KEY_F_MIN_ACTION, &p, 0) == KEY_OK);
    KeySample s;
    CK(key_sample_at(&p, -1.0, &s) == 0);
    CK(s.x == 0.0 && s.y == 0.0);
    CK(key_sample_at(&p, 99.0, &s) == 0);
    CK(s.x == 10.0 && s.y == 10.0);
    CK(key_sample_at(&p, 1.0, &s) == 0);
    CK(s.x == 10.0 && s.y == 0.0);
    CK(key_sample_at(NULL, 1.0, &s) == -1);
}


int main(void) {
    test_midpoint_formula();
    test_straight_line();
    test_nonuniform_time();
    test_reduction_threshold();
    test_linear_flag();
    test_time_reversed();
    test_endpoint_missing();
    test_two_frames_degenerate();
    test_all_levels();
    test_arg_errors();
    test_sample_at();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
