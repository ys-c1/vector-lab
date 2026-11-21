#include "clip.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#define BENCH_CALLS 2000

static double signed_area(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
    return s * 0.5;
}

static CPoly mkpoly(const CPoint *p, int n) {
    CPoly r;
    r.count = n;
    for (int i = 0; i < n; ++i) r.pts[i] = p[i];
    return r;
}

/* The subject covers the whole window here, so the hand value is simply
 * window area minus the area of every hole, each measured by its own ring. */
static void print_ring_table(const CPolyRings *r, double *outer_sum, double *hole_sum) {
    *outer_sum = 0.0;
    *hole_sum = 0.0;
    printf("  outer rings: %d\n", r->outer_count);
    for (int i = 0; i < r->outer_count; ++i) {
        double a = signed_area(r->outer[i].pts, r->outer[i].count);
        *outer_sum += a;
        printf("    O%-2d  %3d pts  area %+11.2f\n", i, r->outer[i].count, a);
    }
    printf("  hole rings: %d\n", r->hole_count);
    for (int i = 0; i < r->hole_count; ++i) {
        double a = signed_area(r->hole[i].pts, r->hole[i].count);
        *hole_sum += a;
        printf("    H%-2d  %3d pts  area %+11.2f\n", i, r->hole[i].count, a);
    }
    printf("  ring sums: outer %+.2f, holes %+.2f, net %+.2f\n",
           *outer_sum, *hole_sum, *outer_sum + *hole_sum);
}

static void run_case(const char *name, const CPoint *win, int wn,
                     const CPoly *holes, int nhole) {
    double win_area = fabs(signed_area(win, wn));
    double hole_area = 0.0;
    for (int i = 0; i < nhole; ++i)
        hole_area += fabs(signed_area(holes[i].pts, holes[i].count));
    double hand = win_area - hole_area;

    /* The subject is the window itself: everything is clipped away outside. */
    CPolyRings r;
    int bad = -1;
    if (clip_convex_holes(win, wn, win, wn, holes, nhole, &r, &bad) != CLIP_OK) {
        printf("%s: clip failed (bad hole %d)\n", name, bad);
        return;
    }

    double outer_sum, hole_sum;
    printf("%s\n", name);
    print_ring_table(&r, &outer_sum, &hole_sum);

    double got = clip_rings_area(&r);
    double hand_from_rings = outer_sum + hole_sum;
    double err = fabs(got - hand);
    double rel = hand != 0.0 ? err / hand * 100.0 : 0.0;
    printf("  window %.2f, holes %.2f -> hand area %.2f\n",
           win_area, hole_area, hand);
    printf("  clip area %.2f, ring-table area %.2f\n", got, hand_from_rings);
    printf("  area error %.6f (%.6f%%)\n", err, rel);

    clock_t t0 = clock();
    int fails = 0;
    for (int i = 0; i < BENCH_CALLS; ++i) {
        CPolyRings rr;
        if (clip_convex_holes(win, wn, win, wn, holes, nhole, &rr, NULL) != CLIP_OK)
            ++fails;
    }
    double us = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / BENCH_CALLS;
    printf("  rings: %d outer, %d hole; time %.2f us/call (%d calls)\n\n",
           r.outer_count, r.hole_count, us, BENCH_CALLS);
}

static int check_no_holes(const CPoint *win, int wn) {
    CPoly plain;
    CPolyRings r;
    if (clip_convex(win, wn, win, wn, &plain) != 0) return 0;
    if (clip_convex_holes(win, wn, win, wn, NULL, 0, &r, NULL) != CLIP_OK) return 0;
    if (r.outer_count != 1 || r.hole_count != 0) return 0;
    if (r.outer[0].count != plain.count) return 0;
    for (int i = 0; i < plain.count; ++i)
        if (r.outer[0].pts[i].x != plain.pts[i].x ||
            r.outer[0].pts[i].y != plain.pts[i].y) return 0;
    return 1;
}

