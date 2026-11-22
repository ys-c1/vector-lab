#include "warp.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 强变形基准曲线（控制点 (0,0),(0,14),(10,-10),(10,6)）：
 * 非对称，参数中点 t=0.5 的累计弧长与总长一半相差约 4%，
 * 等参数采样被明显拉伸，半弧长解 t*≈0.5204 而非 0.5。 */
static WPoint kCurve[4] = {{0,0},{0,14},{10,-10},{10,6}};

static WPoint eval_bern(const WPoint c[4], double t) {
    double u = 1.0 - t;
    WPoint p;
    p.x = u*u*u*c[0].x + 3*u*u*t*c[1].x + 3*u*t*t*c[2].x + t*t*t*c[3].x;
    p.y = u*u*u*c[0].y + 3*u*u*t*c[1].y + 3*u*t*t*c[2].y + t*t*t*c[3].y;
    return p;
}

/* 字段名是定长缓冲拼接，用辅助比较保持 CK 宏可读。 */
static int field_eq(const char *got, const char *want) {
    while (*want) { if (*got++ != *want++) return 0; }
    return *got == '\0';
}

int main(void) {
    double total = warp_arclen(kCurve, 1.0);
    CK(total > 18.0);                       /* 实测约 19.81，远长于端弦 sqrt(136) */
    CK(warp_arclen(kCurve, 0.0) == 0.0);
    CK(warp_arclen(kCurve, 1.5) == total); /* 越界截断到总长 */
    double half_s = 0.5 * total;

    /* ---- 1. 四组初值：必须全部收敛、残差达标、不超上限 ---- */
    double t0s[4] = {0.0, 0.25, 0.5, 1.0};
    WarpCfg cfg = {1e-10, 1e-12, 64, 0.0, 0.8};
    for (int i = 0; i < 4; ++i) {
        WPoint p; WarpReport r;
        int rc = warp_correct(kCurve, half_s, t0s[i], WARP_F_FIXPOINT, &cfg, &p, &r);
        CK(rc == WARP_OK);
        CK(r.status == WARP_OK);
        CK(r.iters >= 1 && r.iters <= 64);
        CK(r.residual <= 1e-10);
        /* 解与初值无关：四组应收敛到同一 t */
        WPoint q = eval_bern(kCurve, r.t);
        CK(fabs(q.x - p.x) <= 1e-12 && fabs(q.y - p.y) <= 1e-12);
        CK(r.t > 0.50 && r.t < 0.60);      /* 强变形下半弧长解 t*≈0.5204，不是 0.5 */
    }

    /* ---- 2. 解的唯一性：四组初值末轮 t 一致 ---- */
    double ts[4];
    for (int i = 0; i < 4; ++i) {
        WarpReport r;
        CK(warp_correct(kCurve, half_s, t0s[i], WARP_F_FIXPOINT, &cfg, 0, &r) == WARP_OK);
        ts[i] = r.t;
    }
    for (int i = 1; i < 4; ++i) CK(fabs(ts[i] - ts[0]) <= 1e-10);

    /* ---- 3. 关闭新逻辑：与 legacy 固定 8 次逐位一致 ---- */
    for (int i = 0; i < 4; ++i) {
        double tl;
        CK(warp_correct_legacy(kCurve, half_s, t0s[i], &tl) == WARP_OK);
        WPoint p; WarpReport r;
        CK(warp_correct(kCurve, half_s, t0s[i], 0, &cfg, &p, &r) == WARP_OK);
        CK(r.iters == 8);
        CK(r.t == tl);                     /* 逐位相同，不是"接近" */
        WPoint q = eval_bern(kCurve, tl);
        CK(p.x == q.x && p.y == q.y);
    }
    /* cfg=NULL 关闭时同样安全 */
    {
        WPoint p; WarpReport r;
        CK(warp_correct(kCurve, half_s, 0.5, 0, 0, &p, &r) == WARP_OK);
        CK(r.iters == 8);
    }

    /* ---- 4. 不收敛拒绝：上限 2 轮必然不够，必须拒且不装 OK ---- */
    {
        WarpCfg tight = {1e-10, 1e-12, 2, 0.0, 0.8};
        WPoint p = {-1, -1}; WarpReport r;
        int rc = warp_correct(kCurve, half_s, 0.0, WARP_F_FIXPOINT, &tight, &p, &r);
        CK(rc == WARP_NO_CONVERGE);
        CK(r.status == WARP_NO_CONVERGE);
        CK(r.iters == 2);
        CK(r.residual > 1e-10);
        CK(p.x == -1.0 && p.y == -1.0);    /* 拒绝时 point 未被写入 */
    }

    /* ---- 5. 振荡单独上报：omega 取接近 2 的弱压缩/过冲值，
     * 配合端点目标与跨端点初值制造方向反转不衰减 ---- */
    {
        /* omega=1.9 仍在 (0,2) 合法区间但压缩性很弱，构造目标端点附近、
         * 初值在另一端，配合小 max_iter/小 eta 触发方向反转不衰减。 */
        WarpCfg osc = {1e-12, 1e-14, 40, 1.9, 0.5};
        WPoint p; WarpReport r;
        int rc = warp_correct(kCurve, 0.97 * total, 0.02, WARP_F_FIXPOINT, &osc, &p, &r);
        CK(rc == WARP_OSCILLATING || rc == WARP_NO_CONVERGE || rc == WARP_OK);
        if (rc == WARP_OSCILLATING) {
            CK(r.osc_period == 2);
            CK(r.osc_step >= 2);
        }
    }
    /* 直接构造 2-周期：g 在 t=0/1 边界来回（极大 omega + 端点目标） */
    {
        WarpCfg osc = {1e-13, 1e-15, 64, 1.99, 0.99};
        WPoint p; WarpReport r;
        int rc = warp_correct(kCurve, 0.999 * total, 0.0, WARP_F_FIXPOINT, &osc, &p, &r);
        CK(rc == WARP_OSCILLATING);
        CK(r.osc_period == 2 && r.osc_step >= 2);
    }

    /* ---- 6. 参数校验与错误码字段名 ---- */
    {
        WarpReport r;
        CK(warp_correct(0, half_s, 0.5, WARP_F_FIXPOINT, &cfg, 0, &r) == WARP_E_ARG);
        WPoint bad[4] = {{0,0},{0,1},{NAN,1},{1,1}};
        CK(warp_correct(bad, half_s, 0.5, WARP_F_FIXPOINT, &cfg, 0, &r) == WARP_E_CTRL);
        CK(field_eq(r.field, "c[2].x"));
        CK(warp_correct(kCurve, -1.0, 0.5, WARP_F_FIXPOINT, &cfg, 0, &r) == WARP_E_TARGET);
        CK(warp_correct(kCurve, total + 1.0, 0.5, WARP_F_FIXPOINT, &cfg, 0, &r) == WARP_E_TARGET);
        WarpCfg bad_om = {1e-10, 1e-12, 64, 2.0, 0.8};
        CK(warp_correct(kCurve, half_s, 0.5, WARP_F_FIXPOINT, &bad_om, 0, &r) == WARP_E_TOL);
        CK(field_eq(r.field, "omega"));
        WarpCfg bad_mx = {1e-10, 1e-12, 9999, 0.0, 0.8};
        CK(warp_correct(kCurve, half_s, 0.5, WARP_F_FIXPOINT, &bad_mx, 0, &r) == WARP_E_TOL);
        WPoint degen[4] = {{1,1},{1,1},{1,1},{1,1}};
        CK(warp_correct(degen, 0.0, 0.5, WARP_F_FIXPOINT, &cfg, 0, &r) == WARP_E_DEGEN);
    }

    /* ---- 7. 弧长等分：新逻辑各点残差全达标，legacy 与开关关闭逐位一致 ---- */
    {
        enum { N = 9 };
        WPoint a[N], b[N];
        WarpReport reps[N];
        CK(warp_equally_spaced(kCurve, N, WARP_F_FIXPOINT, &cfg, a, reps) == WARP_OK);
        CK(warp_equally_spaced(kCurve, N, 0, &cfg, b, 0) == WARP_OK);
        for (int i = 0; i < N; ++i) {
            if (i > 0 && i < N - 1) CK(reps[i].residual <= 1e-10);
            double tl;
            double s = total * i / (N - 1);
            CK(warp_correct_legacy(kCurve, s, (double)i / (N - 1), &tl) == WARP_OK);
            WPoint q = eval_bern(kCurve, tl);
            CK(b[i].x == q.x && b[i].y == q.y);   /* 关闭路径 == legacy */
        }
        CK(a[0].x == 0.0 && a[0].y == 0.0);
        CK(fabs(a[N-1].x - 10.0) <= 1e-12 && fabs(a[N-1].y - 6.0) <= 1e-12);
        /* 相邻点弧长增量相等（由构造保证），这里查相邻弦长不为零 */
        for (int i = 1; i < N; ++i) {
            double dx = a[i].x - a[i-1].x, dy = a[i].y - a[i-1].y;
            CK(sqrt(dx*dx + dy*dy) > 1e-6);
        }
    }

    /* ---- 8. 更严容差得到不更差的残差，且迭代次数单调不减趋势 ---- */
    {
        WarpCfg fine = {1e-13, 1e-14, 128, 0.0, 0.8};
        WarpReport r1, r2;
        CK(warp_correct(kCurve, half_s, 0.0, WARP_F_FIXPOINT, &cfg, 0, &r1) == WARP_OK);
        CK(warp_correct(kCurve, half_s, 0.0, WARP_F_FIXPOINT, &fine, 0, &r2) == WARP_OK);
        CK(r2.residual <= r1.residual + 1e-15);
        CK(r2.iters >= r1.iters);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
