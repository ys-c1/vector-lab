#include "keyframe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 节点容量：KEY_MAX_KEYS 个关键帧时最多 (n-1)*levels + 1 个节点。 */
#define MAXN KEY_MAX_NODES

const char *key_strerror(int code) {
    switch (code) {
    case KEY_OK:          return "ok";
    case KEY_E_ARG:       return "invalid argument: null pointer or bad frame count";
    case KEY_E_FEW:       return "too few keyframes (need at least 2); err_at = frame count";
    case KEY_E_TIME:      return "keyframe times are not strictly increasing; err_at = offending frame";
    case KEY_E_NONFINITE: return "keyframe time or position is not finite; err_at = offending frame";
    case KEY_E_LEVELS:    return "levels out of range [2, 16]";
    case KEY_E_FLAGS:     return "unknown flags bit";
    case KEY_E_ENDPOINT:  return "endpoint keyframe missing (non-finite position); err_at = 0 or n-1";
    case KEY_E_SOLVE:     return "linear system is not positive definite (numerically degenerate)";
    default:              return "unknown error";
    }
}

/* 段内比例：t 落在 [keys[i].t, keys[i+1].t] 内。调用方保证区间有效。 */
static double frac(const KeyFrame *keys, int i, double t) {
    double h = keys[i + 1].t - keys[i].t;
    return h > 0.0 ? (t - keys[i].t) / h : 0.0;
}

/* 节点布局：每段 levels 个间隔，关键帧落在段首节点上。
 * 段 i 的节点下标区间为 [i*levels, (i+1)*levels]。
 * 同时铺开线性轨的位置 lx/ly：段内第 r 个节点取比例 f = r/levels，
 * 坐标用 x0 + (x1-x0)*f，与 baseline/linear_ref.c 的旧实现同式同序，
 * 关闭新逻辑时整张节点表因此可以逐字节比对。 */
static int build_nodes(const KeyFrame *keys, int n, int levels,
                       double *ts, double *lx, double *ly, int *fixed) {
    int N = (n - 1) * levels + 1;
    for (int j = 0; j < N; ++j) fixed[j] = -1;
    for (int i = 0; i < n - 1; ++i) {
        for (int r = 0; r < levels; ++r) {
            int j = i * levels + r;
            double f = (double)r / (double)levels;
            ts[j] = keys[i].t + (keys[i + 1].t - keys[i].t) * f;
            lx[j] = keys[i].x + (keys[i + 1].x - keys[i].x) * f;
            ly[j] = keys[i].y + (keys[i + 1].y - keys[i].y) * f;
            if (r == 0) fixed[j] = i;
        }
    }
    ts[N - 1] = keys[n - 1].t;
    lx[N - 1] = keys[n - 1].x;
    ly[N - 1] = keys[n - 1].y;
    fixed[N - 1] = n - 1;
    return N;
}

/* 逐轴组装并求解。freev 是内部节点下标表，M 为其数量；
 * fixedval 按节点下标给出固定值（仅固定节点有意义）。
 * 未知量按 freev 的顺序排；固定节点直接代入右端。
 *
 * 二阶差分把节点 k 与 k-1、k+1 连在一起；一段之内自由节点是连续的，
 * 所以相邻自由节点最多隔两个下标（半带宽 2），但按稠密存更简单，
 * M <= KEY_MAX_NODES-1 = 225 时最多约 400 KB，放堆上。 */
