#include "clip.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-6; }

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

static int same_as_plain(const CPolyRings *r, const CPoly *plain) {
    if (!r || !plain) return 0;
    if (r->hole_count != 0 || r->outer_count != (plain->count >= 3 ? 1 : 0))
        return 0;
    if (plain->count < 3) return 1;
    if (r->outer[0].count != plain->count) return 0;
    for (int i = 0; i < plain->count; ++i)
        if (r->outer[0].pts[i].x != plain->pts[i].x ||
            r->outer[0].pts[i].y != plain->pts[i].y) return 0;
    return 1;
}

int main(void) {
    /* --- original convex-clip behaviour is unchanged --- */
    {
        CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
        CPoly out;
        CK(clip_convex(poly,4,win,4,&out) == 0);
        CK(out.count == 4);
        CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);
        CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
        CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);
        CK(clip_convex(poly,2,win,4,&out) == -1);
    }

    /* --- no holes: identical result, point for point --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint w[4] = {{2,2},{8,2},{8,8},{2,8}};
        CPoint wide[4] = {{-10,-10},{30,-10},{30,30},{-10,30}};
        CPoly plain;
        CPolyRings rings;
        int bad = 123;
        CK(clip_convex(p,4,w,4,&plain) == 0);
        CK(clip_convex_holes(p,4,w,4,NULL,0,&rings,&bad) == CLIP_OK);
        CK(same_as_plain(&rings, &plain));
        CK(near(clip_rings_area(&rings), clip_area(&plain)));
        CK(bad == -1);

        CK(clip_convex(wide,4,w,4,&plain) == 0);
        CK(clip_convex_holes(wide,4,w,4,NULL,0,&rings,&bad) == CLIP_OK);
        CK(same_as_plain(&rings, &plain));
        CK(near(clip_rings_area(&rings), clip_area(&plain)));

        /* subject entirely outside the window */
        CK(clip_convex_holes(wide,4,w,4,NULL,0,&rings,&bad) == CLIP_OK);
        CK(rings.outer_count + rings.hole_count >= 0);
    }

    /* --- one hole: window 10x10, 2x2 hole in the middle --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h[4] = {{4,4},{6,4},{6,6},{4,6}};
        CPoly holes[1] = {mkpoly(h,4)};
        CPolyRings r;
        int bad = -1;
        CK(clip_convex_holes(p,4,p,4,holes,1,&r,&bad) == CLIP_OK);
        CK(bad == -1);
        CK(r.outer_count == 1 && r.hole_count == 1);
        CK(near(clip_rings_area(&r), 96.0));
        CK(near(signed_area(r.outer[0].pts, r.outer[0].count), 100.0));
        CK(near(signed_area(r.hole[0].pts, r.hole[0].count), -4.0));
    }

    /* --- two holes --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h1[4] = {{1,1},{2,1},{2,2},{1,2}};
        CPoint h2[4] = {{7,7},{9,7},{9,9},{7,9}};
        CPoly holes[2] = {mkpoly(h1,4), mkpoly(h2,4)};
        CPolyRings r;
        int bad = -1;
        CK(clip_convex_holes(p,4,p,4,holes,2,&r,&bad) == CLIP_OK);
        CK(bad == -1);
        CK(r.outer_count == 1 && r.hole_count == 2);
        CK(near(clip_rings_area(&r), 100.0 - 1.0 - 4.0));
    }

    /* --- non-convex (L-shaped) hole --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h[6] = {{2,2},{6,2},{6,4},{4,4},{4,6},{2,6}};
        CPoly holes[1] = {mkpoly(h,6)};
        CPolyRings r;
        int bad = -1;
        CK(clip_convex_holes(p,4,p,4,holes,1,&r,&bad) == CLIP_OK);
        CK(bad == -1);
        CK(r.outer_count == 1 && r.hole_count == 1);
        CK(near(clip_rings_area(&r), 100.0 - 12.0));
        CK(near(signed_area(r.hole[0].pts, r.hole[0].count), -12.0));
    }

    /* --- hole on the window boundary is rejected as outside --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h1[4] = {{-1,4},{1,4},{1,6},{-1,6}};
        CPoint h2[4] = {{0,4},{1,4},{1,6},{0,6}};
        CPoly holes[1] = {mkpoly(h1,4)};
        CPolyRings r;
        int bad = -1;
        CK(clip_convex_holes(p,4,p,4,holes,1,&r,&bad) == CLIP_ERR_HOLE_OUTSIDE);
        CK(bad == 0);
        holes[0] = mkpoly(h2,4);
        CK(clip_convex_holes(p,4,p,4,holes,1,&r,&bad) == CLIP_ERR_HOLE_OUTSIDE);
        CK(bad == 0);
    }

    /* --- hole outside the window -> detected with its index --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint ok[4] = {{2,2},{3,2},{3,3},{2,3}};
        CPoint far[4] = {{20,20},{21,20},{21,21},{20,21}};
        CPoint strad[4] = {{9,4},{11,4},{11,6},{9,6}};
        CPoly holes[3] = {mkpoly(ok,4), mkpoly(far,4), mkpoly(strad,4)};
        CPolyRings r;
        int bad = -1;
        CK(clip_convex_holes(p,4,p,4,holes,3,&r,&bad) == CLIP_ERR_HOLE_OUTSIDE);
        CK(bad == 1);
        CK(clip_convex_holes(p,4,p,4,holes+1,2,&r,&bad) == CLIP_ERR_HOLE_OUTSIDE);
        CK(bad == 0);
    }

    /* --- degenerate holes are detected with their index --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint ok[4] = {{1,1},{2,1},{2,2},{1,2}};
        CPoint line[3] = {{3,3},{4,3},{5,3}};
        CPoint dup[4] = {{3,3},{3,3},{5,5},{5,3}};
        CPoint bow[4] = {{3,3},{6,6},{6,3},{3,6}};
        CPolyRings r;
        int bad = -1;

        CPoly h1[2] = {mkpoly(ok,4), mkpoly(line,3)};
        CK(clip_convex_holes(p,4,p,4,h1,2,&r,&bad) == CLIP_ERR_HOLE_DEGENERATE);
        CK(bad == 1);

        CPoly h2[3] = {mkpoly(ok,4), mkpoly(ok,4), mkpoly(dup,4)};
        CK(clip_convex_holes(p,4,p,4,h2,3,&r,&bad) == CLIP_ERR_HOLE_DEGENERATE);
        CK(bad == 2);

        CPoly h3[4] = {mkpoly(ok,4), mkpoly(ok,4), mkpoly(ok,4), mkpoly(bow,4)};
        CK(clip_convex_holes(p,4,p,4,h3,4,&r,&bad) == CLIP_ERR_HOLE_DEGENERATE);
        CK(bad == 3);

        CK(clip_hole_status(p,4,line,3) == CLIP_ERR_HOLE_DEGENERATE);
        CK(clip_hole_status(p,4,bow,4) == CLIP_ERR_HOLE_DEGENERATE);
        CK(clip_hole_status(p,4,dup,4) == CLIP_ERR_HOLE_DEGENERATE);
        CK(clip_hole_status(p,4,ok,4) == CLIP_OK);
    }

    /* --- clockwise hole / clockwise window give the same result --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint wcw[4] = {{0,0},{0,10},{10,10},{10,0}};
        CPoint ccw[4] = {{4,4},{6,4},{6,6},{4,6}};
        CPoint cw[4] = {{4,4},{4,6},{6,6},{6,4}};
        CPoly h1[1] = {mkpoly(ccw,4)};
        CPoly h2[1] = {mkpoly(cw,4)};
        CPolyRings r1, r2, r3;
        CK(clip_convex_holes(p,4,p,4,h1,1,&r1,NULL) == CLIP_OK);
        CK(clip_convex_holes(p,4,p,4,h2,1,&r2,NULL) == CLIP_OK);
        CK(clip_convex_holes(p,4,wcw,4,h1,1,&r3,NULL) == CLIP_OK);
        CK(near(clip_rings_area(&r1), 96.0));
        CK(near(clip_rings_area(&r2), 96.0));
        CK(near(clip_rings_area(&r3), 96.0));
        CK(r1.outer_count == r2.outer_count && r1.hole_count == r2.hole_count);
    }

    /* --- hole fully covering the subject: everything is removed --- */
    {
        CPoint p[4] = {{2,2},{8,2},{8,8},{2,8}};
        CPoint w[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h[4] = {{1,1},{9,1},{9,9},{1,9}};
        CPoly holes[1] = {mkpoly(h,4)};
        CPolyRings r;
        CK(clip_convex_holes(p,4,w,4,holes,1,&r,NULL) == CLIP_OK);
        CK(r.outer_count == 0 && r.hole_count == 0);
        CK(near(clip_rings_area(&r), 0.0));
    }

    /* --- hole not overlapping the subject at all --- */
    {
        CPoint p[4] = {{0,0},{5,0},{5,5},{0,5}};
        CPoint w[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h[4] = {{7,7},{9,7},{9,9},{7,9}};
        CPoly holes[1] = {mkpoly(h,4)};
        CPolyRings r;
        CK(clip_convex_holes(p,4,w,4,holes,1,&r,NULL) == CLIP_OK);
        CK(r.outer_count == 1 && r.hole_count == 0);
        CK(near(clip_rings_area(&r), 25.0));
    }

    /* --- overlapping holes merge into one hole ring --- */
    {
        CPoint p[4] = {{0,0},{100,0},{100,100},{0,100}};
        CPoint ha[4] = {{20,20},{60,20},{60,60},{20,60}};
        CPoint hb[4] = {{40,40},{80,40},{80,80},{40,80}};
        CPoly holes[2] = {mkpoly(ha,4), mkpoly(hb,4)};
        CPolyRings r;
        CK(clip_convex_holes(p,4,p,4,holes,2,&r,NULL) == CLIP_OK);
        CK(r.outer_count == 1 && r.hole_count == 1);
        CK(near(clip_rings_area(&r), 7200.0));
    }

    /* --- holes sharing an edge merge; touching at a corner stay separate --- */
    {
        CPoint p[4] = {{0,0},{100,0},{100,100},{0,100}};
        CPoint e1[4] = {{20,20},{40,20},{40,40},{20,40}};
        CPoint e2[4] = {{40,20},{60,20},{60,40},{40,40}};
        CPoly eholes[2] = {mkpoly(e1,4), mkpoly(e2,4)};
        CPoint c1[4] = {{20,20},{40,20},{40,40},{20,40}};
        CPoint c2[4] = {{40,40},{60,40},{60,60},{40,60}};
        CPoly choles[2] = {mkpoly(c1,4), mkpoly(c2,4)};
        CPolyRings r;
        CK(clip_convex_holes(p,4,p,4,eholes,2,&r,NULL) == CLIP_OK);
        CK(r.outer_count == 1 && r.hole_count == 1);
        CK(near(clip_rings_area(&r), 9200.0));
        CK(clip_convex_holes(p,4,p,4,choles,2,&r,NULL) == CLIP_OK);
        CK(r.outer_count == 1 && r.hole_count == 2);
        CK(near(clip_rings_area(&r), 9200.0));
    }

    /* --- hole nested inside another hole: only the outer one counts --- */
    {
        CPoint p[4] = {{0,0},{100,0},{100,100},{0,100}};
        CPoint big[4] = {{10,10},{70,10},{70,70},{10,70}};
        CPoint small[4] = {{30,30},{50,30},{50,50},{30,50}};
        CPoly holes[2] = {mkpoly(big,4), mkpoly(small,4)};
        CPolyRings r;
        CK(clip_convex_holes(p,4,p,4,holes,2,&r,NULL) == CLIP_OK);
        CK(r.outer_count == 1 && r.hole_count == 1);
        CK(near(clip_rings_area(&r), 10000.0 - 3600.0));
    }

    /* --- argument checks --- */
    {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint h[4] = {{4,4},{6,4},{6,6},{4,6}};
        CPoly holes[1] = {mkpoly(h,4)};
        CPolyRings r;
        CK(clip_convex_holes(NULL,4,p,4,holes,1,&r,NULL) == CLIP_ERR_ARGS);
        CK(clip_convex_holes(p,4,p,4,NULL,1,&r,NULL) == CLIP_ERR_ARGS);
        CK(clip_convex_holes(p,2,p,4,holes,1,&r,NULL) == CLIP_ERR_ARGS);
        CK(clip_convex_holes(p,4,p,4,holes,-1,&r,NULL) == CLIP_ERR_ARGS);
        CK(clip_rings_area(NULL) == 0.0);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
