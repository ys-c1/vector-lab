#include "winnorm.h"
#include "winsplit.h"
#include "winsolve.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int bit_eq(double a, double b) {
    return memcmp(&a, &b, sizeof a) == 0;
}

/* 用 long double 独立复算面积，交叉核对 abs_area。 */
static long double area_ld(const WPoint *p, int n) {
    long double s = 0.0L;
    for (int i = 0; i < n; ++i) {
        const WPoint a = p[i], b = p[(i + 1) % n];
        s += (long double)a.x * (long double)b.y - (long double)b.x * (long double)a.y;
    }
    return 0.5L * s;
}

static void sort3(double *v, int n) {
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (v[j] < v[i]) { double t = v[i]; v[i] = v[j]; v[j] = t; }
}

/* 归一化后：每个子环符号面积 > 0；同一顶点集合（逆序不改几何）；
 * |面积| 与独立 long double 复算一致；翻前翻后 |面积| 逐位相同。 */
static void check_norm(const WNorm *nrm, int expect_rings) {
    CK(nrm->rings == expect_rings);
    for (int k = 0; k < nrm->rings; ++k) {
        CK(winnorm_signed_area(nrm->ring[k].pts, nrm->ring[k].n) > 0.0);
        long double ref = area_ld(nrm->ring[k].pts, nrm->ring[k].n);
        CK(fabsl(fabsl(ref) - (long double)nrm->abs_area[k]) < 1e-9L);
        double rawsigned = nrm->in_signed_area[k];
        double rawabs = rawsigned < 0.0 ? -rawsigned : rawsigned;
        CK(bit_eq(rawabs, nrm->abs_area[k]));
    }
}