int main(void) {
    CPoint win[4] = {{0,0},{200,0},{200,200},{0,200}};

    printf("zero-hole: %s\n\n",
           check_no_holes(win, 4) ? "point-for-point identical to clip_convex"
                                  : "MISMATCH");

    CPoint h2[2][8];
    h2[0][0] = (CPoint){30,30};  h2[0][1] = (CPoint){60,30};
    h2[0][2] = (CPoint){60,60};  h2[0][3] = (CPoint){30,60};
    h2[1][0] = (CPoint){120,40}; h2[1][1] = (CPoint){160,40};
    h2[1][2] = (CPoint){160,55}; h2[1][3] = (CPoint){135,55};
    h2[1][4] = (CPoint){135,90}; h2[1][5] = (CPoint){120,90};
    CPoly holes2[2] = {mkpoly(h2[0], 4), mkpoly(h2[1], 6)};

    CPoly holes16[16];
    int k = 0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double x = 15 + i * 45.0, y = 15 + j * 45.0;
            CPoint h[6] = {{x,y},{x+20,y},{x+20,y+10},
                           {x+10,y+10},{x+10,y+20},{x,y+20}};
            holes16[k++] = mkpoly(h, 6);
        }
    }

    run_case("case: 2 holes", win, 4, holes2, 2);
    run_case("case: 16 holes", win, 4, holes16, 16);

    /* --- rejected holes carry their index --- */
    {
        CPoint good[4] = {{20,20},{30,20},{30,30},{20,30}};
        CPoint line[3] = {{50,50},{60,50},{70,50}};
        CPoint far[4] = {{220,20},{230,20},{230,30},{220,30}};
        CPoly bad_holes[3] = {mkpoly(good,4), mkpoly(line,3), mkpoly(far,4)};
        CPolyRings r;
        int bad = -1;
        int rc = clip_convex_holes(win,4,win,4,bad_holes,3,&r,&bad);
        printf("reject demo: rc=%d (degenerate=%d outside=%d) bad hole index %d\n\n",
               rc, CLIP_ERR_HOLE_DEGENERATE, CLIP_ERR_HOLE_OUTSIDE, bad);
        CPoly only_far[1] = {mkpoly(far,4)};
        rc = clip_convex_holes(win,4,win,4,only_far,1,&r,&bad);
        printf("reject demo: single outside hole -> rc=%d bad hole index %d\n\n",
               rc, bad);
    }

    /* side-by-side summary */
    CPolyRings a, b;
    clip_convex_holes(win, 4, win, 4, holes2, 2, &a, NULL);
    clip_convex_holes(win, 4, win, 4, holes16, 16, &b, NULL);
    double hand2 = 200.0 * 200.0, hand16 = 200.0 * 200.0;
    for (int i = 0; i < 2; ++i) hand2 -= fabs(signed_area(holes2[i].pts, holes2[i].count));
    for (int i = 0; i < 16; ++i) hand16 -= fabs(signed_area(holes16[i].pts, holes16[i].count));
    clock_t t0 = clock();
    for (int i = 0; i < BENCH_CALLS; ++i) {
        CPolyRings t;
        clip_convex_holes(win, 4, win, 4, holes2, 2, &t, NULL);
    }
    double us2 = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / BENCH_CALLS;
    t0 = clock();
    for (int i = 0; i < BENCH_CALLS; ++i) {
        CPolyRings t;
        clip_convex_holes(win, 4, win, 4, holes16, 16, &t, NULL);
    }
    double us16 = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / BENCH_CALLS;

    printf("compare\n");
    printf("  %-6s %-7s %-12s %-12s %-10s %-10s\n",
           "holes", "rings", "clip area", "hand area", "error", "time");
    printf("  %-6d %d+%d%-4s %-12.2f %-12.2f %-10.6f %.1f us\n",
           2, a.outer_count, a.hole_count, "",
           clip_rings_area(&a), hand2, fabs(clip_rings_area(&a) - hand2), us2);
    printf("  %-6d %d+%d%-4s %-12.2f %-12.2f %-10.6f %.1f us\n",
           16, b.outer_count, b.hole_count, "",
           clip_rings_area(&b), hand16, fabs(clip_rings_area(&b) - hand16), us16);
    return 0;
}
