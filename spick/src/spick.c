#include "spick.h"
#include <math.h>
#include <string.h>

#define SP_EPS 1e-12
#define SP_SING_EPS 1e-14
#define SP_HALFDIAG 0.70710678118654752440 /* sqrt(2)/2：像素中心到像素角 */

static int finite2(double x, double y) { return isfinite(x) && isfinite(y); }

static void set_field(char ef[SP_FIELD_MAX], const char *name) {
    if (!ef) return;
    if (!name) name = "";
    strncpy(ef, name, SP_FIELD_MAX - 1);
    ef[SP_FIELD_MAX - 1] = '\0';
}

const char *sp_strerror(int code) {
    switch (code) {
    case SP_OK: return "ok";
    case SP_E_ARG: return "bad argument";
    case SP_E_SEG: return "bad segment";
    case SP_E_TOL: return "bad tolerance";
    case SP_E_FLAGS: return "bad flags";
    case SP_E_SINGULAR: return "singular transform";
    case SP_E_CAP: return "too many hits";
    default: return "unknown";
    }
}

const char *sp_reason_name(int reason) {
    switch (reason) {
    case SP_R_NONE: return "none";
    case SP_R_INSIDE: return "inside";
    case SP_R_OUTSIDE: return "outside";
    default: return "?";
    }
}

SpPoint sp_map(const SpForm *m, SpPoint p) {
    SpPoint r;
    r.x = m->a * p.x + m->c * p.y + m->e;
    r.y = m->b * p.x + m->d * p.y + m->f;
    return r;
}

/* J = M^{-1} 的线性部分 = (1/det)[[d,-c],[-b,a]]；det 已校验非零有限。 */
static void jac_of(const SpForm *m, double det, double J[4]) {
    J[0] = m->d / det;
    J[1] = -m->c / det;
    J[2] = -m->b / det;
    J[3] = m->a / det;
}

int sp_jacobian(const SpForm *m, SpJac *out, char ef[SP_FIELD_MAX]) {
    static const char *names[6] = {"a", "b", "c", "d", "e", "f"};
    if (!m) { set_field(ef, "m"); return SP_E_ARG; }
    SpJac j;
    memset(&j, 0, sizeof j);

    /* 逐字段有效性：非有限系数直接指出字段名 */
    const double vals[6] = {m->a, m->b, m->c, m->d, m->e, m->f};
    for (int i = 0; i < 6; ++i) {
        if (!isfinite(vals[i])) {
            j.singular = 1;
            j.field = names[i];
            if (out) *out = j;
            set_field(ef, names[i]);
            return SP_E_SINGULAR;
        }
    }

    double det = m->a * m->d - m->b * m->c;
    j.det = det;
    if (!isfinite(det) || fabs(det) < SP_SING_EPS) {
        j.singular = 1;
        j.field = "det";
        if (out) *out = j;
        set_field(ef, "det");
        return SP_E_SINGULAR;
    }

    double J[4];
    jac_of(m, det, J);
    j.j00 = J[0]; j.j01 = J[1]; j.j10 = J[2]; j.j11 = J[3];
    if (!isfinite(J[0]) || !isfinite(J[1]) || !isfinite(J[2]) || !isfinite(J[3])) {
        /* 逆矩阵元素溢出：J 的分量由输入系数除以 det 得到 */
        const char *fld = "det";
        if (!isfinite(J[0]) && !isfinite(m->d)) fld = "d";
        else if (!isfinite(J[1]) && !isfinite(m->c)) fld = "c";
        else if (!isfinite(J[2]) && !isfinite(m->b)) fld = "b";
        else if (!isfinite(J[3]) && !isfinite(m->a)) fld = "a";
        j.singular = 1;
        j.field = fld;
        if (out) *out = j;
        set_field(ef, fld);
        return SP_E_SINGULAR;
    }

    /* G = J^T J 的特征值 = 奇异值的平方：
     *   sigma^2 = (|J|_F^2 ± sqrt(|J|_F^4 - 4*det(J)^2)) / 2 */
    double fro2 = J[0]*J[0] + J[1]*J[1] + J[2]*J[2] + J[3]*J[3];
    j.fro2 = fro2;
    if (!isfinite(fro2) || fro2 <= 0.0) {
        j.singular = 1;
        j.field = "fro2";
        if (out) *out = j;
        set_field(ef, "fro2");
        return SP_E_SINGULAR;
    }
    double detJ = 1.0 / det;
    double disc = fro2 * fro2 - 4.0 * detJ * detJ;
    if (!isfinite(disc)) {
        j.singular = 1;
        j.field = "det";
        if (out) *out = j;
        set_field(ef, "det");
        return SP_E_SINGULAR;
    }
    if (disc < 0.0) disc = 0.0; /* 只吸收舍入；G 半正定，判别式理论上非负 */
    double root = sqrt(disc);
    double s2max = 0.5 * (fro2 + root);
    double s2min = 0.5 * (fro2 - root);
    if (s2min < 0.0) s2min = 0.0;
    j.sigma_max = sqrt(s2max);
    j.sigma_min = sqrt(s2min);
    if (!isfinite(j.sigma_max) || j.sigma_max <= 0.0) {
        j.singular = 1;
        j.field = "sigma_max";
        if (out) *out = j;
        set_field(ef, "sigma_max");
        return SP_E_SINGULAR;
    }
    if (!isfinite(j.sigma_min) || j.sigma_min <= 0.0) {
        j.singular = 1;
        j.field = "sigma_min";
        if (out) *out = j;
        set_field(ef, "sigma_min");
        return SP_E_SINGULAR;
    }
    j.kappa = j.sigma_max / j.sigma_min;
    j.singular = 0;
    j.field = "";
    if (out) *out = j;
    return SP_OK;
}

