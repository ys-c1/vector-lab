#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int close2(double a, double b) { return fabs(a - b) < 1e-12; }

int main(void) {
    IPoint a0 = {0,0}, a1 = {10,0}, b0 = {5,-5}, b1 = {5,5};
    IHit h;
    CK(isect_seg(a0,a1,b0,b1,&h) == 1);
    CK(h.at.x > 4.9 && h.at.x < 5.1);
    IPoint c0 = {0,1}, c1 = {10,1};
    CK(isect_seg(a0,a1,c0,c1,&h) == 0);
    IPoint pa[2] = {{0,0},{10,0}}, pb[2] = {{5,-5},{5,5}};
    int hits = 0;
    CK(isect_polyline(pa,2,pb,2,&hits) == 0 && hits == 1);
    IPoint bow[5] = {{0,0},{10,10},{10,0},{0,10},{0,0}};
    int ia = -1, ib = -1;
    CK(isect_self(bow,5,&ia,&ib) == 1);
    CK(isect_polyline(pa,1,pb,2,&hits) == -1);

    /* ---- hand-computed intersection table ----
     * A = (0,0)-(2,0)-(4,0); B = (0.5,-1)-(1.5,1)-(2.5,-1)-(3.5,1)
     * B crosses y=0 at x=1,2,3 (u=0.5 on B segments 0,1,2).
     * x=2 is vertex A[1], reported twice: (ia=0,t=1) and (ia=1,t=0).
     * raw:  (0,0)->(1,0) t=.5 | (0,1)->(2,0) t=1 | (1,1)->(2,0) t=0 | (1,2)->(3,0) t=.5
     * dedup: (1,0), (2,0), (3,0) sorted by x. */
    IPoint A[3] = {{0,0},{2,0},{4,0}};
    IPoint B[4] = {{0.5,-1},{1.5,1},{2.5,-1},{3.5,1}};
    IXHit buf[16];
    int n = 0;
    char err[256];
    err[0] = 0;

    /* raw mode: must match the brute-force enumeration item by item */
    CK(isect_polyline_list(A,3,B,4,buf,16,&n,0,err,sizeof err) == ISECT_OK);
    CK(n == 4);
    {
        int k = 0, ok = 1;
        for (int i = 0; i + 1 < 3 && ok; ++i)
            for (int j = 0; j + 1 < 4 && ok; ++j) {
                IHit e;
                if (!isect_seg(A[i],A[i+1],B[j],B[j+1],&e)) continue;
                if (k >= n || buf[k].ia != i || buf[k].ib != j ||
                    !close2(buf[k].t, e.t) || !close2(buf[k].u, e.u) ||
                    !close2(buf[k].at.x, e.at.x) || !close2(buf[k].at.y, e.at.y))
                    ok = 0;
                ++k;
            }
        CK(ok && k == n);
    }
    CK(isect_polyline(A,3,B,4,&hits) == 0 && hits == n);
    /* hand table, raw entries */
    CK(buf[0].ia == 0 && buf[0].ib == 0 && close2(buf[0].t,0.5) && close2(buf[0].at.x,1) && close2(buf[0].at.y,0));
    CK(buf[1].ia == 0 && buf[1].ib == 1 && close2(buf[1].t,1.0) && close2(buf[1].at.x,2));
    CK(buf[2].ia == 1 && buf[2].ib == 1 && close2(buf[2].t,0.0) && close2(buf[2].at.x,2));
    CK(buf[3].ia == 1 && buf[3].ib == 2 && close2(buf[3].t,0.5) && close2(buf[3].at.x,3));

    /* dedup+sort: vertex duplicate at (2,0) removed, sorted by x */
    CK(isect_polyline_list(A,3,B,4,buf,16,&n,1,err,sizeof err) == ISECT_OK);
    CK(n == 3);
    CK(close2(buf[0].at.x,1) && close2(buf[1].at.x,2) && close2(buf[2].at.x,3));
    CK(buf[0].at.x <= buf[1].at.x && buf[1].at.x <= buf[2].at.x);

    /* ---- error reporting with position ---- */
    CK(isect_polyline_list(A,1,B,4,buf,16,&n,0,err,sizeof err) == ISECT_ERR_ARG);
    CK(strstr(err, "na=1") != NULL);
    CK(isect_polyline_list(NULL,3,B,4,buf,16,&n,0,err,sizeof err) == ISECT_ERR_ARG);
    CK(strstr(err, "arg a is NULL") != NULL);
    CK(isect_polyline_list(A,3,B,4,buf,0,&n,0,err,sizeof err) == ISECT_ERR_ARG);
    CK(strstr(err, "cap=0") != NULL);
    CK(isect_polyline_list(A,3,B,4,buf,2,&n,0,err,sizeof err) == ISECT_ERR_CAP);
    CK(strstr(err, "ia=") != NULL && strstr(err, "ib=") != NULL);
    {
        IPoint far[2] = {{100,100},{200,100}};
        CK(isect_polyline_list(A,3,far,2,buf,16,&n,0,err,sizeof err) == ISECT_EMPTY);
        CK(n == 0 && strstr(err, "empty intersection list") != NULL);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
