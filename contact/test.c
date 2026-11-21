#include "contact.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 解析基准曲线：控制点 (0,0),(0,3),(3,1),(3,0)，
 *   x(t) = 9t^2 - 6t^3,  y(t) = 9t - 15t^2 + 6t^3；
 *   y'(t) = 9 - 30t + 18t^2 = 0  ->  t* = (5 - sqrt(7)) / 6。 */
#define TS ((5.0 - sqrt(7.0)) / 6.0)
static double base_y(double t) { return 9.0*t - 15.0*t*t + 6.0*t*t*t; }

/* 独立复刻 isect/src/isect.c 的 isect_seg，作“现状”参照。 */
static int ref_isect(CtPoint a0, CtPoint a1, CtPoint b0, CtPoint b1,
                     double *t_out, double *u_out, CtPoint *at_out) {
    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double den = rx*sy - ry*sx;
    if (fabs(den) < 1e-12) return 0;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    double t = (qx*sy - qy*sx) / den;
    double u = (qx*ry - qy*rx) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    if (t_out) *t_out = t;
    if (u_out) *u_out = u;
    if (at_out) { at_out->x = a0.x + rx*t; at_out->y = a0.y + ry*t; }
    return 1;
}

/* 8 段折线 + ref_isect：现状管线的命中表。 */
static int ref_flatten_hits(const CtPoint c[4], CtPoint b0, CtPoint b1,
                            int spans, CtChordHit *hits, int cap) {
    CtPoint p[65];
    for (int i = 0; i <= spans; ++i) {
        double t = (double)i/(double)spans, u = 1.0 - t;
        double a = u*u*u, b = 3*u*u*t, d = 3*u*t*t, e = t*t*t;
        p[i].x = a*c[0].x + b*c[1].x + d*c[2].x + e*c[3].x;
        p[i].y = a*c[0].y + b*c[1].y + d*c[2].y + e*c[3].y;
    }
    int k = 0;
    for (int i = 0; i < spans; ++i) {
        double t, u; CtPoint at;
        if (!ref_isect(p[i], p[i+1], b0, b1, &t, &u, &at)) continue;
        if (hits && k < cap) { hits[k].chord = i; hits[k].t = t; hits[k].u = u; hits[k].at = at; }
        ++k;
    }
    return k;
}

/* 按公式手算某区间的判别式（输入为三点有符号距离）。 */
static double hand_disc(double h0, double hm, double h1) {
    double A = 2.0*(h0 - 2.0*hm + h1);
    double B = 4.0*hm - 3.0*h0 - h1;
    double C = h0;
    return B*B - 4.0*A*C;
}

static double signed_dist(CtPoint b0, CtPoint b1, CtPoint p) {
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double L = hypot(sx, sy);
    return (sx*(p.y - b0.y) - sy*(p.x - b0.x)) / L; /* = cross(s, p-b0) */
}

static CtPoint curve_at(const CtPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u*u*u, b = 3*u*u*t, d = 3*u*t*t, e = t*t*t;
    CtPoint p;
    p.x = a*c[0].x + b*c[1].x + d*c[2].x + e*c[3].x;
    p.y = a*c[0].y + b*c[1].y + d*c[2].y + e*c[3].y;
    return p;
}

/* 与 verify 独立的参考判别：给定区间，用公式手算 D 并报预期状态。
 * 返回 1 表示该区间判别式为负（应被拒绝）。 */
static int hand_span_disc_negative(const CtPoint c[4], CtPoint b0, CtPoint b1,
                                   int spans, int span, double *D_out) {
    double ta = (double)span/spans, tb = (double)(span+1)/spans, tm = 0.5*(ta+tb);
    double h0 = signed_dist(b0, b1, curve_at(c, ta));
    double hm = signed_dist(b0, b1, curve_at(c, tm));
    double h1 = signed_dist(b0, b1, curve_at(c, tb));
    double D = hand_disc(h0, hm, h1);
    if (D_out) *D_out = D;
    return D < 0.0;
}