int sp_unmap(const SpForm *m, SpPoint scr, SpPoint *out, char ef[SP_FIELD_MAX]) {
    if (!m || !out) { set_field(ef, "m"); return SP_E_ARG; }
    if (!finite2(scr.x, scr.y)) { set_field(ef, "scr"); return SP_E_SEG; }
    double det = m->a * m->d - m->b * m->c;
    if (!isfinite(det) || fabs(det) < SP_SING_EPS) {
        set_field(ef, "det");
        return SP_E_SINGULAR;
    }
    double J[4];
    jac_of(m, det, J);
    double px = scr.x - m->e, py = scr.y - m->f;
    out->x = J[0] * px + J[1] * py;
    out->y = J[2] * px + J[3] * py;
    if (!finite2(out->x, out->y)) { set_field(ef, "det"); return SP_E_SINGULAR; }
    return SP_OK;
}

/* 点到段最近点（任意空间）；返回距离，写出参数 u 与最近点。零长段按端点。 */
static double seg_closest(SpPoint p, SpPoint a, SpPoint b, double *u_out, SpPoint *at_out) {
    double sx = b.x - a.x, sy = b.y - a.y;
    double len2 = sx * sx + sy * sy;
    double u = 0.0;
    if (len2 > SP_EPS) {
        u = ((p.x - a.x) * sx + (p.y - a.y) * sy) / len2;
        if (u < 0.0) u = 0.0;
        if (u > 1.0) u = 1.0;
    }
    SpPoint q;
    q.x = a.x + u * sx;
    q.y = a.y + u * sy;
    double dx = p.x - q.x, dy = p.y - q.y;
    if (u_out) *u_out = u;
    if (at_out) *at_out = q;
    return sqrt(dx * dx + dy * dy);
}

int sp_pick_legacy(const SpSeg *segs, int n, const SpForm *m, SpPoint scr,
                   double tol, SpPick *out, char ef[SP_FIELD_MAX]) {
    return sp_pick(segs, n, m, scr, tol, 0u, out, ef);
}