static int solve_axis(int N, const double *ts, const double *fixedval,
                      const int *freev, int M, double *out) {
    if (M == 0) return 0;
    double *A = (double *)calloc((size_t)M * (size_t)M, sizeof(double));
    double *b = (double *)calloc((size_t)M, sizeof(double));
    int *idx = (int *)malloc(sizeof(int) * (size_t)N);
    if (!A || !b || !idx) { free(A); free(b); free(idx); return -1; }
    for (int j = 0; j < N; ++j) idx[j] = -1;
    for (int p = 0; p < M; ++p) idx[freev[p]] = p;

    for (int k = 1; k < N - 1; ++k) {
        double hL = ts[k] - ts[k - 1];
        double hR = ts[k + 1] - ts[k];
        double s = 2.0 / (hL + hR);
        double w = 0.5 * (hL + hR);
        /* a_k = cL*q_{k-1} + cC*q_k + cR*q_{k+1}；S 项为 w*a_k^2 */
        double c[3] = { s / hL, -s * (1.0 / hL + 1.0 / hR), s / hR };
        int nd[3] = { k - 1, k, k + 1 };
        for (int a = 0; a < 3; ++a) {
            int p = idx[nd[a]];
            if (p < 0) continue;
            for (int q = 0; q < 3; ++q) {
                double coef = 2.0 * w * c[a] * c[q];
                int pp = idx[nd[q]];
                if (pp >= 0) A[p * M + pp] += coef;
                else b[p] -= coef * fixedval[nd[q]];
            }
        }
    }
    free(idx);

    /* 对称正定，Cholesky 分解（下三角存回 A 的左下半部）。 */
    for (int i = 0; i < M; ++i) {
        for (int j = 0; j <= i; ++j) {
            double sum = A[i * M + j];
            for (int k = 0; k < j; ++k) sum -= A[i * M + k] * A[j * M + k];
            if (i == j) {
                if (!(sum > 0.0)) { free(A); free(b); return -2; }
                A[i * M + i] = sqrt(sum);
            } else {
                A[i * M + j] = sum / A[j * M + j];
            }
        }
    }
    for (int i = 0; i < M; ++i) {
        double sum = b[i];
        for (int k = 0; k < i; ++k) sum -= A[i * M + k] * b[k];
        b[i] = sum / A[i * M + i];
    }
    for (int i = M - 1; i >= 0; --i) {
        double sum = b[i];
        for (int k = i + 1; k < M; ++k) sum -= A[k * M + i] * b[k];
        b[i] = sum / A[i * M + i];
    }
    for (int p = 0; p < M; ++p) out[freev[p]] = b[p];
    free(A);
    free(b);
    return 0;
}

/* 节点上的离散加速度项 w_k*|a_k|^2，返回 k = 1..N-2 的项和整段 S。
 * term[k] 对应节点下标 k；端点两项为 0。 */
static double action_terms(int N, const double *ts,
                           const double *xs, const double *ys, double *term) {
    double S = 0.0;
    if (term) term[0] = term[N - 1] = 0.0;
    for (int k = 1; k < N - 1; ++k) {
        double hL = ts[k] - ts[k - 1];
        double hR = ts[k + 1] - ts[k];
        double s = 2.0 / (hL + hR);
        double w = 0.5 * (hL + hR);
        double ax = s * ((xs[k + 1] - xs[k]) / hR - (xs[k] - xs[k - 1]) / hL);
        double ay = s * ((ys[k + 1] - ys[k]) / hR - (ys[k] - ys[k - 1]) / hL);
        double a = w * (ax * ax + ay * ay);
        if (term) term[k] = a;
        S += a;
    }
    return S;
}

/* 段作用量份额：关键帧节点项对半，其余归所在段。 */
static void span_actions(int n, int levels, int N,
                         const int *fixed_key, const double *term,
                         double *out) {
    for (int i = 0; i < n - 1; ++i) out[i] = 0.0;
    for (int k = 1; k < N - 1; ++k) {
        int key = fixed_key[k];
        if (key >= 0) {
            if (key > 0) out[key - 1] += 0.5 * term[k];
            if (key < n - 1) out[key] += 0.5 * term[k];
        } else {
            /* 自由节点 k 严格落在段 i 内：i*levels < k < (i+1)*levels，
             * 整数除法 k/levels 正好等于 i。 */
            int i = k / levels;
            out[i] += term[k];
        }
    }
}

