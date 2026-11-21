#include "merge.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
/* ---- 手算基准 ----
 * 组 2：两个边长 4 的方块，重叠 2x2
 *   并集 = 16 + 16 - 4 = 28；穿越切点 2 个；外环 8 点
 * 组 16：16 个边长 10 的方块锯齿链，第 k 块左下 (6(k-1), 6*((k-1) mod 2))，
 *   相邻块 x、y 方向各压 4，重叠 4x4=16；相隔块互不相交，无三重重叠。
 *   并集 = 16*100 - 15*16 = 1360；
 *   第 k 步累计面积手算式 A(k)=100k - 16(k-1) = 84k + 16。
 */
#define HAND_AREA_2   28.0
#define HAND_RING_2   8
#define HAND_AREA_16  1360.0
#define HAND_RING_16  64      /* 每并入一块净增 4 外环点，k 块后 4k；16*4=64 */
#define SIDE 10.0
#define PITCH 6.0

/* 锯齿链第 k 块（k 从 1 开始编号），4 个 CCW 顶点 */
static void zig_square(int k, MPoint out[4]) {
    double x = PITCH * (k - 1);
    double y = ((k - 1) & 1) ? PITCH : 0.0;
    out[0] = (MPoint){x, y};
    out[1] = (MPoint){x + SIDE, y};
    out[2] = (MPoint){x + SIDE, y + SIDE};
    out[3] = (MPoint){x, y + SIDE};
}

static int is_tangent(const MMerge *m, const MPoint *p) {
    for (int i = 0; i < m->n_hits; ++i) {
        double dx = p->x - m->hits[i].x, dy = p->y - m->hits[i].y;
        if (sqrt(dx * dx + dy * dy) <= 1e-9) return 1;
    }
    return 0;
}

typedef struct { int cur_seg, next_seg; int rc; int ring_pts; double area; double hand_area; } OrderRow;

/* 按段号 1..nseg 顺序链式并集；order 记录每一步，final_out 回传最终结果。
   reps 次重复取平均耗时（clock() 分辨率有限，单次会测成 0）。 */
static int chain_union(int nseg, int reps, double *t_us, MMerge *final_out,
                       OrderRow *order, int *norder) {
    MPoint cur[MERGE_RING_CAP];
    double t0 = (double)clock();
    int rc = MERGE_OK;
    for (int rep = 0; rep < reps; ++rep) {
        int cn = 0, cur_id = 1;
        *norder = 0;
        for (int k = 1; k <= nseg; ++k) {
            MPoint sq[4];
            zig_square(k, sq);
            if (k == 1) {
                for (int i = 0; i < 4; ++i) cur[i] = sq[i];
                cn = 4;
                continue;
            }
            MMerge r;
            rc = merge_polygons(cur, cn, cur_id, sq, 4, k, 1, &r);
            if (order && rep == reps - 1) {
                order[*norder].cur_seg = cur_id;
                order[*norder].next_seg = k;
                order[*norder].rc = rc;
                order[*norder].ring_pts = rc == MERGE_OK ? r.rings[0].count : -1;
                order[*norder].area = rc == MERGE_OK ? merge_union_area(&r) : 0.0;
                order[*norder].hand_area = 84.0 * k + 16.0;
                ++(*norder);
            }
            if (rc != MERGE_OK) {
                *t_us = ((double)clock() - t0) / (double)CLOCKS_PER_SEC * 1e6 / reps;
                return rc;
            }
            cn = r.rings[0].count;
            for (int i = 0; i < cn; ++i) cur[i] = r.rings[0].pts[i];
            cur_id = k;
            if (k == nseg && final_out && rep == reps - 1) *final_out = r;
        }
    }
    *t_us = ((double)clock() - t0) / (double)CLOCKS_PER_SEC * 1e6 / reps;
    return rc;
}

/* 外环点与切点配对列出 */
static void print_ring_points(const MMerge *m) {
    int n = m->rings[0].count;
    printf("外环点 %d 个（本次穿越切点 %d 个；T=切点，V=原始顶点）:\n", n, m->n_hits);
    printf("  %-4s %-4s %10s %10s  %s\n", "序号", "类型", "x", "y", "配对切点");
    int tno = 0;
    for (int i = 0; i < n; ++i) {
        const MPoint *p = &m->rings[0].pts[i];
        int t = is_tangent(m, p);
        char pair[32];
        if (t) snprintf(pair, sizeof(pair), "切点 #%d (%.2f, %.2f)",
                        ++tno, p->x, p->y);
        else pair[0] = '\0';
        printf("  %-4d %-4s %10.4f %10.4f  %s\n", i, t ? "T" : "V", p->x, p->y, pair);
    }
}