int sp_pick(const SpSeg *segs, int n, const SpForm *m, SpPoint scr,
            double tol, unsigned flags, SpPick *out, char ef[SP_FIELD_MAX]) {
    if (!segs || !m) { set_field(ef, !segs ? "segs" : "m"); return SP_E_ARG; }
    if (n <= 0 || n > SP_MAX_SEGS) { set_field(ef, "n"); return SP_E_ARG; }
    if (!finite2(scr.x, scr.y)) { set_field(ef, "scr"); return SP_E_SEG; }
    if (!isfinite(tol) || tol < 0.0) { set_field(ef, "tol"); return SP_E_TOL; }
    if (flags & ~SP_F_SCREEN_TOL) { set_field(ef, "flags"); return SP_E_FLAGS; }
    for (int i = 0; i < n; ++i) {
        if (!finite2(segs[i].p0.x, segs[i].p0.y) ||
            !finite2(segs[i].p1.x, segs[i].p1.y)) {
            set_field(ef, "segs");
            return SP_E_SEG;
        }
    }

    /* 奇异/退化在任何几何运算之前报出来 */
    SpJac jac;
    int jrc = sp_jacobian(m, &jac, ef);
    if (jrc != SP_OK) return jrc;

    SpPoint back;
    int urc = sp_unmap(m, scr, &back, ef);
    if (urc != SP_OK) return urc;

    int use_screen = (flags & SP_F_SCREEN_TOL) != 0;
    SpPick local;
    memset(&local, 0, sizeof local);
    local.jac = jac;
    local.mode = use_screen ? SP_TOL_MODE_JACOBIAN : SP_TOL_MODE_LEGACY;
    local.nsegs = n;
    local.tol_in = tol;
    local.tol_shape = use_screen ? tol * jac.sigma_max : tol;

    for (int i = 0; i < n; ++i) {
        double u = 0.0;
        SpPoint at;
        double dist = seg_closest(back, segs[i].p0, segs[i].p1, &u, &at);

        /* 屏幕空间：取段的屏幕像上离拾取点最近的点。
         * 仿射下 argmin 可由形状空间参数 u 经 M 直接搬运，两类距离一致。 */
        SpPoint sc = sp_map(m, at);
        SpPoint s0 = sp_map(m, segs[i].p0), s1 = sp_map(m, segs[i].p1);
        double r_px = seg_closest(scr, s0, s1, NULL, NULL);

        SpRow *h = &local.rows[i];
        h->seg = i;
        h->dist = dist;
        h->tol_shape = local.tol_shape;
        h->u = u;
        h->at = at;
        h->scr = sc;
        h->dist_px = hypot(sc.x - scr.x, sc.y - scr.y);
        h->r_px = r_px;
        int inside = (dist <= local.tol_shape);
        h->hit = inside;
        h->reason = inside ? SP_R_INSIDE : SP_R_OUTSIDE;
        if (inside) {
            if (local.count >= SP_MAX_SEGS) {   /* 不可能：count <= n */
                set_field(ef, "out");
                return SP_E_CAP;
            }
            ++local.count;
        }
    }

    /* 与逐像素点击参考可能不一致的段（逐段精确判定，见头文件推导）：
     *   新命中   且 r_px > tol_px - sqrt(2)/2
     *   新未命中 且 r_px <= tol_px + sqrt(2)/2
     * band_n 即逐项不一致数的上界，命中率误差 <= band_n/nsegs。 */
    if (use_screen) {
        /* 不一致上界（见头文件推导）：r ∈ (tol_px - h, tol_px*kappa + h]
         * 内的段才可能新旧判定不同。相似变换 kappa=1，带退化为 tol_px±h。 */
        double lo = tol - SP_HALFDIAG;
        double hi = tol * jac.kappa + SP_HALFDIAG;
        local.band_n = 0;
        for (int i = 0; i < n; ++i) {
            double r = local.rows[i].r_px;
            int may = (r > lo) && (r <= hi);
            if (may && local.band_n < SP_MAX_MISMATCH)
                local.band_seg[local.band_n] = i;
            if (may) ++local.band_n;
        }
    } else {
        /* 旧路径：没有像素离散化，也没有雅可比换算，上界恒为 0 */
        local.band_n = 0;
    }

    if (out) *out = local;
    return SP_OK;
}

int sp_export_rows(const SpPick *p, FILE *fp, int only_hits) {
    if (!p || !fp) return SP_E_ARG;
    if (fprintf(fp,
                "seg,dist,tol_shape,u,x,y,scr_x,scr_y,dist_px,r_px,hit,reason\n") < 0)
        return SP_E_ARG;
    for (int i = 0; i < p->nsegs; ++i) {
        const SpRow *h = &p->rows[i];
        if (only_hits && !h->hit) continue;
        if (fprintf(fp,
                    "%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%d,%s\n",
                    h->seg, h->dist, h->tol_shape, h->u,
                    h->at.x, h->at.y, h->scr.x, h->scr.y,
                    h->dist_px, h->r_px, h->hit, sp_reason_name(h->reason)) < 0)
            return SP_E_ARG;
    }
    return SP_OK;
}
