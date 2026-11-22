#include "arclen.h"
#include "adapt.h"
#include <math.h>
#include <stdio.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void test_legacy(void) {
    APoint p[3] = {{0,0},{10,0},{10,10}};
    ATable t;
    CK(arclen_build(p, 3, &t) == 0);
    CK(t.s[1] == 10.0 && t.s[2] == 20.0);
    CK(arclen_total(&t, p, 3) == 20.0);
    APoint m;
    CK(arclen_sample_at(p, 3, &t, 15.0, &m) == 0);
    CK(m.x > 9.9 && m.x < 10.1 && m.y > 4.9 && m.y < 5.1);
    CK(arclen_sample_at(p, 3, &t, -1.0, &m) == -2);
    CK(arclen_build(p, 1, &t) == -1);
}

static void test_adapt(void) {
    enum { N = 1001 };
    APoint pts[N];
    /* 半径 R=5 的整圆，曲率 k=0.2 */
    const double R = 5.0;
    for (int i = 0; i < N; ++i) {
        double a = 2.0 * M_PI * i / (N - 1);
        pts[i].x = R * cos(a);
        pts[i].y = R * sin(a);
    }
    double eps = 0.01;
    AdaptConfig cfg = { eps };
    AdaptiveTable t;
    CK(adapt_build(pts, N, &cfg, &t) == 0);
    CK(t.count >= 2);
    CK(t.max_interval <= sqrt(2.0 * eps / 0.2) + 1e-12);
    CK(t.kmax > 0.19 && t.kmax < 0.21);
    double err = adapt_verify(pts, N, &t, 5000);
    CK(err >= 0.0 && err <= eps);
    APoint q;
    CK(adapt_sample(&t, -1.0, &q) == -2);
    CK(adapt_sample(&t, t.total + 1.0, &q) == -2);
    CK(adapt_sample(&t, 0.0, &q) == 0);
    CK(fabs(q.x - pts[0].x) < 1e-9 && fabs(q.y - pts[0].y) < 1e-9);
    adapt_free(&t);

    /* 直线：曲率为零，间隔等于总长，两节点且零误差 */
    APoint line[3] = {{0,0},{5,0},{10,0}};
    CK(adapt_build(line, 3, &cfg, &t) == 0);
    CK(t.count == 2);
    CK(fabs(t.max_interval - 10.0) < 1e-12);
    err = adapt_verify(line, 3, &t, 100);
    CK(fabs(err) < 1e-12);
    adapt_free(&t);

    /* 近退化尖角三元组：实测 Menger 曲率 k=8e-10，近乎为零。
     * eps 宽松到 d=0.9*sqrt(2eps/k) 超过总长时必须拒绝（-2），
     * 本例阈值 eps>k*L^2/(2*0.9^2)≈4.94e-6。 */
    APoint near_deg[3] = {{0,0},{50,1e-6},{100,0}};
    AdaptConfig loose = { 1e-3 };
    CK(adapt_build(near_deg, 3, &loose, &t) == -2);
    /* 同一曲线 eps 收紧到阈值以下：建表成功，实测误差不超上界
     * （1e-13 高于该几何的双精度舍入地板 ~1.4e-14）。 */
    AdaptConfig tight2 = { 1e-13 };
    CK(adapt_build(near_deg, 3, &tight2, &t) == 0);
    err = adapt_verify(near_deg, 3, &t, 2000);
    CK(err >= 0.0 && err <= 1e-13);
    adapt_free(&t);

    /* 光滑圆 + 宽松 eps（阈值 eps>k*L^2/(2*0.9^2)≈122.6）：反解间隔大于总长，拒绝 */
    AdaptConfig very_loose = { 200.0 };
    CK(adapt_build(pts, N, &very_loose, &t) == -2);

    /* 参数报错：点少于二、eps 非正、空指针 */
    CK(adapt_build(pts, 1, &cfg, &t) == -1);
    AdaptConfig bad = { 0.0 };
    CK(adapt_build(pts, N, &bad, &t) == -1);
    CK(adapt_build(NULL, N, &cfg, &t) == -1);

    /* 零长度曲线报错 */
    APoint flat[2] = {{3,3},{3,3}};
    CK(adapt_build(flat, 2, &cfg, &t) == -1);
}

int main(void) {
    test_legacy();
    test_adapt();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
