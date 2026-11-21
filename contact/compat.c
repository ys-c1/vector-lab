/* 交叉验证：关闭分类时，contact 的输出必须与仓库现状管线
 * （bezier_flatten 固定 8 段 + isect_seg 逐弦求交）逐位一致。
 * 直接调用相邻模块的真实源码，而不是复刻一份公式。 */
#include "contact.h"
#include "bezier.h"
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* BPoly 与 IPoly 的结构布局相同（BPoint/IPoint 都是两个 double），
 * 但 -Wstrict-aliasing 不允许直接指针转换，这里逐点拷贝。 */
static void to_ipoly(const BPoly *bp, IPoint *ip) {
    for (int i = 0; i < bp->count; ++i) {
        ip[i].x = bp->pts[i].x;
        ip[i].y = bp->pts[i].y;
    }
}

static int run_case(const char *name, const BPoint bc[4], IPoint q0, IPoint q1) {
    BPoly fb;
    if (bezier_flatten(bc, 0.25, &fb) != 0) { printf("flatten failed\n"); return 1; }

    CtPoint c[4];
    for (int i = 0; i < 4; ++i) { c[i].x = bc[i].x; c[i].y = bc[i].y; }

    char ef[CT_FIELD_MAX];
    CtChordHit got[64];
    int ng = ct_legacy_hits(c, (CtPoint){q0.x, q0.y}, (CtPoint){q1.x, q1.y},
                            8, got, 64, ef);
    CK(ng >= 0);

    IPoint fp[65];
    to_ipoly(&fb, fp);
    int k = 0, bad = 0;
    for (int i = 0; i + 1 < fb.count; ++i) {
        IHit h;
        if (!isect_seg(fp[i], fp[i + 1], q0, q1, &h))
            continue;
        /* 现状管的线参数与 contact 的旧路径必须逐位相同 */
        if (k >= ng || got[k].chord != i || got[k].t != h.t || got[k].u != h.u ||
            got[k].at.x != h.at.x || got[k].at.y != h.at.y)
            bad = 1;
        ++k;
    }
    printf("%-24s bezier+isect=%d contact_legacy=%d %s\n",
           name, k, ng, (bad || k != ng) ? "MISMATCH" : "bitwise identical");
    return bad || k != ng;
}

int main(void) {
    BPoint curve[4] = {{0,0},{0,3},{3,1},{3,0}};
    double ts = (5.0 - sqrt(7.0)) / 6.0;
    double ys = 9*ts - 15*ts*ts + 6*ts*ts*ts;
    int bad = 0;

    bad |= run_case("crossing y=1", curve, (IPoint){-1,1}, (IPoint){3,1});
    bad |= run_case("exact tangent", curve, (IPoint){-1,ys}, (IPoint){3,ys});
    bad |= run_case("diagonal", curve, (IPoint){-1,-0.5}, (IPoint){3,2.5});
    bad |= run_case("vertical x=1", curve, (IPoint){1,-1}, (IPoint){1,2});
    bad |= run_case("no contact", curve, (IPoint){-1,5}, (IPoint){3,5});

    /* ct_contact 关闭分类的整表输出同样逐位一致 */
    CtScan s; char ef[CT_FIELD_MAX];
    CtPoint c4[4] = {{0,0},{0,3},{3,1},{3,0}};
    CK(ct_contact(c4, (CtPoint){-1,1}, (CtPoint){3,1}, 64, 0, 0.0, &s, ef) == CT_OK);
    BPoly fb;
    bezier_flatten(curve, 0.25, &fb);
    IPoint fp[65];
    to_ipoly(&fb, fp);
    int k = 0;
    for (int i = 0; i + 1 < fb.count; ++i) {
        IHit h;
        if (!isect_seg(fp[i], fp[i + 1], (IPoint){-1,1}, (IPoint){3,1}, &h))
            continue;
        double tg = ((double)i + h.t) / 8.0;
        CK(k < s.count && s.hits[k].t == tg && s.hits[k].u == h.u);
        ++k;
    }
    CK(k == s.count);

    printf("%d checks, %d failures\n", checks, fails);
    return (bad || fails) ? 1 : 0;
}
