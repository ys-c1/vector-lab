#include "homog.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

static int g_perspective = 1;          /* 默认开启新逻辑 */

void homog_set_perspective(int on) { g_perspective = on ? 1 : 0; }
int  homog_perspective_enabled(void) { return g_perspective; }

Homog homog_identity(void) {
    Homog h = {1, 0, 0,  0, 1, 0,  0, 0};
    return h;
}

HPoint homog_apply(Homog h, HPoint p) {
    double w = h.h31 * p.x + h.h32 * p.y + 1.0;
    HPoint r;
    r.x = (h.h11 * p.x + h.h12 * p.y + h.h13) / w;
    r.y = (h.h21 * p.x + h.h22 * p.y + h.h23) / w;
    return r;
}

/* 3x3 伴随矩阵闭式求逆。删行/删列后剩余索引按升序取，保证余子式符号正确。 */
int homog_invert(const Homog *h, Homog *out) {
    if (!h || !out) return -1;
    double m[3][3] = {
        {h->h11, h->h12, h->h13},
        {h->h21, h->h22, h->h23},
        {h->h31, h->h32, 1.0}
    };
    double cf[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            int r[2], s[2], kr = 0, ks = 0;
            for (int k = 0; k < 3; ++k) {
                if (k != i) r[kr++] = k;
                if (k != j) s[ks++] = k;
            }
            double minor = m[r[0]][s[0]] * m[r[1]][s[1]]
                         - m[r[0]][s[1]] * m[r[1]][s[0]];
            cf[i][j] = ((i + j) & 1) ? -minor : minor;
        }
    double det = m[0][0] * cf[0][0] + m[0][1] * cf[0][1] + m[0][2] * cf[0][2];
    if (fabs(det) < 1e-14) return -8;
    double inv[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv[i][j] = cf[j][i] / det;            /* 伴随 = 余子式转置 */
    double s = inv[2][2] != 0.0 ? inv[2][2] : 1.0; /* 重新固定 h33 = 1 */
    out->h11 = inv[0][0] / s; out->h12 = inv[0][1] / s; out->h13 = inv[0][2] / s;
    out->h21 = inv[1][0] / s; out->h22 = inv[1][1] / s; out->h23 = inv[1][2] / s;
    out->h31 = inv[2][0] / s; out->h32 = inv[2][1] / s;
    return 0;
}

static double cross2(HPoint a, HPoint b, HPoint c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static double dist2(HPoint a, HPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/* 退化检测：角点重合 / 四点共线。
 * 共线判据：四个三点组合中最小三角形面积相对包围盒尺度过小。 */
int homog_check(const HPoint *dst, int n, int *bad_pair) {
    if (!dst) return -1;
    if (n < 4) return -5;
    const double EPS = 1e-12;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (dist2(dst[i], dst[j]) <= EPS) {
                if (bad_pair) { bad_pair[0] = i; bad_pair[1] = j; }
                return -6;
            }
    double minx = dst[0].x, maxx = dst[0].x;
    double miny = dst[0].y, maxy = dst[0].y;
    for (int i = 1; i < 4; ++i) {
        if (dst[i].x < minx) minx = dst[i].x;
        if (dst[i].x > maxx) maxx = dst[i].x;
        if (dst[i].y < miny) miny = dst[i].y;
        if (dst[i].y > maxy) maxy = dst[i].y;
    }
    double scale = (maxx - minx) * (maxy - miny);   /* 包围盒面积 */
    if (scale <= EPS) scale = 1.0;
    double worst = 1.0;
    int wbi = 0, wbj = 1;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            for (int k = j + 1; k < 4; ++k) {
                double area = fabs(cross2(dst[i], dst[j], dst[k])) * 0.5;
                double rel = area / scale;
                if (rel < worst) { worst = rel; wbi = i; wbj = j; (void)k; }
            }
    if (worst < 1e-9) {                             /* 最小三角形相对面积 ~0 => 共线 */
        if (bad_pair) { bad_pair[0] = wbi; bad_pair[1] = wbj; }
        return -7;
    }
    return 0;
}

void homog_residual(const HPoint *src, const HPoint *dst, const Homog *h, HResidual *res) {
    double sum = 0.0, mx = 0.0;
    for (int i = 0; i < 4; ++i) {
        HPoint q = homog_apply(*h, src[i]);
        double ex = q.x - dst[i].x, ey = q.y - dst[i].y;
        double e = hypot(ex, ey);
        res->perr[i] = e;
        sum += ex * ex + ey * ey;
        if (e > mx) mx = e;
    }
    res->rms = sqrt(sum / 4.0);
    res->maxerr = mx;
}

/* ---------- 8x8 高斯消元（带部分主元），解 A x = b，解写回 b ---------- */
static int gauss8(double A[8][8], double b[8]) {
    for (int col = 0; col < 8; ++col) {
        int piv = col;
        double best = fabs(A[col][col]);
        for (int r = col + 1; r < 8; ++r)
            if (fabs(A[r][col]) > best) { best = fabs(A[r][col]); piv = r; }
        if (best < 1e-14) return -1;
        if (piv != col) {
            for (int c = 0; c < 8; ++c) {
                double t = A[col][c]; A[col][c] = A[piv][c]; A[piv][c] = t;
            }
            double t = b[col]; b[col] = b[piv]; b[piv] = t;
        }
        for (int r = 0; r < 8; ++r) {
            if (r == col) continue;
            double f = A[r][col] / A[col][col];
            for (int c = col; c < 8; ++c) A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int i = 0; i < 8; ++i) b[i] /= A[i][i];
    return 0;
}

/* Hartley 归一化：平移到质心 + 各向同性缩放，使平均到原点距离为 sqrt(2)。
 * T 以 3x3 齐次形式输出（与 Homog 同布局，h33=1）。 */
static void normalize4(const HPoint *p, double T[3][3]) {
    double cx = 0, cy = 0;
    for (int i = 0; i < 4; ++i) { cx += p[i].x; cy += p[i].y; }
    cx *= 0.25; cy *= 0.25;
    double mean = 0;
    for (int i = 0; i < 4; ++i)
        mean += hypot(p[i].x - cx, p[i].y - cy);
    mean *= 0.25;
    double s = (mean > 1e-14) ? M_SQRT2 / mean : 1.0;
    memset(T, 0, sizeof(double) * 9);
    T[0][0] = s;    T[1][1] = s;    T[2][2] = 1.0;
    T[0][2] = -s * cx;
    T[1][2] = -s * cy;
}

static HPoint apply3(double T[3][3], HPoint p) {
    double w = T[2][0] * p.x + T[2][1] * p.y + T[2][2];
    HPoint r;
    r.x = (T[0][0] * p.x + T[0][1] * p.y + T[0][2]) / w;
    r.y = (T[1][0] * p.x + T[1][1] * p.y + T[1][2]) / w;
    return r;
}

/* 对归一化后的点构造 DLT 方程：固定 h33=1，每行 8 个未知量。 */
static int dlt_normalized(const HPoint *ns, const HPoint *nd, Homog *out) {
    double A[8][8];
    double b[8];
    memset(A, 0, sizeof(A));
    memset(b, 0, sizeof(b));
    for (int i = 0; i < 4; ++i) {
        double x = ns[i].x, y = ns[i].y;
        double u = nd[i].x, v = nd[i].y;
        int r1 = 2 * i, r2 = 2 * i + 1;
        A[r1][0] = x; A[r1][1] = y; A[r1][2] = 1;
        A[r1][6] = -u * x; A[r1][7] = -u * y;
        b[r1] = u;
        A[r2][3] = x; A[r2][4] = y; A[r2][5] = 1;
        A[r2][6] = -v * x; A[r2][7] = -v * y;
        b[r2] = v;
    }
    if (gauss8(A, b) != 0) return -8;
    out->h11 = b[0]; out->h12 = b[1]; out->h13 = b[2];
    out->h21 = b[3]; out->h22 = b[4]; out->h23 = b[5];
    out->h31 = b[6]; out->h32 = b[7];
    return 0;
}

/* 3x3 相乘 C = A B（同 Homog 布局，[2][2]=1） */
static void mat3mul(double C[3][3], double A[3][3], double B[3][3]) {
    double T[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k) s += A[i][k] * B[k][j];
            T[i][j] = s;
        }
    memcpy(C, T, sizeof(T));
}

static Homog mat3_to_homog(double M[3][3]) {
    Homog h;
    double s = M[2][2] != 0.0 ? M[2][2] : 1.0;   /* 重新固定 h33 = 1 */
    h.h11 = M[0][0] / s; h.h12 = M[0][1] / s; h.h13 = M[0][2] / s;
    h.h21 = M[1][0] / s; h.h22 = M[1][1] / s; h.h23 = M[1][2] / s;
    h.h31 = M[2][0] / s; h.h32 = M[2][1] / s;
    return h;
}

int homog_solve_dlt(const HPoint *src, const HPoint *dst, Homog *out) {
    if (!src || !dst || !out) return -1;
    int bp[2];
    int rc = homog_check(dst, 4, bp);
    if (rc != 0) return rc;
    double Ts[3][3], Td[3][3];
    normalize4(src, Ts);
    normalize4(dst, Td);
    HPoint ns[4], nd[4];
    for (int i = 0; i < 4; ++i) {
        ns[i] = apply3(Ts, src[i]);
        nd[i] = apply3(Td, dst[i]);
    }
    Homog hn;
    rc = dlt_normalized(ns, nd, &hn);
    if (rc != 0) return rc;
    /* H = Td^-1 * Hn * Ts */
    double Hn[3][3] = {
        {hn.h11, hn.h12, hn.h13},
        {hn.h21, hn.h22, hn.h23},
        {hn.h31, hn.h32, 1.0}
    };
    double Tdi[3][3];
    /* Td 为相似变换：[[s,0,tx],[0,s,ty],[0,0,1]]，直接写逆 */
    double s = Td[0][0], tx = Td[0][2], ty = Td[1][2];
    Tdi[0][0] = 1 / s; Tdi[0][1] = 0;     Tdi[0][2] = -tx / s;
    Tdi[1][0] = 0;     Tdi[1][1] = 1 / s; Tdi[1][2] = -ty / s;
    Tdi[2][0] = 0;     Tdi[2][1] = 0;     Tdi[2][2] = 1;
    double tmp[3][3], H[3][3];
    mat3mul(tmp, Hn, Ts);
    mat3mul(H, Tdi, tmp);
    *out = mat3_to_homog(H);
    return 0;
}

/* ---------- 高斯-牛顿最小二乘精化 ----------
 * 参数向量 p = (h11,h12,h13,h21,h22,h23,h31,h32)，目标残差为四角
 * 重投影的 (ex, ey)，共 8 个方程。8x8 正规方程，最多 10 轮，
 * 步长改善不足即停。 */
static void project_jacobian(const Homog *h, HPoint p,
                             double fx[8], double fy[8], HPoint *q) {
    double w  = h->h31 * p.x + h->h32 * p.y + 1.0;
    double Xn = h->h11 * p.x + h->h12 * p.y + h->h13;
    double Yn = h->h21 * p.x + h->h22 * p.y + h->h23;
    q->x = Xn / w;
    q->y = Yn / w;
    double iw = 1.0 / w, iw2 = iw * iw;
    /* dX/dp */
    fx[0] = p.x * iw; fx[1] = p.y * iw; fx[2] = iw;
    fx[3] = 0; fx[4] = 0; fx[5] = 0;
    fx[6] = -Xn * p.x * iw2; fx[7] = -Xn * p.y * iw2;
    /* dY/dp */
    fy[0] = 0; fy[1] = 0; fy[2] = 0;
    fy[3] = p.x * iw; fy[4] = p.y * iw; fy[5] = iw;
    fy[6] = -Yn * p.x * iw2; fy[7] = -Yn * p.y * iw2;
}

int homog_refine(const HPoint *src, const HPoint *dst, Homog *h, HResidual *res) {
    if (!src || !dst || !h) return -1;
    HResidual cur;
    homog_residual(src, dst, h, &cur);
    for (int iter = 0; iter < 10; ++iter) {
        double JtJ[8][8];
        double Jte[8];
        memset(JtJ, 0, sizeof(JtJ));
        memset(Jte, 0, sizeof(Jte));
        for (int i = 0; i < 4; ++i) {
            double jx[8], jy[8];
            HPoint q;
            project_jacobian(h, src[i], jx, jy, &q);
            double ex = dst[i].x - q.x;      /* 残差取负号 => 解 J dp = e */
            double ey = dst[i].y - q.y;
            for (int a = 0; a < 8; ++a) {
                Jte[a] += jx[a] * ex + jy[a] * ey;
                for (int b = 0; b < 8; ++b)
                    JtJ[a][b] += jx[a] * jx[b] + jy[a] * jy[b];
            }
        }
        double dp[8];
        memcpy(dp, Jte, sizeof(dp));
        if (gauss8(JtJ, dp) != 0) break;
        Homog nh = *h;
        nh.h11 += dp[0]; nh.h12 += dp[1]; nh.h13 += dp[2];
        nh.h21 += dp[3]; nh.h22 += dp[4]; nh.h23 += dp[5];
        nh.h31 += dp[6]; nh.h32 += dp[7];
        HResidual nr;
        homog_residual(src, dst, &nh, &nr);
        double step = 0;
        for (int a = 0; a < 8; ++a) step += dp[a] * dp[a];
        if (nr.rms >= cur.rms || sqrt(step) < 1e-13) break;   /* 不接受劣化步 */
        *h = nh;
        cur = nr;
    }
    if (res) *res = cur;
    return 0;
}

/* ---------- 仿射回退：6 参数最小二乘，h31 = h32 = 0 ---------- */
int homog_solve_affine(const HPoint *src, const HPoint *dst, Homog *out, HResidual *res) {
    if (!src || !dst || !out) return -1;
    int bp[2];
    int rc = homog_check(dst, 4, bp);
    if (rc != 0) return rc;
    /* 正规方程，每个点贡献两行：
     *   x a + y c + e = u ; x b + y d + f = v
     * 未知量顺序 (a,c,e,b,d,f) */
    double A[6][6];
    double b[6];
    memset(A, 0, sizeof(A));
    memset(b, 0, sizeof(b));
    for (int i = 0; i < 4; ++i) {
        double row[3] = {src[i].x, src[i].y, 1.0};
        double rhs[2] = {dst[i].x, dst[i].y};
        for (int g = 0; g < 2; ++g) {
            int base = 3 * g;
            for (int a = 0; a < 3; ++a) {
                b[base + a] += row[a] * rhs[g];
                for (int c = 0; c < 3; ++c)
                    A[base + a][base + c] += row[a] * row[c];
            }
        }
    }
    /* 6x6 消元（部分主元） */
    for (int col = 0; col < 6; ++col) {
        int piv = col;
        double best = fabs(A[col][col]);
        for (int r = col + 1; r < 6; ++r)
            if (fabs(A[r][col]) > best) { best = fabs(A[r][col]); piv = r; }
        if (best < 1e-14) return -8;
        if (piv != col) {
            for (int c = 0; c < 6; ++c) {
                double t = A[col][c]; A[col][c] = A[piv][c]; A[piv][c] = t;
            }
            double t = b[col]; b[col] = b[piv]; b[piv] = t;
        }
        for (int r = 0; r < 6; ++r) {
            if (r == col) continue;
            double f = A[r][col] / A[col][col];
            for (int c = col; c < 6; ++c) A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int i = 0; i < 6; ++i) b[i] /= A[i][i];
    out->h11 = b[0]; out->h12 = b[1]; out->h13 = b[2];
    out->h21 = b[3]; out->h22 = b[4]; out->h23 = b[5];
    out->h31 = 0.0;  out->h32 = 0.0;
    if (res) homog_residual(src, dst, out, res);
    return 0;
}

int homog_solve(const HPoint *src, const HPoint *dst, Homog *out, HResidual *res) {
    if (!src || !dst || !out) return -1;
    int bp[2];
    int rc = homog_check(dst, 4, bp);
    if (rc != 0) return rc;
    Homog h;
    HResidual local;
    HResidual *rp = res ? res : &local;
    if (g_perspective) {
        rc = homog_solve_dlt(src, dst, &h);
        if (rc != 0) return rc;
        rc = homog_refine(src, dst, &h, rp);
        if (rc != 0) return rc;
        if (rp->maxerr > 0.5) return -9;          /* 角点残差不得超过 0.5px */
        *out = h;
        return 0;
    }
    rc = homog_solve_affine(src, dst, out, rp);
    if (rc != 0) return rc;
    if (rp->maxerr > 0.5) return -9;
    return 0;
}