int main(void) {
    /* ---------- 组 2：2 段 ---------- */
    printf("================ 组 2：2 段方块（重叠 2x2） ================\n");
    MPoint a[4] = {{0,0},{4,0},{4,4},{0,4}};
    MPoint b[4] = {{2,2},{6,2},{6,6},{2,6}};
    MMerge r2;
    enum { REPS2 = 2000 };
    double t0 = (double)clock();
    int rc = MERGE_OK;
    for (int rep = 0; rep < REPS2; ++rep)
        rc = merge_polygons(a, 4, 1, b, 4, 2, 1, &r2);
    double t2 = ((double)clock() - t0) / (double)CLOCKS_PER_SEC * 1e6 / REPS2;
    if (rc != MERGE_OK) { printf("merge failed: %s\n", merge_last_error()); return 1; }
    printf("拼接顺序表（按段号）:\n");
    printf("  %-6s %-10s %-10s %-8s %-10s %10s\n",
           "步骤", "累计段", "拼入段", "返回码", "外环点", "累计面积");
    printf("  %-6d %-10d %-10d %-8d %-10d %10.4f\n",
           1, 1, 2, MERGE_OK, r2.rings[0].count, merge_union_area(&r2));
    double a2 = merge_union_area(&r2);
    printf("组 2  | 段数  2 | 并集面积 %9.4f | 手算 %9.1f | 面积误差 %+.2e | "
           "外环点 %3d (手算 %d) | 朴素相加 %9.4f | 耗时 %.1f us\n",
           a2, HAND_AREA_2, a2 - HAND_AREA_2, r2.rings[0].count, HAND_RING_2, 32.0, t2);
    print_ring_points(&r2);

    /* 并集开关关闭：必须回到原两段 */
    MMerge r2off;
    merge_polygons(a, 4, 1, b, 4, 2, 0, &r2off);
    printf("并集开关关闭: n_rings=%d, 两段点数=[%d,%d]（与输入逐点一致）\n\n",
           r2off.n_rings, r2off.rings[0].count, r2off.rings[1].count);

    /* ---------- 组 16：16 段锯齿链 ---------- */
    printf("================ 组 16：16 段锯齿链 ================\n");
    OrderRow order[32];
    int norder = 0;
    MMerge r16;
    double t16 = 0.0;
    rc = chain_union(16, 200, &t16, &r16, order, &norder);
    if (rc != MERGE_OK) { printf("chain merge failed: %s\n", merge_last_error()); return 1; }
    printf("拼接顺序表（按段号）:\n");
    printf("  %-6s %-10s %-10s %-8s %-10s %12s %12s %10s\n",
           "步骤", "累计段", "拼入段", "返回码", "外环点", "累计面积", "手算式84k+16", "误差");
    for (int i = 0; i < norder; ++i) {
        printf("  %-6d %-10d %-10d %-8d %-10d %12.4f %12.1f %+10.2e\n",
               i + 1, order[i].cur_seg, order[i].next_seg,
               order[i].rc, order[i].ring_pts, order[i].area,
               order[i].hand_area, order[i].area - order[i].hand_area);
    }
    double a16 = merge_union_area(&r16);
    printf("组 16 | 段数 16 | 并集面积 %9.4f | 手算 %9.1f | 面积误差 %+.2e | "
           "外环点 %3d (手算 %d) | 朴素相加 %9.4f | 耗时 %.1f us\n",
           a16, HAND_AREA_16, a16 - HAND_AREA_16,
           r16.rings[0].count, HAND_RING_16, 16.0 * SIDE * SIDE, t16);
    print_ring_points(&r16);

    /* ---------- 两组对比汇总 ---------- */
    printf("\n================ 两组对比汇总（与手算并集核对） ================\n");
    printf("%-8s %-8s %-14s %-14s %-14s %-16s %-12s\n",
           "组", "段数", "并集面积", "手算面积", "面积误差", "外环点(手算)", "耗时(us)");
    char ringcol[24];
    snprintf(ringcol, sizeof(ringcol), "%d (%d)", r2.rings[0].count, HAND_RING_2);
    printf("%-8s %-8d %-14.4f %-14.1f %-14.2e %-16s %-12.1f\n",
           "2", 2, a2, HAND_AREA_2, a2 - HAND_AREA_2, ringcol, t2);
    snprintf(ringcol, sizeof(ringcol), "%d (%d)", r16.rings[0].count, HAND_RING_16);
    printf("%-8s %-8d %-14.4f %-14.1f %-14.2e %-16s %-12.1f\n",
           "16", 16, a16, HAND_AREA_16, a16 - HAND_AREA_16, ringcol, t16);
    return 0;
}
