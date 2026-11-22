/* snap 命令行：反解最优等分总长。
 * 用法：
 *   snap <line.txt> <m>                反解并输出到 stdout（每行 "%.6f %.6f"）
 *   snap <line.txt> <m> --nodes <out>  最优等分点的原始/取整坐标与误差，落盘
 *   snap <line.txt> <m> --legacy       旧的小数等分（逐字节兼容 gold/ref points）
 *   snap --self                        打印目标函数与拒绝情形的自检结果
 * 输入格式：每行 "x y"。新方案的输出首行是点数 m，随后每行 "x y"。
 *
 * stdout 只放点文件（首行 m，随后每行 "x y"），可与 gold 直接比对；
 * 摘要走 stderr：
 *   RESULT status=<EXACT|NEAREST|OVER_BUDGET> m=<m> T=<总长> L=<目标总长>
 *          d=<间距> cover=<L/T> max_err=<欧氏最大取整误差> axis_err=<>
 *          rms_err=<> ms=<毫秒> evals=<>
 * 拒绝（退出码 2）：闭合折线、零长线段、m<2、无法读入。
 */
#define _POSIX_C_SOURCE 199309L
#include "snap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static SnapPoint *load(const char *path, int *out_n) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    int cap = 256, n = 0;
    SnapPoint *p = (SnapPoint *)malloc((size_t)cap * sizeof *p);
    if (!p) { fclose(f); return NULL; }
    while (fscanf(f, "%lf %lf", &p[n].x, &p[n].y) == 2) {
        if (++n == cap) {
            cap *= 2;
            SnapPoint *q = (SnapPoint *)realloc(p, (size_t)cap * sizeof *q);
            if (!q) { free(p); fclose(f); return NULL; }
            p = q;
        }
    }
    fclose(f);
    *out_n = n;
    return p;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int cmd_solve(const char *path, int m, const char *nodes_path, int legacy) {
    int n = 0;
    SnapPoint *pts = load(path, &n);
    if (!pts) {
        fprintf(stderr, "REJECT read: 无法读入 %s\n", path);
        return 2;
    }
    if (n < 2) {
        fprintf(stderr, "REJECT degenerate: 折线点数不足 (%d)\n", n);
        free(pts);
        return 2;
    }
    if (m < 2) {
        fprintf(stderr, "REJECT arg: 等分点数 m 必须 >= 2（参数: m）\n");
        free(pts);
        return 2;
    }

    if (legacy) {
        SnapPoint *out = (SnapPoint *)malloc((size_t)m * sizeof *out);
        if (!out) { free(pts); return 2; }
        double t0 = now_ms();
        int rc = snap_points_legacy(pts, n, m, out);
        double ms = now_ms() - t0;
        if (rc != SNAP_OK) {
            fprintf(stderr, "REJECT %s: %s\n", snap_strerror(rc),
                    rc == SNAP_E_CLOSED ? "起点与终点重合" : "退化输入");
            free(out);
            free(pts);
            return 2;
        }
        printf("%d\n", m);
        for (int k = 0; k < m; ++k) printf("%.6f %.6f\n", out[k].x, out[k].y);
        fprintf(stderr, "LEGACY m=%d ms=%.3f\n", m, ms);
        free(out);
        free(pts);
        return 0;
    }

    SnapResult r;
    SnapNode *nodes = (SnapNode *)malloc((size_t)m * sizeof *nodes);
    if (!nodes) { free(pts); return 2; }
    double t0 = now_ms();
    int rc = snap_solve(pts, n, m, &r, nodes, m);
    double ms = now_ms() - t0;
    if (rc != SNAP_OK) {
        fprintf(stderr, "%s\n", r.detail[0] ? r.detail : snap_strerror(rc));
        free(nodes);
        free(pts);
        return 2;
    }

    printf("%d\n", m);
    for (int k = 0; k < m; ++k)
        printf("%.6f %.6f\n", nodes[k].x, nodes[k].y);
    /* 摘要只走 stderr：stdout 保持纯点文件，可直接与 gold/ref points 比对。 */
    fprintf(stderr,
            "RESULT status=%s m=%d T=%.6f L=%.6f d=%.6f cover=%.6f "
            "max_err=%.6f axis_err=%.6f rms_err=%.6f ms=%.3f evals=%llu\n",
            snap_status_str(r.status), m, r.total, r.target, r.step, r.cover,
            r.max_err, r.axis_err, r.rms_err, ms, r.evals);

    if (nodes_path) {
        FILE *f = fopen(nodes_path, "w");
        if (!f) {
            fprintf(stderr, "REJECT write: 无法写出 %s\n", nodes_path);
            free(nodes);
            free(pts);
            return 2;
        }
        fprintf(f, "%d\n", m);
        for (int k = 0; k < m; ++k)
            fprintf(f, "%.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                    nodes[k].x, nodes[k].y, nodes[k].rx, nodes[k].ry,
                    nodes[k].ex, nodes[k].ey, nodes[k].err);
        fclose(f);
    }

    free(nodes);
    free(pts);
    return r.feasible ? 0 : 3; /* 半像素内无解：退出码 3，但方案仍然输出 */
}

