/* 随机几何的完整性/不变量检查（可重复，固定种子）。
 *
 * 对每个随机窗口（坐标在 ±100 内、3..16 个顶点、可能自交）验证：
 *   1) 归一化成功时，每个子环自身无真穿过；
 *   2) 任意两个子环之间无真穿过；
 *   3) 每个子环归一化后面积 > 0；
 *   4) 每个子环的 |原符号面积| 与 abs_area 逐位相同；
 *   5) 原输入的奇偶覆盖与子环并集的奇偶覆盖在被检查的点上相同。
 *
 * "真穿过"用 long double 判定，且要求交点参数落在 [0.01, 0.99]：近共线、
 * 共端点的浮点噪声不算违规，只有明确的交叉才报错。覆盖采样跳过离任何边
 * 1e-4 以内的点（那里半开约定不同，属边界约定而非几何差异）。
 *
 * 用法：fuzz_arrange [迭代次数]（默认 20000）
 */
#include "winnorm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long st = 13579;
static unsigned long rnd(void) {
    st ^= st << 13;
    st ^= st >> 7;
    st ^= st << 17;
    return st;
}
static double frand(double a, double b) {
    return a + (b - a) * ((double)(rnd() >> 11) / 9007199254740992.0);
}
static int bit_eq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

static int proper_ld(WPoint a, WPoint b, WPoint c, WPoint d) {
    long double rx = (long double)b.x - a.x, ry = (long double)b.y - a.y;
    long double sx = (long double)d.x - c.x, sy = (long double)d.y - c.y;
    long double la = hypotl(rx, ry), lb = hypotl(sx, sy);
    long double den = rx * sy - ry * sx;
    if (fabsl(den) <= 1e-9L * la * lb) return 0;
    long double qx = (long double)c.x - a.x, qy = (long double)c.y - a.y;
    long double t = (qx * sy - qy * sx) / den;
    long double u = (qx * ry - qy * rx) / den;
    return t > 0.01L && t < 0.99L && u > 0.01L && u < 0.99L;
}

static int ring_cross(const WRing *a, const WRing *b) {
    for (int i = 0; i < a->n; ++i)
        for (int j = 0; j < b->n; ++j)
            if (proper_ld(a->pts[i], a->pts[(i + 1) % a->n],
                          b->pts[j], b->pts[(j + 1) % b->n]))
                return 1;
    return 0;
}

static int even_odd(const WPoint *p, int n, double x, double y) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        WPoint a = p[i], b = p[(i + 1) % n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static double seg_dist(double px, double py, WPoint a, WPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double l2 = dx * dx + dy * dy;
    double t = l2 > 0.0 ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return hypot(a.x + dx * t - px, a.y + dy * t - py);
}

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 20000;
    int ok = 0, rej = 0, fails = 0;
    for (int it = 0; it < iters && fails < 8; ++it) {
        int n = 3 + (int)(rnd() % 14);
        WPoint p[20];
        for (int i = 0; i < n; ++i) {
            p[i].x = frand(-100, 100);
            p[i].y = frand(-100, 100);
        }
        WNorm nrm;
        int at = -1, at2 = -1;
        int rc = winnorm_normalize(p, n, 1, &nrm, &at, &at2);
        if (rc != WN_OK) {
            ++rej;
            if (rc != WN_ERR_SELF_CROSS && rc != WN_ERR_DUP_POINT &&
                rc != WN_ERR_CONTACT && rc != WN_ERR_DEGENERATE &&
                rc != WN_ERR_TOO_MANY_RINGS && rc != WN_ERR_CAP &&
                rc != WN_ERR_AREA_DRIFT) {
                printf("FAIL 意外错误码 %d it=%d\n", rc, it);
                ++fails;
            }
            continue;
        }
        ++ok;
        for (int a = 0; a < nrm.rings; ++a) {
            if (ring_cross(&nrm.ring[a], &nrm.ring[a])) {
                printf("FAIL 子环 %d 仍自交 it=%d n=%d\n", a, it, n);
                ++fails;
            }
            if (!bit_eq(fabs(nrm.in_signed_area[a]), nrm.abs_area[a])) {
                printf("FAIL 子环 %d 面积绝对值漂移 it=%d\n", a, it);
                ++fails;
            }
            if (!(winnorm_signed_area(nrm.ring[a].pts, nrm.ring[a].n) > 0.0)) {
                printf("FAIL 子环 %d 归一化后符号面积不为正 it=%d\n", a, it);
                ++fails;
            }
            for (int b = a + 1; b < nrm.rings; ++b)
                if (ring_cross(&nrm.ring[a], &nrm.ring[b])) {
                    printf("FAIL 子环 %d 与 %d 互相穿过 it=%d n=%d\n", a, b, it, n);
                    ++fails;
                }
        }
        double minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
        for (int i = 1; i < n; ++i) {
            if (p[i].x < minx) minx = p[i].x;
            if (p[i].x > maxx) maxx = p[i].x;
            if (p[i].y < miny) miny = p[i].y;
            if (p[i].y > maxy) maxy = p[i].y;
        }
        for (int s = 0; s < 32; ++s) {
            double x = frand(minx, maxx), y = frand(miny, maxy);
            int skip = 0;
            for (int i = 0; i < n && !skip; ++i)
                if (seg_dist(x, y, p[i], p[(i + 1) % n]) < 1e-4) skip = 1;
            for (int k = 0; k < nrm.rings && !skip; ++k)
                for (int i = 0; i < nrm.ring[k].n && !skip; ++i)
                    if (seg_dist(x, y, nrm.ring[k].pts[i],
                                 nrm.ring[k].pts[(i + 1) % nrm.ring[k].n]) < 1e-4)
                        skip = 1;
            if (skip) continue;
            int c1 = 0;
            for (int k = 0; k < nrm.rings; ++k)
                c1 += even_odd(nrm.ring[k].pts, nrm.ring[k].n, x, y);
            if (even_odd(p, n, x, y) != (c1 & 1)) {
                printf("FAIL 几何覆盖不一致 it=%d n=%d rings=%d 点(%.6f,%.6f)\n",
                       it, n, nrm.rings, x, y);
                ++fails;
                break;
            }
        }
    }
    printf("随机完整性：%d 归一化成功，%d 拒绝，%d 失败（共 %d 次）\n",
           ok, rej, fails, iters);
    return fails ? 1 : 0;
}
