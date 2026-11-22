#include "budget.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* x 左侧（已分配部分）的边际收益 */
static double slope_below(const BCurve *c, double x) {
    double acc = 0.0, s = c->slope[0];
    for (int k = 0; k < c->nseg; ++k) {
        if (acc < x) s = c->slope[k];
        acc += c->width[k];
    }
    return s;
}

int main(void) {
    BCurve cs[BUDGET_MAX_MOD];
    double tiers[8];
    int n = budget_std_curves(cs, BUDGET_MAX_MOD);
    int nt = budget_std_tiers(tiers, 8);
    CK(n == 6);
    CK(nt == 4);

    /* 非法收益曲线：拒绝并说明 */
    BCurve bad_width = {"bad_width", 1, {0.0}, {1.0}};
    CK(budget_curve_check(&bad_width) == BUDGET_ERR_WIDTH);
    BCurve bad_slope = {"bad_slope", 1, {10.0}, {-1.0}};
    CK(budget_curve_check(&bad_slope) == BUDGET_ERR_SLOPE);
    BCurve bad_convex = {"bad_convex", 2, {10.0, 10.0}, {1.0, 2.0}};
    CK(budget_curve_check(&bad_convex) == BUDGET_ERR_CONVEX);
    BCurve bad_nseg = {"bad_nseg", 0, {0.0}, {0.0}};
    CK(budget_curve_check(&bad_nseg) == BUDGET_ERR_NSEG);
    BAlloc al[BUDGET_MAX_MOD];
    CK(budget_lagrange(&bad_convex, 1, 10.0, al, NULL, NULL, NULL) == BUDGET_ERR_CONVEX);
    CK(budget_equal(&bad_convex, 1, 10.0, al) == BUDGET_ERR_CONVEX);

    /* 预算为零或为负：拒绝并说明 */
    CK(budget_lagrange(cs, n, 0.0, al, NULL, NULL, NULL) == BUDGET_ERR_BUDGET);
    CK(budget_lagrange(cs, n, -5.0, al, NULL, NULL, NULL) == BUDGET_ERR_BUDGET);
    CK(budget_equal(cs, n, 0.0, al) == BUDGET_ERR_BUDGET);
    CK(strlen(budget_strerror(BUDGET_ERR_BUDGET)) > 0);
    CK(strlen(budget_strerror(BUDGET_ERR_CONVEX)) > 0);

    /* 收益恒为零的模块：单独列出，不参与分配 */
    int zidx[BUDGET_MAX_MOD], nz = 0;
    double lam = 0.0;
    CK(budget_lagrange(cs, n, tiers[0], al, zidx, &nz, &lam) == BUDGET_OK);
    CK(nz == 2);
    CK(al[zidx[0]].x == 0.0 && al[zidx[1]].x == 0.0);
    CK(budget_curve_is_zero(&cs[zidx[0]]));
    CK(budget_curve_is_zero(&cs[zidx[1]]));
    CK(!budget_curve_is_zero(&cs[0]));

    /* 四档预算：守恒、边际收益相等（KKT）、总收益比均分高两成 */
    BAlloc eq[BUDGET_MAX_MOD];
    for (int t = 0; t < nt; ++t) {
        CK(budget_lagrange(cs, n, tiers[t], al, zidx, &nz, &lam) == BUDGET_OK);
        CK(budget_equal(cs, n, tiers[t], eq) == BUDGET_OK);
        double sum = 0.0, ltot = 0.0, etot = 0.0, cap = 0.0;
        for (int i = 0; i < n; ++i) {
            sum += al[i].x;
            ltot += al[i].gain;
            etot += eq[i].gain;
            if (!budget_curve_is_zero(&cs[i])) cap += budget_curve_cap(&cs[i]);
        }
        double want = tiers[t] < cap ? tiers[t] : cap;
        CK(fabs(sum - want) < 1e-9);
        for (int i = 0; i < n; ++i) {
            if (budget_curve_is_zero(&cs[i])) continue;
            if (al[i].x > 0.0)
                CK(slope_below(&cs[i], al[i].x) >= lam - 1e-9);
            if (al[i].x < budget_curve_cap(&cs[i]) - 1e-12)
                CK(slope_below(&cs[i], al[i].x + 1e-9) <= lam + 1e-9);
        }
        CK(ltot >= 1.2 * etot - 1e-9);
    }

    /* 关掉新逻辑：旧版均分报告逐字节不变 */
    FILE *g = fopen("gold/equal.txt", "rb");
    CK(g != NULL);
    if (g) {
        FILE *tmp = tmpfile();
        CK(tmp != NULL);
        if (tmp) {
            CK(budget_report(tmp, cs, n, tiers, nt, 0) == BUDGET_OK);
            rewind(tmp);
            int same = 1, b1, b2;
            do {
                b1 = fgetc(tmp);
                b2 = fgetc(g);
                if (b1 != b2) same = 0;
            } while (b1 != EOF && b2 != EOF);
            CK(same);
            fclose(tmp);
        }
        fclose(g);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
