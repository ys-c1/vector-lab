#include "stroke.h"
#include <stdio.h>

static const char *jname(int j) {
    switch (j) {
        case SJ_MITER: return "miter";
        case SJ_BEVEL: return "bevel";
        case SJ_ROUND: return "round";
        default:       return "?";
    }
}

int main(void) {
    /* 含钝角、直角、锐角三种拐点的折线 */
    SPoint p[5] = {{0,0},{20,0},{30,8},{30,24},{12,24}};
    SOutline out;
    SStrokeOpt opt = { .join = SJ_AUTO, .miter_limit = 4.0 };
    int bad = -1;
    int rc = stroke_outline(p, 5, 3.0, &opt, &out, &bad);
    if (rc != 0) { printf("rejected rc=%d at corner %d\n", rc, bad); return 1; }

    printf("AUTO selection table (miter limit %.1f):\n", opt.miter_limit);
    printf("  corner  angle(deg)  join   miter/width\n");
    for (int i = 0; i < opt.record_count; ++i) {
        SJoinInfo *r = &opt.info[i];
        printf("  %6d  %9.2f  %-6s %10.3f\n",
               r->index, r->angle * 180.0 / 3.14159265358979,
               jname(r->join), r->miter_ratio);
    }
    printf("outline points: %d\n", out.count);
    for (int i = 0; i < out.count; ++i)
        printf("  %.3f %.3f\n", out.pts[i].x, out.pts[i].y);

    /* 固定策略对照 */
    const int modes[3] = { SJ_MITER, SJ_BEVEL, SJ_ROUND };
    for (int m = 0; m < 3; ++m) {
        SStrokeOpt o2 = { .join = modes[m], .miter_limit = 4.0 };
        SOutline tmp;
        if (stroke_outline(p, 5, 3.0, &o2, &tmp, 0) == 0)
            printf("%-6s -> %d points\n", jname(modes[m]), tmp.count);
    }
    return 0;
}
