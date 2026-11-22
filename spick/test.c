#include "spick.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SP_HALFDIAG_TEST 0.70710678118654752440

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int feq(double a, double b, double tol) { return fabs(a - b) <= tol; }

/* 旋转+等比缩放：x = k*cos*u - k*sin*v + tx, y = k*sin*u + k*cos*v + ty */
static SpForm sim(double k, double th, double tx, double ty) {
    SpForm m;
    m.a = k * cos(th);  m.b = k * sin(th);
    m.c = -k * sin(th); m.d = k * cos(th);
    m.e = tx;           m.f = ty;
    return m;
}

int main(void) {
    char ef[SP_FIELD_MAX];
    SpPick p;

    /* --- 1. 雅可比：相似变换 sigma_max=sigma_min=1/k，det=k^2 --- */
    {
        SpForm m = sim(2.0, 0.7, 10.0, -4.0);
        SpJac j;
        CK(sp_jacobian(&m, &j, ef) == SP_OK);
        CK(feq(j.det, 4.0, 1e-12));
        CK(feq(j.sigma_max, 0.5, 1e-12));
        CK(feq(j.sigma_min, 0.5, 1e-12));
        CK(feq(j.kappa, 1.0, 1e-12));
        CK(j.singular == 0 && j.field[0] == '\0');
    }

    /* --- 2. 非均匀缩放：奇异值为 1/sx、1/sy --- */
    {
        SpForm m = {3, 0, 0, 0.5, 0, 0};
        SpJac j;
        CK(sp_jacobian(&m, &j, NULL) == SP_OK);
        CK(feq(j.sigma_max, 2.0, 1e-12));
        CK(feq(j.sigma_min, 1.0 / 3.0, 1e-12));
        CK(feq(j.kappa, 6.0, 1e-12));
        CK(feq(j.fro2, 4.0 + 1.0 / 9.0, 1e-12));
    }

    /* --- 3. 旋转不改变奇异值 --- */
    {
        SpForm m = sim(1.0, 1.234, 0, 0);
        SpJac j;
        CK(sp_jacobian(&m, &j, NULL) == SP_OK);
        CK(feq(j.sigma_max, 1.0, 1e-12) && feq(j.sigma_min, 1.0, 1e-12));
    }

    /* --- 4. 一般仿射：与手算的 2x2 奇异值一致 --- */
    {
        /* L = [[2,1],[0,0.5]]；L^T L = [[4,2],[2,1.25]] */
        SpForm m = {2, 0, 1, 0.5, 0, 0};
        SpJac j;
        CK(sp_jacobian(&m, &j, NULL) == SP_OK);
        /* 特征值 = (5.25 ± sqrt(27.5625 - 4*(4*1.25-4)))/2 = (5.25 ± sqrt(27.5625))/2 */
        double disc = sqrt(27.5625 - 4.0 * (4.0 * 1.25 - 2.0 * 2.0));
        double lmax = (5.25 + disc) / 2.0;
        double lmin = (5.25 - disc) / 2.0;
        CK(feq(j.sigma_max, 1.0 / sqrt(lmin), 1e-12));
        CK(feq(j.sigma_min, 1.0 / sqrt(lmax), 1e-12));
    }

    /* --- 5. 奇异：det=0 -> SP_E_SINGULAR, field=det --- */
    {
        SpForm m = {1, 2, 2, 4, 0, 0}; /* det = 4-4 = 0 */
        SpJac j;
        strcpy(ef, "");
        CK(sp_jacobian(&m, &j, ef) == SP_E_SINGULAR);
        CK(strcmp(ef, "det") == 0);
        CK(j.singular == 1 && strcmp(j.field, "det") == 0);
    }

    /* --- 6. 退化：非有限系数 -> 字段 a..f --- */
    {
        SpForm m = {1, 0, 0, 1, 0, INFINITY};
        SpJac j;
        strcpy(ef, "");
        CK(sp_jacobian(&m, &j, ef) == SP_E_SINGULAR);
        CK(strcmp(ef, "f") == 0 && strcmp(j.field, "f") == 0);
        m = (SpForm){NAN, 0, 0, 1, 0, 0};
        CK(sp_jacobian(&m, &j, ef) == SP_E_SINGULAR && strcmp(ef, "a") == 0);
        m = (SpForm){0, 0, 0, 1e300, 0, 0};  /* det 溢出为 inf */
        CK(sp_jacobian(&m, &j, ef) == SP_E_SINGULAR && strcmp(ef, "det") == 0);
    }

    /* --- 7. 屏幕容差：命中边界随缩放正确移动 --- */
    {
        SpForm m = sim(2.0, 0.5, 100.0, 50.0);
        SpSeg segs[1];
        SpPoint back = {3.0, 4.0};
        /* 形状距离 10 的段 */
        segs[0].p0 = (SpPoint){back.x, back.y - 10};
        segs[0].p1 = (SpPoint){back.x + 2.0, back.y - 10};
        SpPoint scr = sp_map(&m, back);
        /* sigma_max = 0.5 -> tol_shape = tol_px/2 */
        CK(sp_pick(segs, 1, &m, scr, 21.0, SP_F_SCREEN_TOL, &p, ef) == SP_OK);
        CK(p.count == 1 && p.mode == SP_TOL_MODE_JACOBIAN);
        CK(feq(p.tol_shape, 10.5, 1e-12));
        CK(p.rows[0].hit == 1 && feq(p.rows[0].r_px, 20.0, 1e-12));
        CK(sp_pick(segs, 1, &m, scr, 19.0, SP_F_SCREEN_TOL, &p, ef) == SP_OK);
        CK(p.count == 0);
    }

    /* --- 8. 旧路径与“现状”一致：tol 直接当形状单位 --- */
    {
        SpForm m = sim(4.0, 0.3, -7.0, 2.0);
        SpSeg segs[2];
        SpPoint back = {-1.0, 2.0};
        segs[0].p0 = (SpPoint){back.x, back.y - 3};
        segs[0].p1 = (SpPoint){back.x + 1, back.y - 3};
        segs[1].p0 = (SpPoint){back.x, back.y - 30};
        segs[1].p1 = (SpPoint){back.x + 1, back.y - 30};
        SpPoint scr = sp_map(&m, back);
        SpPick a, b;
        CK(sp_pick(segs, 2, &m, scr, 3.0, 0, &a, ef) == SP_OK);
        CK(sp_pick_legacy(segs, 2, &m, scr, 3.0, &b, ef) == SP_OK);
        CK(a.count == b.count && a.count == 1);
        CK(a.rows[0].hit == b.rows[0].hit && a.rows[1].hit == b.rows[1].hit);
        CK(a.rows[0].dist == b.rows[0].dist && a.rows[0].u == b.rows[0].u);
        CK(a.tol_shape == 3.0 && a.mode == SP_TOL_MODE_LEGACY);
        CK(a.band_n == 0);
    }

    /* --- 9. 输入校验与报错字段 --- */
    {
        SpForm m = sim(1, 0, 0, 0);
        SpSeg segs[1] = {{{0, 0}, {1, 0}}};
        SpPoint scr = {0, 0};
        strcpy(ef, "");
        CK(sp_pick(NULL, 1, &m, scr, 1, 0, &p, ef) == SP_E_ARG &&
           strcmp(ef, "segs") == 0);
        CK(sp_pick(segs, 0, &m, scr, 1, 0, &p, ef) == SP_E_ARG &&
           strcmp(ef, "n") == 0);
        CK(sp_pick(segs, 1, &m, scr, -1, 0, &p, ef) == SP_E_TOL &&
           strcmp(ef, "tol") == 0);
        CK(sp_pick(segs, 1, &m, scr, 1, 0x80u, &p, ef) == SP_E_FLAGS &&
           strcmp(ef, "flags") == 0);
        SpSeg bad[1] = {{{NAN, 0}, {1, 0}}};
        CK(sp_pick(bad, 1, &m, scr, 1, 0, &p, ef) == SP_E_SEG &&
           strcmp(ef, "segs") == 0);
        SpPoint badscr = {NAN, 0};
        CK(sp_pick(segs, 1, &m, badscr, 1, 0, &p, ef) == SP_E_SEG &&
           strcmp(ef, "scr") == 0);
        SpForm sing = {1, 1, 1, 1, 0, 0};
        CK(sp_pick(segs, 1, &sing, scr, 1, SP_F_SCREEN_TOL, &p, ef) ==
           SP_E_SINGULAR && strcmp(ef, "det") == 0);
        CK(sp_unmap(&sing, scr, &scr, ef) == SP_E_SINGULAR &&
           strcmp(ef, "det") == 0);
    }

    /* --- 10. 屏幕容差不随缩放漂移（相似变换）。
     * 段：屏幕空间恒在距离 10 px 处（形状距离 10/k，拾取点随变换走）。 --- */
    {
        double ks[4] = {0.25, 1.0, 4.0, 16.0};
        double ths[4] = {0.0, 0.9, 2.4, 5.1};
        for (int i = 0; i < 4; ++i) {
            SpForm m = sim(ks[i], ths[i], 300.0, 200.0);
            SpPoint back = {2.0, -3.0};
            SpPoint scr = sp_map(&m, back);
            /* 形状空间沿 (1,0) 距 back 10/k 处放一段平行段：屏幕距离恒 10 */
            double d = 10.0 / ks[i];
            SpSeg segs[1];
            segs[0].p0 = (SpPoint){back.x + d, back.y - 5.0};
            segs[0].p1 = (SpPoint){back.x + d, back.y + 5.0};
            CK(sp_pick(segs, 1, &m, scr, 10.5, SP_F_SCREEN_TOL, &p, ef) == SP_OK);
            CK(p.rows[0].hit == 1);
            CK(feq(p.rows[0].r_px, 10.0, 1e-9));
            CK(sp_pick(segs, 1, &m, scr, 9.5, SP_F_SCREEN_TOL, &p, ef) == SP_OK);
            CK(p.rows[0].hit == 0);
        }
    }

    /* --- 11. 不一致上界：band_n 必须覆盖实测全部不一致 --- */
    for (int trial = 0; trial < 20; ++trial) {
        SpSeg segs[48];
        unsigned long long rs = 12345ULL + (unsigned long long)trial * 7919ULL;
        for (int i = 0; i < 48; ++i) {
            rs = rs * 6364136223846793005ULL + 1442695040888963407ULL;
            double cx = (double)((rs >> 11) % 2000ULL) / 10.0;
            rs = rs * 6364136223846793005ULL + 1442695040888963407ULL;
            double cy = (double)((rs >> 11) % 2000ULL) / 10.0;
            segs[i].p0 = (SpPoint){cx, cy};
            segs[i].p1 = (SpPoint){cx + 3.0, cy + 1.5};
        }
        /* 混合缩放/旋转/错切，各向异性 kappa 各异 */
        SpForm m = {1.5 + 0.1 * trial, 0.2, 0.4,
                    0.8 + 0.05 * trial, 17.0, -9.0};
        SpPoint scr = {(double)(trial * 13 % 90) + 40.0,
                       (double)(trial * 7 % 60) + 30.0};
        SpJac j;
        if (sp_jacobian(&m, &j, ef) != SP_OK) continue;
        CK(sp_pick(segs, 48, &m, scr, 2.0, SP_F_SCREEN_TOL, &p, ef) == SP_OK);
        /* 逐段用屏幕真实距离与像素半对角线判定参考结论的确定域 */
        int certain_in = 0, certain_out = 0, may = 0;
        for (int i = 0; i < 48; ++i) {
            double r = p.rows[i].r_px;
            if (r <= 2.0 - SP_HALFDIAG_TEST) ++certain_in;
            else if (r > 2.0 + SP_HALFDIAG_TEST) ++certain_out;
            else ++may;
        }
        CK(p.band_n == may);
        CK(p.count >= certain_in);
        CK(p.count <= 48 - certain_out);
    }

    /* --- 12. 导出 --- */
    {
        SpForm m = sim(1, 0, 0, 0);
        SpSeg segs[2] = {{{0, 0}, {1, 0}}, {{0, 5}, {1, 5}}};
        SpPoint scr = {0, 0};
        CK(sp_pick(segs, 2, &m, scr, 1.0, 0, &p, ef) == SP_OK);
        FILE *fp = tmpfile();
        CK(fp != NULL);
        if (fp) {
            CK(sp_export_rows(&p, fp, 0) == SP_OK);
            fclose(fp);
        }
        CK(sp_export_rows(&p, NULL, 0) == SP_E_ARG);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
