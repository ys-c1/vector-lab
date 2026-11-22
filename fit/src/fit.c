#define _POSIX_C_SOURCE 199309L
#include "fit.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FIT_MAXN 64

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

const char *fit_status_name(int status) {
    switch (status) {
    case FIT_OK:            return "OK";
    case FIT_E_INPUT:       return "E_INPUT";
    case FIT_E_DEGENERATE:  return "E_DEGENERATE";
    case FIT_E_RESIDUAL:    return "E_RESIDUAL";
    case FIT_E_BOUNDS:      return "E_BOUNDS";
    default:                return "UNKNOWN";
    }
}

FPoint fit_apply(FTransform tr, FPoint p) {
    double c = cos(tr.rot), s = sin(tr.rot);
    FPoint r;
    r.x = c * p.x - s * p.y + tr.tx;
    r.y = s * p.x + c * p.y + tr.ty;
    return r;
}

static double norm_angle(double r) {
    while (r > M_PI) r -= 2.0 * M_PI;
    while (r <= -M_PI) r += 2.0 * M_PI;
    return r;
}

/*
 * 由最小二乘解 (a,b,tx,ty) 组装结果：
 * 刚性模型强制 a^2+b^2=1，因此把 (a,b) 投影到单位圆得到转角；
 * 解的模长 hypot(a,b) 反映数据隐含的相对缩放，仅用于越界/诊断，不进入模型。
 */
static FFitResult assemble(const FPoint *src, const FPoint *dst, int n,
                           double a, double b, double tx, double ty,
                           const FBounds *bounds) {
    FFitResult res;
    memset(&res, 0, sizeof res);
    res.conflict_a = res.conflict_b = -1;
    res.conflict_axis[0][0] = res.conflict_axis[1][0] =
        res.conflict_axis[0][1] = res.conflict_axis[1][1] = '\0';

    double mag = hypot(a, b);
    if (mag < 1e-12) {
        res.status = FIT_E_DEGENERATE;
        snprintf(res.detail, sizeof res.detail,
                 "解出的旋转分量 (a,b) 趋零，转角不可观测（方程秩不足）");
        return res;
    }
    double an = a / mag, bn = b / mag;   /* 单位化：强制刚性约束 a^2+b^2=1 */

    FTransform tr;
    tr.rot = norm_angle(atan2(bn, an));
    tr.tx = tx;
    tr.ty = ty;
    tr.scale_unused = mag;
    res.tr = tr;

    double sum2 = 0.0, worst = 0.0;
    for (int i = 0; i < n; ++i) {
        FPoint q = fit_apply(tr, src[i]);
        double dx = q.x - dst[i].x, dy = q.y - dst[i].y;
        double r = hypot(dx, dy);
        res.per_corner[i] = r;
        if (r > worst) worst = r;
        sum2 += r * r;
    }
    res.max_residual = worst;
    res.rms_residual = sqrt(sum2 / n);

    /* 未知量越界检查 */
    if (bounds) {
        if (bounds->min_scale > 0.0 &&
            (mag < bounds->min_scale || mag > bounds->max_scale)) {
            res.status = FIT_E_BOUNDS;
            snprintf(res.detail, sizeof res.detail,
                     "数据隐含相对缩放 %.6f 越界 [%.6f, %.6f]（刚性模型应为 1），拒绝该解",
                     mag, bounds->min_scale, bounds->max_scale);
            return res;
        }
        if (bounds->max_rot_abs > 0.0 && fabs(tr.rot) > bounds->max_rot_abs) {
            res.status = FIT_E_BOUNDS;
            snprintf(res.detail, sizeof res.detail,
                     "转角 %.6f rad 越界（|rot| <= %.6f），拒绝该解",
                     tr.rot, bounds->max_rot_abs);
            return res;
        }
        if (bounds->max_t_abs > 0.0 &&
            (fabs(tr.tx) > bounds->max_t_abs || fabs(tr.ty) > bounds->max_t_abs)) {
            res.status = FIT_E_BOUNDS;
            snprintf(res.detail, sizeof res.detail,
                     "平移 (%.6f, %.6f) 越界（|t| <= %.3f 像素），拒绝该解",
                     tr.tx, tr.ty, bounds->max_t_abs);
            return res;
        }
    }

    double tol = (bounds && bounds->pixel_tol > 0.0) ? bounds->pixel_tol : 0.5;
    if (worst > tol) {
        res.status = FIT_E_RESIDUAL;
        int who = 0;
        for (int i = 1; i < n; ++i)
            if (res.per_corner[i] > res.per_corner[who]) who = i;
        snprintf(res.detail, sizeof res.detail,
                 "最大残差 %.4f 像素 > 半像素容差 %.4f，超差角点 #%d",
                 worst, tol, who);
        return res;
    }

    res.status = FIT_OK;
    snprintf(res.detail, sizeof res.detail,
             "残差 %.4f 像素，隐含缩放 %.6f", worst, mag);
    return res;
}