int key_solve(const KeyFrame *keys, int n, int levels, unsigned flags,
              KeyPlan *out, int *err_at) {
    int dummy = -1;
    if (!err_at) err_at = &dummy;
    *err_at = -1;
    if (!keys || !out) return KEY_E_ARG;
    if (flags & ~KEY_F_MIN_ACTION) return KEY_E_FLAGS;
    if (n < 2 || n > KEY_MAX_KEYS) { *err_at = n; return KEY_E_FEW; }
    if (levels < KEY_MIN_LEVELS || levels > KEY_MAX_LEVELS) return KEY_E_LEVELS;

    /* 端点先查：首帧或末帧的坐标/时刻非有限即「端点缺失」。端点
     * 很有可能是整条曲线唯一缺失的头/尾，报 KEY_E_ENDPOINT 比笼统的
     * 非有限更具体；NaN 也会让后续单调性比较得出无意义的结论。 */
    if (!isfinite(keys[0].t) || !isfinite(keys[0].x) || !isfinite(keys[0].y)) {
        *err_at = 0;
        return KEY_E_ENDPOINT;
    }
    if (!isfinite(keys[n - 1].t) || !isfinite(keys[n - 1].x) || !isfinite(keys[n - 1].y)) {
        *err_at = n - 1;
        return KEY_E_ENDPOINT;
    }
    for (int i = 1; i < n - 1; ++i) {
        if (!isfinite(keys[i].t) || !isfinite(keys[i].x) || !isfinite(keys[i].y)) {
            *err_at = i;
            return KEY_E_NONFINITE;
        }
    }
    for (int i = 1; i < n; ++i) {
        if (!(keys[i].t > keys[i - 1].t)) {
            *err_at = i;
            return KEY_E_TIME;
        }
    }
    *err_at = -1;

    /* solve_axis 的固定值表按节点下标索引：固定节点是位于段首的
     * 关键帧，取该帧坐标；自由节点的槽位填 0（不参与右端）。 */
    double nodefx[MAXN], nodefy[MAXN];
    for (int j = 0; j < MAXN; ++j) nodefx[j] = nodefy[j] = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = i * levels;
        nodefx[j] = keys[i].x;
        nodefy[j] = keys[i].y;
    }
    int freev[MAXN], M = 0;
    double ts[MAXN], xs[MAXN], ys[MAXN], lxs[MAXN], lys[MAXN];
    int fixed_key[MAXN];
    int N = build_nodes(keys, n, levels, ts, lxs, lys, fixed_key);
    for (int j = 0; j < N; ++j) {
        if (fixed_key[j] < 0) {
            freev[M++] = j;
            xs[j] = ys[j] = 0.0;
        } else {
            xs[j] = keys[fixed_key[j]].x;
            ys[j] = keys[fixed_key[j]].y;
        }
    }

    /* 平滑轨：关闭开关时就是线性轨（逐位相同）。只有两帧时，
     * 两头钳死的二阶差分能量最小值就是直线本身，平滑轨与线性轨
     * 重合，直接复用线性节点，并标记退化。 */
    int degenerate = (n == 2);
    if ((flags & KEY_F_MIN_ACTION) && !degenerate) {
        int rc = solve_axis(N, ts, nodefx, freev, M, xs);
        if (rc == 0) rc = solve_axis(N, ts, nodefy, freev, M, ys);
        if (rc != 0) return rc == -1 ? KEY_E_ARG : KEY_E_SOLVE;
    } else {
        for (int j = 0; j < N; ++j) { xs[j] = lxs[j]; ys[j] = lys[j]; }
    }

    double term[MAXN], lterm[MAXN];
    double S = action_terms(N, ts, xs, ys, term);
    double SL = action_terms(N, ts, lxs, lys, lterm);

    memset(out, 0, sizeof *out);
    out->nkeys = n;
    out->levels = levels;
    out->nnodes = N;
    out->total_action = S;
    out->total_linear_action = SL;
    out->total_reduction = (!degenerate && SL > 0.0) ? 1.0 - S / SL : 0.0;
    out->nspans = n - 1;
    out->degenerate = degenerate;
    double sa[KEY_MAX_KEYS - 1], sla[KEY_MAX_KEYS - 1];
    span_actions(n, levels, N, fixed_key, term, sa);
    span_actions(n, levels, N, fixed_key, lterm, sla);
    for (int i = 0; i < n - 1; ++i) {
        KeySpan *sp = &out->spans[i];
        sp->from = i;
        sp->to = i + 1;
        sp->action = sa[i];
        sp->linear_action = sla[i];
        sp->reduction = (!degenerate && sla[i] > 0.0) ? 1.0 - sa[i] / sla[i] : 0.0;
        sp->degenerate = degenerate;
    }

    /* 每段中点。时刻取两端关键帧时刻的算术平均（与旧线性实现同式）。
     * 关闭新逻辑时，中点必须直接由两端关键帧算，算式与旧实现逐位
     * 相同：f = (tm - t0)/(t1 - t0)，q = q0 + (q1 - q0)*f。
     * 打开新逻辑时取平滑轨的中点：levels 偶数时中点正好是节点，
     * 奇数时取中点两侧平滑节点的线性插值（段内比例 f）。 */
    int smooth = (flags & KEY_F_MIN_ACTION) != 0;
    for (int i = 0; i < n - 1; ++i) {
        double tm = 0.5 * (keys[i].t + keys[i + 1].t);
        int base = i * levels;
        KeySample *mid = &out->spans[i].mid;
        mid->t = tm;
        if (!smooth) {
            double f = frac(keys, i, tm);
            mid->x = keys[i].x + (keys[i + 1].x - keys[i].x) * f;
            mid->y = keys[i].y + (keys[i + 1].y - keys[i].y) * f;
        } else if (levels % 2 == 0) {
            int j = base + levels / 2;
            mid->x = xs[j];
            mid->y = ys[j];
        } else {
            int jl = base + (levels - 1) / 2;   /* 中点左侧节点 */
            double f = frac(keys, i, tm);       /* 段内比例，恰为 0.5 */
            mid->x = xs[jl] + (xs[jl + 1] - xs[jl]) * f;
            mid->y = ys[jl] + (ys[jl + 1] - ys[jl]) * f;
        }
    }

    for (int j = 0; j < N; ++j) {
        out->nodes[j].t = ts[j];
        out->nodes[j].x = xs[j];
        out->nodes[j].y = ys[j];
    }
    return KEY_OK;
}