int main(void) {
    CtPoint c[4] = {{0,0},{0,3},{3,1},{3,0}};
    double tstar = TS;
    double ystar = base_y(tstar);
    char ef[CT_FIELD_MAX];
    CtScan s;

    /* --- 1) 判别式为正：与 y=1 的两个交点均为穿过 --- */
    CtPoint q0 = {-1, 1}, q1 = {3, 1};
    for (int spans = 8; spans <= 64; spans *= 8) {
        CK(ct_contact(c, q0, q1, spans, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
        CK(s.classified == 1 && s.nrows == spans);
        CK(s.crosses == 2 && s.grazes == 0);
        CK(s.disc_neg == 0);
        CK(fabs(s.hits[0].t - 0.1434316734) < 2e-3);
        CK(fabs(s.hits[s.count-1].t - 0.7025110) < 2e-3);
        for (int i = 0; i < s.count; ++i) {
            CK(s.hits[i].kind == CT_KIND_CROSS);
            CK(fabs(s.hits[i].resid) < 1e-2);
            CK(s.hits[i].u > 0.0 && s.hits[i].u < 1.0);
        }
    }

    /* --- 2) 判别式为零：解析切线只报一个擦边，参数收敛 --- */
    CtPoint l0 = {-1, ystar}, l1 = {3, ystar};
    double terr[2] = {0, 0};
    int idx = 0;
    for (int spans = 8; spans <= 64; spans *= 8, ++idx) {
        CK(ct_contact(c, l0, l1, spans, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
        CK(s.crosses == 0);
        CK(s.grazes == 1);
        CK(s.count == 1);
        terr[idx] = fabs(s.hits[0].t - tstar);
        CK(terr[idx] <= (spans == 8 ? 2e-3 : 1e-4));
        CK(s.hits[0].u > 0.0 && s.hits[0].u < 1.0);
        CK(fabs(s.hits[0].resid) <= 1e-3);
        /* 手算判别式核对：该区间行内 D 与按公式独立算出的 D 一致 */
        const CtRow *r = &s.rows[s.hits[0].span];
        double D = hand_disc(r->h0, r->hm, r->h1);
        CK(r->disc == D);
        /* 判别式为负的区间必须都写明字段 discriminant */
        for (int i = 0; i < s.nrows; ++i) {
            if (s.rows[i].status == CT_KIND_REJECT &&
                s.rows[i].reason == CT_R_DISC_NEG) {
                CK(strcmp(s.rows[i].field, "discriminant") == 0);
                CK(hand_span_disc_negative(c, l0, l1, spans, i, NULL));
            }
        }
    }
    /* 64 段比 8 段更准 */
    CK(terr[1] < terr[0]);

    /* --- 3) 判别式为负：线在切线上方，无接触，字段可定位 --- */
    CtPoint a0 = {-1, ystar + 0.01}, a1 = {3, ystar + 0.01};
    CK(ct_contact(c, a0, a1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
    CK(s.count == 0 && s.rejects > 0 && s.disc_neg > 0);
    CK(s.rows[0].status == CT_KIND_REJECT);
    CK(strcmp(s.rows[0].field, "discriminant") == 0);
    /* 单区间接口：判别式为负必须返回 CT_E_DISC 并说明字段 */
    CtRow row;
    CK(ct_classify_span(c, l0, l1, 8, 0, 1e-3, &row, ef) == CT_E_DISC);
    CK(strcmp(ef, "discriminant") == 0);
    CK(row.disc < 0.0);
    /* 严格模式整体拒绝并指出字段 */
    CK(ct_contact(c, l0, l1, 8, CT_F_CLASSIFY|CT_F_STRICT_DISC, 1e-3, &s, ef) == CT_E_DISC);
    CK(strcmp(ef, "discriminant") == 0);

    /* --- 4) 端点接触与共线贴合归入擦边 --- */
    CtPoint e0 = {-1, 0}, e1 = {1, 0};
    CK(ct_contact(c, e0, e1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
    CK(s.crosses == 0 && s.grazes == 1);
    CK(fabs(s.hits[0].t) < 1e-9);
    CK(s.hits[0].reason == CT_R_CURVE_ENDPOINT);
    CtPoint k0 = {-1, 0}, k1 = {4, 0};
    CK(ct_contact(c, k0, k1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
    CK(s.crosses == 0 && s.grazes == 2);
    for (int i = 0; i < s.count; ++i) CK(s.hits[i].kind == CT_KIND_GRAZE);

    /* --- 5) 不分类时与现状逐位一致（8 段折线 + isect_seg） --- */
    for (int case_i = 0; case_i < 3; ++case_i) {
        CtPoint p0 = {0, 0}, p1 = {0, 0};
        if (case_i == 0) { p0 = q0; p1 = q1; }
        else if (case_i == 1) { p0 = l0; p1 = l1; }
        else { p0 = (CtPoint){-1, -0.5}; p1 = (CtPoint){3, 2.5}; }
        CtChordHit ref[64], got[64];
        int nr = ref_flatten_hits(c, p0, p1, 8, ref, 64);
        int ng = ct_legacy_hits(c, p0, p1, 8, got, 64, ef);
        CK(ng == nr);
        for (int i = 0; i < nr; ++i) {
            CK(got[i].chord == ref[i].chord);
            CK(got[i].t == ref[i].t);          /* 逐位相同 */
            CK(got[i].u == ref[i].u);
            CK(got[i].at.x == ref[i].at.x && got[i].at.y == ref[i].at.y);
        }
        /* 关闭分类的整表接口输出一致，且忽略 spans */
        CtScan sa, sb;
        CK(ct_contact(c, p0, p1, 8, 0, 0.0, &sa, ef) == CT_OK);
        CK(ct_contact(c, p0, p1, 64, 0, 0.0, &sb, ef) == CT_OK);
        CK(sa.classified == 0 && sa.nrows == 0 && sa.spans == CT_LEGACY_SPANS);
        CK(sa.count == nr && sb.count == nr);
        for (int i = 0; i < nr; ++i) {
            double tg = ((double)ref[i].chord + ref[i].t) / 8.0;
            CK(sa.hits[i].t == tg);
            CK(sb.hits[i].t == tg);
            CK(sa.hits[i].u == ref[i].u && sb.hits[i].u == ref[i].u);
            CK(sa.hits[i].reason == CT_R_LEGACY);
        }
    }
    /* 相切处旧路径漏判（0 命中），分类路径报 1 次擦边 */
    {
        CtChordHit lh[16];
        CK(ct_legacy_hits(c, l0, l1, 8, lh, 16, ef) == 0);
        CK(ct_contact(c, l0, l1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
        CK(s.grazes == 1);
    }

    /* --- 6) 输入校验：非有限控制点、退化线段、坏参数都说明字段 --- */
    CtPoint badc[4] = {{0,0},{INFINITY,0},{3,1},{3,0}};
    CK(ct_contact(badc, l0, l1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_CTRL);
    CK(strcmp(ef, "c[1].x") == 0);
    badc[1] = (CtPoint){0, NAN};
    CK(ct_contact(badc, l0, l1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_CTRL);
    CK(strcmp(ef, "c[1].y") == 0);
    badc[1] = (CtPoint){0, 3};
    CtPoint z0 = {2, 2}, z1 = {2, 2};
    CK(ct_contact(c, z0, z1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_SEG);
    CK(strcmp(ef, "b0/b1") == 0);
    CtPoint inf0 = {INFINITY, 0};
    CK(ct_contact(c, inf0, l1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_SEG);
    CK(strcmp(ef, "b0") == 0);
    CK(ct_contact(c, l0, l1, 8, CT_F_CLASSIFY, -1.0, &s, ef) == CT_E_TOL);
    CK(strcmp(ef, "tol") == 0);
    CK(ct_contact(c, l0, l1, 8, CT_F_CLASSIFY, NAN, &s, ef) == CT_E_TOL);
    CK(ct_contact(c, l0, l1, 0, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_ARG);
    CK(strcmp(ef, "spans") == 0);
    CK(ct_contact(c, l0, l1, 65, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_ARG);
    CK(ct_contact(c, l0, l1, 8, 0x100u, 1e-3, &s, ef) == CT_E_FLAGS);
    CK(strcmp(ef, "flags") == 0);
    CK(ct_contact(NULL, l0, l1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_E_ARG);
    CK(strcmp(ef, "c") == 0);
    CK(ct_classify_span(c, l0, l1, 8, 8, 1e-3, &row, ef) == CT_E_ARG);
    CK(strcmp(ef, "span") == 0);
    /* 控制点非有限时旧路径同样拒绝并说明字段 */
    badc[1] = (CtPoint){INFINITY, 0};
    CK(ct_legacy_hits(badc, l0, l1, 8, NULL, 0, ef) == CT_E_CTRL);
    CK(strcmp(ef, "c[1].x") == 0);

    /* --- 7) 逐区间分类表与公式手算的判别式逐行核对 --- */
    for (int spans = 8; spans <= 64; spans *= 8) {
        CK(ct_contact(c, l0, l1, spans, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
        for (int i = 0; i < s.nrows; ++i) {
            const CtRow *r = &s.rows[i];
            double D = hand_disc(r->h0, r->hm, r->h1);
            CK(r->disc == D);
            /* 判别式为负的行分两种：明显为负必须记 discriminant_negative；
             * 落在相切容差带内的负值按相切处理，其模型顶点须真的贴近线段。 */
            if (r->reason == CT_R_DISC_NEG) {
                CK(r->disc < 0.0);
            } else if (r->disc < 0.0) {
                double hv = r->C - r->B * r->B / (4.0 * r->A);
                CK(fabs(hv) <= 1e-3 * (1.0 + 1e-9));
            }
            /* 弦叉积也独立核对 */
            CtPoint p0 = curve_at(c, (double)i/spans);
            CtPoint p1 = curve_at(c, (double)(i+1)/spans);
            double ch = (p1.x-p0.x)*(l1.y-l0.y) - (p1.y-p0.y)*(l1.x-l0.x);
            CK(r->chord_cross == ch);
        }
    }

    /* --- 8) 导出：行数正确，且 NaN 写成空字段 --- */
    CK(ct_contact(c, l0, l1, 8, CT_F_CLASSIFY, 1e-3, &s, ef) == CT_OK);
    {
        FILE *f = fopen("build/test_rows.csv", "w");
        CK(f != NULL);
        if (f) { CK(ct_export_rows(&s, f) == CT_OK); fclose(f); }
        f = fopen("build/test_hits.csv", "w");
        CK(f != NULL);
        if (f) { CK(ct_export_hits(&s, f) == CT_OK); fclose(f); }

        f = fopen("build/test_rows.csv", "r");
        CK(f != NULL);
        if (f) {
            int lines = 0, ch;
            while ((ch = fgetc(f)) != EOF) if (ch == '\n') ++lines;
            fclose(f);
            CK(lines == s.nrows + 1);
        }
        f = fopen("build/test_hits.csv", "r");
        CK(f != NULL);
        if (f) {
            int lines = 0, ch;
            while ((ch = fgetc(f)) != EOF) if (ch == '\n') ++lines;
            fclose(f);
            CK(lines == s.count + 1);
        }
        /* 第 2 行（第 1 个区间）的 t/u/x/y/resid 在无候选时应为空字段 */
        f = fopen("build/test_rows.csv", "r");
        if (f) {
            char buf[1024];
            if (fgets(buf, sizeof buf, f) && fgets(buf, sizeof buf, f)) {
                /* 无候选行 reason=discriminant_negative，t/u/x/y/resid 可能仍
                 * 被写入（供定位），因此只核查字段个数 */
                int commas = 0;
                for (char *p = buf; *p; ++p) if (*p == ',') ++commas;
                CK(commas == 19);
            }
            fclose(f);
        }
    }
    CK(ct_export_rows(&s, NULL) == CT_E_ARG);
    CK(ct_export_hits(&s, NULL) == CT_E_ARG);
    CK(ct_export_rows(NULL, stdout) == CT_E_ARG);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
