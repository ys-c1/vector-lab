#include "isect.h"
#include <stdio.h>
int main(void) {
    IPoint a[3] = {{0,0},{10,0},{10,10}};
    IPoint b[2] = {{0,5},{12,5}};
    int hits = 0;
    if (isect_polyline(a,3,b,2,&hits) != 0) return 1;
    printf("hits %d\n", hits);

    /* cubic bezier vs polyline: coarse subdivide + local refine */
    IPoint c[4] = {{0,0},{10,30},{20,-30},{30,0}};
    IPoint line[2] = {{-5,-2},{35,-2}};
    IHit out[8];
    ICurveOpts opt = {32, 1e-9, 1, 0};
    int n = isect_curve_polyline(c, line, 2, &opt, out, 8, 0);
    if (n < 0) {
        printf("rejected: bad field '%s'\n", isect_err_field(n));
        return 1;
    }
    printf("curve hits %d\n", n);
    for (int i = 0; i < n; ++i)
        printf("  t %.9f u %.6f at %.6f %.6f\n", out[i].t, out[i].u, out[i].at.x, out[i].at.y);
    return 0;
}