int key_solve_default(const KeyFrame *keys, int n, int levels,
                      KeyPlan *out, int *err_at) {
#if KEYFRAME_MIN_ACTION
    unsigned flags = KEY_F_MIN_ACTION;
#else
    unsigned flags = 0u;
#endif
    return key_solve(keys, n, levels, flags, out, err_at);
}

int key_sample_at(const KeyPlan *plan, double t, KeySample *out) {
    if (!plan || !out || plan->nnodes < 2) return -1;
    const KeySample *q = plan->nodes;
    int N = plan->nnodes;
    if (t <= q[0].t) { *out = q[0]; return 0; }
    if (t >= q[N - 1].t) { *out = q[N - 1]; return 0; }
    int i = 0;
    while (i + 2 < N && q[i + 1].t <= t) ++i;
    double h = q[i + 1].t - q[i].t;
    double f = h > 0.0 ? (t - q[i].t) / h : 0.0;
    out->t = t;
    out->x = q[i].x + (q[i + 1].x - q[i].x) * f;
    out->y = q[i].y + (q[i + 1].y - q[i].y) * f;
    return 0;
}

int key_export_csv(const KeyPlan *plan, const char *path) {
    if (!plan) return KEY_E_ARG;
    FILE *fp = stdout;
    if (path && *path && !(path[0] == '-' && path[1] == '\0')) {
        fp = fopen(path, "w");
        if (!fp) return KEY_E_ARG;
    }
    fprintf(fp, "from,to,mid_t,mid_x,mid_y,action,linear_action,reduction,degenerate\n");
    for (int i = 0; i < plan->nspans; ++i) {
        const KeySpan *sp = &plan->spans[i];
        fprintf(fp, "%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d\n",
                sp->from, sp->to, sp->mid.t, sp->mid.x, sp->mid.y,
                sp->action, sp->linear_action, sp->reduction, sp->degenerate);
    }
    if (fp != stdout) fclose(fp);
    return KEY_OK;
}