/* 4 阶选主元高斯消元，返回秩（相对阈值判秩），解写回 rhs。 */
static int gauss4_rank(double *A, double *rhs) {
    double scale = 0.0;
    for (int i = 0; i < 16; ++i)
        if (fabs(A[i]) > scale) scale = fabs(A[i]);
    double piv_min = 1e-12 * (scale > 1.0 ? scale : 1.0);

    int rank = 0;
    for (int col = 0; col < 4; ++col) {
        int piv = rank;
        for (int i = rank + 1; i < 4; ++i)
            if (fabs(A[i * 4 + col]) > fabs(A[piv * 4 + col])) piv = i;
        if (fabs(A[piv * 4 + col]) < piv_min) continue;
        if (piv != rank) {
            for (int j = 0; j < 4; ++j) {
                double t = A[rank * 4 + j];
                A[rank * 4 + j] = A[piv * 4 + j];
                A[piv * 4 + j] = t;
            }
            double t = rhs[rank]; rhs[rank] = rhs[piv]; rhs[piv] = t;
        }
        for (int i = 0; i < 4; ++i) {
            if (i == rank) continue;
            double f = A[i * 4 + col] / A[rank * 4 + col];
            for (int j = col; j < 4; ++j) A[i * 4 + j] -= f * A[rank * 4 + j];
            rhs[i] -= f * rhs[rank];
        }
        ++rank;
    }
    return rank;
}

/* 秩不足时定位互相冲突的两条方程（角点下标 + x/y 行）。 */
static FFitResult diagnose(const FPoint *src, const FPoint *dst, int n) {
    FFitResult res;
    memset(&res, 0, sizeof res);
    res.status = FIT_E_DEGENERATE;
    res.conflict_a = res.conflict_b = -1;
    res.conflict_axis[0][0] = res.conflict_axis[1][0] =
        res.conflict_axis[0][1] = res.conflict_axis[1][1] = '\0';
    res.tr.scale_unused = 1.0;

    /* 确认源角点是否全部重合（三未知量刚性模型唯一的秩亏来源） */
    double spread = 0.0;
    for (int i = 1; i < n; ++i)
        spread = fmax(spread, hypot(src[i].x - src[0].x, src[i].y - src[0].y));

    if (spread < 1e-9) {
        /*
         * 源角点重合 ⇒ 所有 x 行的系数 [x,-y,1] 完全相同、所有 y 行亦然，
         * 设计矩阵秩为 2，转角 a,b 无约束。若落点不一致，则同系数行的右侧
         * 互相矛盾：取落点分歧最大的两个角点作为冲突方程。
         */
        int ci = 0, cj = 1;
        double dworst = -1.0;
        const char *ax = "x";
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                double ddx = fabs(dst[j].x - dst[i].x);
                double ddy = fabs(dst[j].y - dst[i].y);
                double d = hypot(dst[j].x - dst[i].x, dst[j].y - dst[i].y);
                if (d > dworst) { dworst = d; ci = i; cj = j; ax = (ddx >= ddy) ? "x" : "y"; }
            }
        res.conflict_a = ci; res.conflict_b = cj;
        strcpy(res.conflict_axis[0], ax);
        strcpy(res.conflict_axis[1], ax);
        snprintf(res.detail, sizeof res.detail,
                 "方程退化（设计矩阵秩=2<4）：源角点全部重合，转角无约束；角点 #%d 的 %s 行与角点 #%d 的 %s 行右侧冲突（落点相差 %.4f 像素）",
                 ci, ax, cj, ax, dworst);
        return res;
    }

    /* 理论上到不了这里：非重合点集对该模型总是满秩 */
    res.conflict_a = 0; res.conflict_b = 1;
    strcpy(res.conflict_axis[0], "x");
    strcpy(res.conflict_axis[1], "x");
    snprintf(res.detail, sizeof res.detail,
             "方程退化：法方程数值秩不足（主元过小），角点 #0 的 x 行与角点 #1 的 x 行近线性相关");
    return res;
}

/* 4 列设计矩阵的法方程最小二乘。返回数值秩（4=满秩，2=源点重合）。 */
static int lstsq4(const FPoint *src, const FPoint *dst, int n, double u[4]) {
    double M[16] = {0}, v[4] = {0};
    for (int i = 0; i < n; ++i) {
        double x = src[i].x, y = src[i].y;
        double rx[2] = {dst[i].x, dst[i].y};
        double rows[2][4] = {{ x, -y, 1.0, 0.0 }, { y,  x, 0.0, 1.0 }};
        for (int q = 0; q < 2; ++q)
            for (int r = 0; r < 4; ++r) {
                v[r] += rows[q][r] * rx[q];
                for (int c = 0; c < 4; ++c)
                    M[r * 4 + c] += rows[q][r] * rows[q][c];
            }
    }
    int rank = gauss4_rank(M, v);
    for (int i = 0; i < 4; ++i) u[i] = (rank == 4) ? v[i] / M[i * 4 + i] : 0.0;
    return rank;
}

/*
 * 共享前置闸：两条求解路径都先过这道闸，保证退化/越界判定逐字节一致。
 * 输出 out_ls 为 4x4 最小二乘解（良态时有效）。
 * 返回 1 表示已给出最终判定（调用方直接返回 res），0 表示良态、可继续。
 */
