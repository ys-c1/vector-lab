#include "stroke.h"
#include <stdio.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static SPoint pts[STROKE_MAX_IN + 1];

static int make_path(int n_corner) {
    int n = n_corner + 2;
    pts[0].x = 0.0; pts[0].y = -20.0;
    for (int i = 0; i <= n_corner; ++i) {
        double ang = -M_PI * 0.5 + (double)i / n_corner * M_PI * 1.5;
        double r = (i % 2) ? 18.0 : 30.0;
        pts[i + 1].x = r * cos(ang);
        pts[i + 1].y = r * sin(ang);
    }
    return n;
}

int main(void) {
    const int modes[4] = { SJ_MITER, SJ_BEVEL, SJ_ROUND, SJ_AUTO };
    const char *names[4] = { "miter", "bevel", "round", "auto" };
    int groups[2] = { 8, 64 };
    for (int g = 0; g < 2; ++g) {
        int n = make_path(groups[g]);
        for (int m = 0; m < 4; ++m) {
            SStrokeOpt opt = { .join = modes[m], .miter_limit = 4.0 };
            SOutline out;
            int bad = -1;
            int rc = stroke_outline(pts, n, 2.0, &opt, &out, &bad);
            if (rc != 0) {
                printf("MODE %d_%s REJECTED rc=%d corner=%d\n",
                       groups[g], names[m], rc, bad);
                continue;
            }
            printf("MODE %d_%s\n", groups[g], names[m]);
            for (int i = 0; i < out.count; ++i)
                printf("%.12f %.12f\n", out.pts[i].x, out.pts[i].y);
        }
    }
    return 0;
}