int main(void) {
    int at = -9, at2 = -9;

    /* 1) 干净逆时针 / 顺时针：只靠逆序统一，面积绝对值逐位不变 */
    WPoint ccw[4] = {{4,4},{36,4},{36,24},{4,24}};
    WPoint cw[4]  = {{44,4},{44,24},{76,24},{76,4}};
    WNorm n1, n2;
    CK(winnorm_normalize(ccw, 4, 0, &n1, &at, &at2) == WN_OK);
    check_norm(&n1, 1);
    CK(n1.flipped[0] == 0);
    CK(bit_eq(n1.abs_area[0], 640.0));
    CK(bit_eq(n1.in_signed_area[0], 640.0));

    CK(winnorm_normalize(cw, 4, 0, &n2, &at, &at2) == WN_OK);
    check_norm(&n2, 1);
    CK(n2.flipped[0] == 1);
    CK(n2.in_signed_area[0] == -640.0);
    CK(bit_eq(n2.abs_area[0], 640.0));   /* 翻前翻后逐位相同 */
    CK(n2.ring[0].pts[0].x == cw[0].x && n2.ring[0].pts[0].y == cw[0].y);

    /* 2) 自交蝴蝶结：关闭拆分必须拒并指出边号；开启拆分得到 2 个子环，
     *    两者原符号相反（符号会骗人），翻转后都为逆时针 */
    WPoint bow[4] = {{84,4},{116,28},{116,4},{84,28}};
    int code = 0, a = -1, b = -1;
    CK(winnorm_check(bow, 4, &code, &a, &b) == 1);
    CK(code == WN_ERR_SELF_CROSS);
    CK(a == 0 && b == 2);                       /* 第一处真穿过边号 */
    CK(winnorm_normalize(bow, 4, 0, &n1, &at, &at2) == WN_ERR_SELF_CROSS);
    CK(at == 0 && at2 == 2);

    WNorm nb;
    CK(winnorm_normalize(bow, 4, 1, &nb, &at, &at2) == WN_OK);
    check_norm(&nb, 2);
    CK((nb.in_signed_area[0] > 0.0) != (nb.in_signed_area[1] > 0.0));
    CK(nb.flipped[0] + nb.flipped[1] == 1);
    CK(bit_eq(nb.abs_area[0], 192.0) && bit_eq(nb.abs_area[1], 192.0));
    CK(bit_eq(winnorm_signed_area_exact(bow, 4), 0.0));  /* 整环符号骗人 */

    /* 3) 双环 6 边形：拆成 3 个子环，中间的环原为顺时针 */
    WPoint dbl[6] = {{124,4},{140,20},{156,4},{156,20},{140,4},{124,20}};
    WNorm nd;
    CK(winnorm_normalize(dbl, 6, 1, &nd, &at, &at2) == WN_OK);
    check_norm(&nd, 3);
    CK(nd.flipped[0] + nd.flipped[1] + nd.flipped[2] == 1);
    double got[3] = {nd.abs_area[0], nd.abs_area[1], nd.abs_area[2]};
    double tot = 0.0;
    for (int k = 0; k < 3; ++k) tot += got[k];
    CK(bit_eq(tot, 256.0));
    sort3(got, 3);
    CK(bit_eq(got[0], 64.0));
    CK(bit_eq(got[1], 64.0));
    CK(bit_eq(got[2], 128.0));

    /* 4) 重复点：拒绝并指出第一个重复点下标 */
    WPoint dup[5] = {{4,44},{36,44},{36,44},{36,64},{4,64}};
    CK(winnorm_check(dup, 5, &code, &a, &b) == 1);
    CK(code == WN_ERR_DUP_POINT);
    CK(a == 1);
    CK(winnorm_normalize(dup, 5, 1, &n1, &at, &at2) == WN_ERR_DUP_POINT);
    CK(at == 1);

    /* 5) 顶点贴边（非真穿过）单独一类；拆分也不能消除，报接触边号 */
    WPoint touch[5] = {{4,84},{36,84},{36,104},{4,104},{20,84}};
    CK(winnorm_check(touch, 5, &code, &a, &b) == 1);
    CK(code == WN_ERR_CONTACT);
    CK(a == 0 && b == 3);
    WRing tmp[4];
    CK(winnorm_split_self(touch, 5, tmp, 4, &a, &b) == WN_ERR_CONTACT);
    CK(a == 0 && b == 3);

    /* 6) 单环入口：未拆分的自交必须拒并给边号；简单的环照常归一并报面积 */
    WRing one;
    CK(winnorm_normalize_ring(bow, 4, &one, NULL, NULL, &a, &b) == WN_ERR_SELF_CROSS);
    CK(a == 0 && b == 2);
    double absv = 0;
    int flipped = -1;
    CK(winnorm_normalize_ring(ccw, 4, &one, &absv, &flipped, &a, &b) == WN_OK);
    CK(winnorm_signed_area(one.pts, one.n) > 0.0);
    CK(bit_eq(absv, 640.0) && flipped == 0);
    CK(winnorm_normalize_ring(cw, 4, &one, &absv, &flipped, &a, &b) == WN_OK);
    CK(bit_eq(absv, 640.0) && flipped == 1);

    /* 7) 只拆分入口与一站式入口给出同一组子环（面积多重集相同） */
    WRing sub[WN_MAX_RINGS];
    int nr = winnorm_split_self(dbl, 6, sub, WN_MAX_RINGS, &a, &b);
    CK(nr == 3);
    double abs_sub[3];
    for (int k = 0; k < nr; ++k) {
        double s = winnorm_signed_area_exact(sub[k].pts, sub[k].n);
        abs_sub[k] = s < 0 ? -s : s;
    }
    sort3(abs_sub, 3);
    CK(bit_eq(abs_sub[0], 64.0) && bit_eq(abs_sub[1], 64.0) &&
       bit_eq(abs_sub[2], 128.0));

    /* 8) 非轴对齐浮点几何：翻环后 |面积| 仍逐位相同（补偿求和对称） */
    {
        WPoint r[5];
        for (int i = 0; i < 5; ++i) {
            double t = 0.37 + 1.3 * i;
            r[i].x = 10.0 + 7.3 * cos(t);
            r[i].y = -3.0 + 5.1 * sin(t);
        }
        WNorm nf;
        CK(winnorm_normalize(r, 5, 0, &nf, &at, &at2) == WN_OK);
        check_norm(&nf, 1);
        double before = winnorm_signed_area_exact(r, 5);
        if (before < 0.0) before = -before;
        CK(bit_eq(before, nf.abs_area[0]));
    }

    /* 9) 参数与退化 */
    CK(winnorm_normalize(NULL, 4, 1, &n1, &at, &at2) == WN_ERR_NULL);
    CK(winnorm_normalize(ccw, 2, 1, &n1, &at, &at2) == WN_ERR_TOO_FEW);
    WPoint line3[3] = {{0,0},{1,1},{2,2}};
    CK(winnorm_normalize(line3, 3, 1, &n1, &at, &at2) == WN_ERR_DEGENERATE);
    CK(at == 0);
    WPoint cross4[4] = {{0,0},{1,0},{0,0},{0,1}};
    CK(winnorm_normalize(cross4, 4, 1, &n1, &at, &at2) == WN_ERR_DUP_POINT);
    CK(at == 0);

    /* 10) 接触分类表：相邻边不算接触；蝴蝶结只报一处真穿过 */
    WContact cs[8];
    CK(winnorm_contacts(ccw, 4, cs, 8) == 0);
    CK(winnorm_contacts(bow, 4, cs, 8) == 1);
    CK(cs[0].kind == WN_TOUCH_PROPER);
    CK(cs[0].edge_a == 0 && cs[0].edge_b == 2);
    CK(winnorm_contacts(touch, 5, cs, 8) >= 1);
    /* cap 小于真实条数时，返回真实条数，只写 cap 条 */
    CK(winnorm_contacts(dbl, 6, cs, 1) == 2);

    /* 11) 所有错误码都有中文说明 */
    for (int c = WN_ERR_AREA_DRIFT; c <= WN_ERR_NULL; ++c)
        CK(winnorm_strerror(c)[0] != '\0');

    /* 12) 独立生成的已拆分文件：装载后的子环面积与模块拆分结果一致 */
    WFileSet set;
    char err[512];
    if (wsplit_load("data/win_double1.txt", &set, err, sizeof err) == 0) {
        CK(set.count == 1);
        CK(set.win[0].rings == 3);
        double la[3] = {set.win[0].raw_abs[0], set.win[0].raw_abs[1],
                        set.win[0].raw_abs[2]};
        sort3(la, 3);
        CK(bit_eq(la[0], 64.0) && bit_eq(la[1], 64.0) && bit_eq(la[2], 128.0));
        CK(bit_eq(wsplit_total_abs(&set.win[0]), 256.0));
    } else {
        printf("SKIP data/win_double1.txt: %s\n", err);
    }

    /* 13) 数据文件的拒绝路径 */
    CK(wsplit_load("data/win_dup.txt", &set, err, sizeof err) == 0); /* 装载不判重复点 */
    CK(wsplit_load("data/bad_dup_id.txt", &set, err, sizeof err) != 0);
    CK(strstr(err, "重复") != NULL);
    CK(wsplit_load("data/bad_split_adjacent.txt", &set, err, sizeof err) != 0);
    CK(wsplit_load("data/bad_split_outside.txt", &set, err, sizeof err) != 0);
    CK(wsplit_load("data/bad_split_win.txt", &set, err, sizeof err) != 0);
    CK(wsplit_load("data/bad_few.txt", &set, err, sizeof err) != 0);
    CK(wsplit_load("data/bad_vertex_before.txt", &set, err, sizeof err) != 0);
    CK(wsplit_load("data/does_not_exist.txt", &set, err, sizeof err) != 0);

    /* 14) 批量：四组窗口全部归一化成功，声明拆分与模块复算一致 */
    if (wsplit_load("data/wins_four.txt", &set, err, sizeof err) == 0) {
        WSet s;
        ws_build(&set, &s);
        CK(s.count == 4);
        for (int k = 0; k < s.count; ++k) {
            CK(s.win[k].rc == WN_OK);
            CK(s.win[k].split_ok == 1);
        }
        CK(s.win[0].rings == 1 && s.win[1].rings == 1);
        CK(s.win[2].rings == 2 && s.win[3].rings == 3);
        CK(bit_eq(s.win[0].total_abs, 640.0));
        CK(bit_eq(s.win[1].total_abs, 640.0));
        CK(bit_eq(s.win[2].total_abs, 384.0));
        CK(bit_eq(s.win[3].total_abs, 256.0));
    } else {
        printf("SKIP data/wins_four.txt: %s\n", err);
    }

    /* 15) 五角星（5 处真穿过）：递归拆到每个子环都简单，
     *     子环面积绝对值之和 == 整环有符号面积（大面积是正的），
     *     且并集的奇偶覆盖与原图一致。 */
    {
        WPoint star[5];
        for (int i = 0; i < 5; ++i) {
            double a = 1.5707963267948966 + 2.0 * 3.141592653589793 *
                                             ((i * 2) % 5) / 5.0;
            star[i].x = 100.0 * cos(a);
            star[i].y = 100.0 * sin(a);
        }
        double whole = winnorm_signed_area_exact(star, 5);
        CK(whole > 0.0);
        WNorm ns;
        CK(winnorm_normalize(star, 5, 0, &ns, &at, &at2) == WN_ERR_SELF_CROSS);
        CK(at == 0 && at2 == 2);
        CK(winnorm_normalize(star, 5, 1, &ns, &at, &at2) == WN_OK);
        check_norm(&ns, ns.rings);
        CK(ns.rings >= 2);
        /* 注意：五角星中心被覆盖两次，奇偶规则下是洞。所以子环面积之
         * 和应等于"原图奇偶覆盖为实的区域"的面积（5 个尖角），而不是
         * 整环有符号面积（那包含了中心两层的抵消）。用网格面积核对。 */
        double tot = 0.0;
        for (int k = 0; k < ns.rings; ++k) tot += ns.abs_area[k];
        /* 子环两两之间不得再有真穿过（拆到每个子环简单） */
        for (int a2i = 0; a2i < ns.rings; ++a2i)
            for (int b2i = a2i + 1; b2i < ns.rings; ++b2i) {
                int hits = 0;
                for (int i = 0; i < ns.ring[a2i].n; ++i)
                    for (int j = 0; j < ns.ring[b2i].n; ++j) {
                        WPoint p0 = ns.ring[a2i].pts[i];
                        WPoint p1 = ns.ring[a2i].pts[(i + 1) % ns.ring[a2i].n];
                        WPoint q0 = ns.ring[b2i].pts[j];
                        WPoint q1 = ns.ring[b2i].pts[(j + 1) % ns.ring[b2i].n];
                        double rx = p1.x - p0.x, ry = p1.y - p0.y;
                        double sx = q1.x - q0.x, sy = q1.y - q0.y;
                        double den = rx * sy - ry * sx;
                        double qx = q0.x - p0.x, qy = q0.y - p0.y;
                        if (fabs(den) < 1e-12) continue;
                        double t = (qx * sy - qy * sx) / den;
                        double u = (qx * ry - qy * rx) / den;
                        if (t > 1e-9 && t < 1 - 1e-9 && u > 1e-9 && u < 1 - 1e-9)
                            ++hits;
                    }
                CK(hits == 0);
            }
        /* 并集奇偶覆盖与原图一致（撒点核对，跳过落在边上的点） */
        double minx = star[0].x, maxx = star[0].x, miny = star[0].y, maxy = star[0].y;
        for (int i = 1; i < 5; ++i) {
            if (star[i].x < minx) minx = star[i].x;
            if (star[i].x > maxx) maxx = star[i].x;
            if (star[i].y < miny) miny = star[i].y;
            if (star[i].y > maxy) maxy = star[i].y;
        }
        /* 网格面积：奇偶覆盖为实的采样点占比 * 包围盒面积 */
        long hit0 = 0, hit1 = 0;
        const int GN = 512;
        double cell = (maxx - minx) * (maxy - miny) / ((double)GN * GN);
        int mismatch = 0;
        for (int j = 0; j < GN; ++j)
            for (int i = 0; i < GN; ++i) {
                double x = minx + (i + 0.5) / GN * (maxx - minx);
                double y = miny + (j + 0.5) / GN * (maxy - miny);
                int c0 = 0, c1 = 0;
                for (int s = 0; s < 5; ++s) {
                    WPoint p0 = star[s], p1 = star[(s + 1) % 5];
                    if ((p0.y > y) == (p1.y > y)) continue;
                    double t = (y - p0.y) / (p1.y - p0.y);
                    if (p0.x + (p1.x - p0.x) * t > x) ++c0;
                }
                for (int k = 0; k < ns.rings; ++k)
                    for (int s = 0; s < ns.ring[k].n; ++s) {
                        WPoint p0 = ns.ring[k].pts[s];
                        WPoint p1 = ns.ring[k].pts[(s + 1) % ns.ring[k].n];
                        if ((p0.y > y) == (p1.y > y)) continue;
                        double t = (y - p0.y) / (p1.y - p0.y);
                        if (p0.x + (p1.x - p0.x) * t > x) ++c1;
                    }
                if (c0 & 1) ++hit0;
                if (c1 & 1) ++hit1;
                if ((c0 & 1) != (c1 & 1)) ++mismatch;
            }
        CK(mismatch == 0);
        /* 子环面积之和 == 奇偶覆盖区域的网格面积（网格误差 ~ 周长/N） */
        double grid_area = (double)hit0 * cell;
        CK(fabs(tot - grid_area) < 2.0 * 100.0 * 200.0 / GN);
        /* 中心五边形被覆盖两次，按奇偶是洞：不得作为子环出现。
         * 5 个尖角 + 中心五边形分离后，子环数应恰为 5。 */
        CK(ns.rings == 5);
        double center = 0.0;
        for (int k = 0; k < ns.rings; ++k)
            if (ns.ring[k].n == 5) center += 1.0;
        CK(center == 0.0);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
