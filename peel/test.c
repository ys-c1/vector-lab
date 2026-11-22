#include "peel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int run(const PPoint *pts, int n, PLayer *layers, int cap, PSummary *sum) {
    PPeelOut out;
    memset(&out, 0, sizeof out);
    out.layers = layers;
    out.layer_cap = cap;
    return peel_run(pts, n, &out, sum);
}

static int near(double a, double b, double rel) {
    return fabs(a - b) <= rel * fmax(1.0, fabs(b));
}

int main(void) {
    /* 1. 单层正方形：面积 100，剥完 */
    {
        PPoint p[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 4, L, 8, &s) == 0);
        CK(s.layer_count == 1);
        CK(s.unique_points == 4);
        CK(L[0].layer == 1 && L[0].hull_verts == 4 && L[0].removed == 4);
        CK(near(L[0].area, 100.0, 1e-12));
        CK(s.stop.reason == PEEL_STOP_DONE && s.stop.layer == 2);
    }

    /* 2. 同心两环：外 24 边形套内 24 边形，两层，面积单调不增 */
    {
        PPoint p[48];
        int k = 0;
        for (int r = 0; r < 2; ++r)
            for (int i = 0; i < 24; ++i) {
                double th = 2.0 * 3.141592653589793 * i / 24.0;
                double rad = r == 0 ? 10.0 : 4.0;
                p[k].x = rad * cos(th);
                p[k].y = rad * sin(th);
                ++k;
            }
        PLayer L[8];
        PSummary s;
        CK(run(p, 48, L, 8, &s) == 0);
        CK(s.layer_count == 2);
        CK(L[1].area < L[0].area);
        CK(L[1].input_unique == 24);
        CK(s.stop.reason == PEEL_STOP_DONE);
    }

    /* 3. 重复点：完全重合的点只剥一次，removed 计入重复 */
    {
        PPoint p[8] = {{0, 0}, {4, 0}, {4, 4}, {0, 4},
                       {0, 0}, {4, 0}, {4, 4}, {0, 4}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 8, L, 8, &s) == 0);
        CK(s.input_points == 8 && s.unique_points == 4);
        CK(s.layer_count == 1);
        CK(L[0].hull_verts == 4);
        CK(L[0].removed == 4); /* 去重后边界点数 */
        CK(near(L[0].area, 16.0, 1e-12));
    }

    /* 4. 边上共线点也随本层剥掉，不算凸包顶点 */
    {
        PPoint p[6] = {{0, 0}, {5, 0}, {10, 0}, {10, 4}, {0, 4}, {5, 2}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 6, L, 8, &s) == 0);
        CK(L[0].hull_verts == 4);
        CK(L[0].removed == 5);      /* 4 个角 + 边上 (5,0) */
        CK(s.layer_count == 1);     /* 剩 (5,2) 一个点，第二层不足三点 */
        CK(s.stop.reason == PEEL_STOP_FEW && s.stop.layer == 2);
        CK(s.stop.remaining == 1);
    }

    /* 5. 全部共线：首层就拒，层号 1 */
    {
        PPoint p[5] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 5, L, 8, &s) == 2);
        CK(s.layer_count == 0);
        CK(s.stop.reason == PEEL_STOP_COLLINEAR && s.stop.layer == 1);
    }

    /* 6. 不足三点：空集与单点 */
    {
        PLayer L[8];
        PSummary s;
        PPoint one[1] = {{1, 2}};
        CK(run(one, 1, L, 8, &s) == 3);
        CK(s.stop.reason == PEEL_STOP_FEW && s.stop.layer == 1);
        CK(run(NULL, 0, L, 8, &s) == 3);
        CK(s.stop.layer == 1);
    }

    /* 6b. 后段共线：外层四方环剥掉后，里面只剩一条直线 -> 第 2 层拒 */
    {
        PPoint p[8] = {{0, 0}, {10, 0}, {10, 10}, {0, 10},
                       {2, 5}, {4, 5}, {6, 5}, {8, 5}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 8, L, 8, &s) == 0);
        CK(s.layer_count == 1);
        CK(s.stop.reason == PEEL_STOP_COLLINEAR && s.stop.layer == 2);
        CK(s.stop.remaining == 4);
    }

    /* 6c. 后段只剩两点：第 2 层按不足三点拒，层号正确 */
    {
        PPoint p[6] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {3, 3}, {7, 7}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 6, L, 8, &s) == 0);
        CK(s.layer_count == 1);
        CK(s.stop.reason == PEEL_STOP_FEW && s.stop.layer == 2 && s.stop.remaining == 2);
    }

    /* 7. 容量只够一层：第 2 层拒绝（不静默截断），层号与原因都要报 */
    {
        PPoint p[8] = {{0, 0}, {10, 0}, {10, 10}, {0, 10},
                       {2, 2}, {8, 2}, {8, 8}, {2, 8}};
        PLayer L[1]; /* 容量只够一层 */
        PSummary s;
        CK(run(p, 8, L, 1, &s) == -4);
        CK(s.layer_count == 1);
        CK(s.stop.reason == PEEL_STOP_CAPACITY && s.stop.layer == 2);
    }

    /* 8. 非有限坐标拒绝；点数超限拒绝 */
    {
        PPoint p[3] = {{0, 0}, {1, 0}, {0, INFINITY}};
        PLayer L[8];
        PSummary s;
        CK(run(p, 3, L, 8, &s) == -1);
        CK(run(NULL, 3, L, 8, &s) == -1);
        CK(run(p, -1, L, 8, &s) == -1);
        CK(run(p, PEEL_MAX_POINTS + 1, L, 8, &s) == -1);
    }

    /* 9. 反解：面积各半的层序列 */
    {
        PLayer L[4];
        memset(L, 0, sizeof L);
        L[0].area = 100.0;
        L[1].area = 40.0; /* 剥完第 1 层，剩余 40% */
        L[2].area = 10.0;
        L[3].area = 0.5;
        double ratio = -1.0;
        CK(peel_cover_layer(L, 4, 0.95, &ratio) == 3); /* 剥 3 层后剩 0.5% */
        CK(near(ratio, 0.005, 1e-12));
        CK(peel_cover_layer(L, 4, 0.5, &ratio) == 1);  /* 剥 1 层剩 40% <= 50% */
        CK(peel_cover_layer(L, 4, 0.6, &ratio) == 1);  /* 同上，门槛放宽 1 层够 */
        CK(peel_cover_layer(L, 4, 0.0, &ratio) == 0);
        CK(peel_cover_layer(L, 0, 0.95, &ratio) == -1);
        CK(peel_cover_layer(NULL, 4, 0.95, &ratio) == -1);
    }

    /* 10. 面积单调性在随机点集上复核；反解层号在有效范围 */
    {
        enum { N = 300 };
        PPoint p[N];
        unsigned long long st = 12345ULL;
        for (int i = 0; i < N; ++i) {
            st += 0x9E3779B97F4A7C15ULL;
            unsigned long long z = st;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z ^= z >> 31;
            p[i].x = (double)(z % 100000);
            p[i].y = (double)((z >> 20) % 100000);
        }
        PLayer L[PEEL_MAX_LAYERS];
        PSummary s;
        CK(run(p, N, L, PEEL_MAX_LAYERS, &s) == 0);
        CK(s.layer_count > 3);
        int mono = 1;
        for (int i = 1; i < s.layer_count; ++i)
            if (L[i].area > L[i - 1].area) mono = 0;
        CK(mono);
        double ratio;
        int k = peel_cover_layer(L, s.layer_count, 0.95, &ratio);
        CK(k >= 1 && k <= s.layer_count);
        CK(ratio <= 0.05 + 1e-12);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