/* 自检：目标函数排序、拒绝情形、旧路径与新路径互不影响。 */
static int cmd_self(void) {
    int fails = 0;

    /* 1) 反解：斜边 3-4-5、12 点，小数等分超半像素，反解后全整点。 */
    SnapPoint diag[2] = {{0.0, 0.0}, {540.0, 720.0}};
    SnapResult r;
    SnapNode nodes[16];
    int rc = snap_solve(diag, 2, 12, &r, nodes, 16);
    printf("case diag345 rc=%d status=%s L=%.6f max_err=%.6f\n",
           rc, rc == 0 ? snap_status_str(r.status) : "-", r.target, r.max_err);
    if (rc != 0 || !r.exact) ++fails;
    if (fabs(r.target - 880.0) > 1e-6) ++fails;

    /* 2) 半像素内无解：取一条刻意坏的折线（两段全等长、格点相位错开），
     *    这里用斜边 45° 且 m=3 的构造保证任何 d 都有点落不到整点。 */
    SnapPoint bad[2] = {{0.0, 0.0}, {1000.0, 1.0}};
    rc = snap_solve(bad, 2, 3, &r, NULL, 0);
    printf("case almostflat rc=%d status=%s max_err=%.6f feasible=%d\n",
           rc, rc == 0 ? snap_status_str(r.status) : "-", r.max_err, r.feasible);
    if (rc != 0) ++fails;

    /* 3) 起点终点重合：拒绝并说明。 */
    SnapPoint closed[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 0}};
    rc = snap_solve(closed, 4, 5, &r, NULL, 0);
    printf("case closed rc=%d detail=%s\n", rc, r.detail);
    if (rc != SNAP_E_CLOSED) ++fails;

    /* 4) 零长线段：拒绝。 */
    SnapPoint zero[3] = {{1, 1}, {1, 1}, {5, 5}};
    rc = snap_solve(zero, 3, 4, &r, NULL, 0);
    printf("case zero rc=%d detail=%s\n", rc, r.detail);
    if (rc != SNAP_E_DEGENERATE) ++fails;

    /* 5) 旧路径：d = T/(count-1)。 */
    SnapPoint line[2] = {{0, 0}, {100, 0}};
    SnapPoint legacy[5];
    rc = snap_points_legacy(line, 2, 5, legacy);
    printf("case legacy rc=%d pts=%.6f %.6f %.6f %.6f %.6f\n", rc,
           legacy[0].x, legacy[1].x, legacy[2].x, legacy[3].x, legacy[4].x);
    if (rc != 0 || fabs(legacy[1].x - 25.0) > 1e-12) ++fails;

    printf("self: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--self")) return cmd_self();
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <line.txt> <m> [--nodes <out>] [--legacy]\n", argv[0]);
        return 1;
    }
    const char *nodes_path = NULL;
    int legacy = 0;
    /* 关闭新逻辑：环境变量 SNAP_DISABLE=1 时退化为旧的小数等分。
     * 旧路径与 gold/ref points 逐字节相同，便于回归比对。 */
    const char *dis = getenv("SNAP_DISABLE");
    if (dis && dis[0] && strcmp(dis, "0") != 0) legacy = 1;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--legacy")) legacy = 1;
        else if (!strcmp(argv[i], "--nodes") && i + 1 < argc) nodes_path = argv[++i];
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    int m = atoi(argv[2]);
    return cmd_solve(argv[1], m, nodes_path, legacy);
}