static int precheck(const FPoint *src, const FPoint *dst, int n,
                    const FBounds *bounds, double u[4], FFitResult *res) {
    double t0 = now_ns();
    int rank = lstsq4(src, dst, n, u);
    double t1 = now_ns();

    if (rank < 4) {
        *res = diagnose(src, dst, n);
        res->solve_ns = t1 - t0;
        return 1;
    }

    double mag = hypot(u[0], u[1]);
    if (bounds && bounds->min_scale > 0.0 &&
        (mag < bounds->min_scale || mag > bounds->max_scale)) {
        memset(res, 0, sizeof *res);
        res->conflict_a = res->conflict_b = -1;
        res->status = FIT_E_BOUNDS;
        res->tr.scale_unused = mag;
        snprintf(res->detail, sizeof res->detail,
                 "数据隐含相对缩放 %.6f 越界 [%.6f, %.6f]（刚性模型应为 1），拒绝该解",
                 mag, bounds->min_scale, bounds->max_scale);
        res->solve_ns = t1 - t0;
        return 1;
    }
    res->solve_ns = t1 - t0;
    return 0;
}

FFitResult fit_solve_linear(const FPoint *src, const FPoint *dst, int n,
                            const FBounds *bounds) {
    FFitResult bad;
    memset(&bad, 0, sizeof bad);
    bad.conflict_a = bad.conflict_b = -1;
    if (!src || !dst || n < 2 || n > FIT_MAXN) {
        bad.status = FIT_E_INPUT;
        snprintf(bad.detail, sizeof bad.detail, "入参非法：需要 2..%d 组角点", FIT_MAXN);
        return bad;
    }

    double u[4];
    FFitResult gate;
    if (precheck(src, dst, n, bounds, u, &gate)) return gate;

    FFitResult res = assemble(src, dst, n, u[0], u[1], u[2], u[3], bounds);
    res.solve_ns = gate.solve_ns;
    return res;
}

FFitResult fit_solve_trial(const FPoint *src, const FPoint *dst, int n,
                           const FBounds *bounds) {
    FFitResult bad;
    memset(&bad, 0, sizeof bad);
    bad.conflict_a = bad.conflict_b = -1;
    if (!src || !dst || n < 2 || n > FIT_MAXN) {
        bad.status = FIT_E_INPUT;
        snprintf(bad.detail, sizeof bad.detail, "入参非法：需要 2..%d 组角点", FIT_MAXN);
        return bad;
    }

    double u[4];
    FFitResult gate;
    if (precheck(src, dst, n, bounds, u, &gate)) return gate;   /* 退化/越界口径与线性路径一致 */

    double t0 = now_ns();

    /* 良态：旧的逐个试逻辑只做转角网格寻优，平移由质心对齐解析给出 */
    double pcx = 0, pcy = 0, qcx = 0, qcy = 0;
    for (int i = 0; i < n; ++i) {
        pcx += src[i].x; pcy += src[i].y;
        qcx += dst[i].x; qcy += dst[i].y;
    }
    pcx /= n; pcy /= n; qcx /= n; qcy /= n;

    double lo = -M_PI, hi = M_PI;
    double best_rot = 0.0;
    enum { COARSE = 72, REFINES = 6, SUB = 25 };

    for (int round = 0; round < REFINES + 1; ++round) {
        int steps = (round == 0) ? COARSE : SUB;
        double rbest = best_rot, ebest = -1.0;
        for (int k = 0; k <= steps; ++k) {
            double rot = lo + (hi - lo) * k / steps;
            double c = cos(rot), s = sin(rot);
            double tx = qcx - (c * pcx - s * pcy);
            double ty = qcy - (s * pcx + c * pcy);
            double err = 0.0;
            for (int i = 0; i < n; ++i) {
                double X = c * src[i].x - s * src[i].y + tx;
                double Y = s * src[i].x + c * src[i].y + ty;
                double dx = X - dst[i].x, dy = Y - dst[i].y;
                err += dx * dx + dy * dy;
            }
            if (ebest < 0.0 || err < ebest) { ebest = err; rbest = rot; }
        }
        best_rot = rbest;
        double half = (hi - lo) / steps;
        lo = rbest - half;
        hi = rbest + half;
    }

    double c = cos(best_rot), s = sin(best_rot);
    double tx = qcx - (c * pcx - s * pcy);
    double ty = qcy - (s * pcx + c * pcy);
    double t1 = now_ns();

    /* 与线性路径共用组装/判定，保证两条路径产物口径一致 */
    FFitResult res = assemble(src, dst, n, c, s, tx, ty, bounds);
    res.solve_ns = t1 - t0;
    return res;
}

/* 编译期开关：默认走新的线性方程组逻辑；定义 FIT_NO_LINEAR 则回退到逐个试。 */
FFitResult fit_solve(const FPoint *src, const FPoint *dst, int n,
                     const FBounds *bounds) {
#ifdef FIT_NO_LINEAR
    return fit_solve_trial(src, dst, n, bounds);
#else
    return fit_solve_linear(src, dst, n, bounds);
#endif
}
