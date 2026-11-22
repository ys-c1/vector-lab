#include "dash.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    DPoint p[2] = {{0,0},{100,0}};
    DPoly out;
    CK(dash_polyline(p, 2, 10.0, 10.0, &out) == 0);
    CK(out.count > 80);
    CK(out.pts[0].x >= 0.0 && out.pts[0].x < 1.0);
    CK(dash_polyline(p, 2, 0.0, 10.0, &out) == -1);
    DPoint bad[2] = {{0,0},{0,0}};
    CK(dash_polyline(bad, 2, 5.0, 5.0, &out) == -2);

    /* alignment off (phase 0.0) must be byte-identical to dash_polyline */
    DPoint pl[5] = {{0,0},{30,0},{30,20},{60,20},{60,50}};
    DPoly a, b;
    CK(dash_polyline(pl, 5, 7.5, 4.5, &a) == 0);
    CK(dash_polyline_phase(pl, 5, 7.5, 4.5, 0.0, &b) == 0);
    CK(a.count == b.count);
    CK(memcmp(a.pts, b.pts, sizeof(DPoint) * (size_t)a.count) == 0);

    /* phase solver: endpoint lands exactly on solid, phase = period - r */
    DashAlign al;
    DPoint seg2[3] = {{0,0},{50,0},{50,45}};      /* L=95, r=11, P=14 */
    CK(dash_solve_phase(seg2, 3, 8.0, 6.0, &al) == 0);
    CK(al.phase == 3.0 && al.end_offset == 0.0);
    CK(fmod(al.phase, al.period) <= 8.0);                     /* start on solid */
    CK(fmod(al.total_len + al.phase, al.period) <= 8.0);      /* end on solid   */

    /* closed loop: square side 20, L=80, r=10 -> phase 4 */
    DPoint sq[5] = {{0,0},{20,0},{20,20},{0,20},{0,0}};
    CK(dash_solve_phase(sq, 5, 8.0, 6.0, &al) == 0);
    CK(al.phase == 4.0 && al.end_offset == 0.0);
    CK(al.end_offset <= 8.0 / 2.0);

    /* zero period rejected */
    CK(dash_solve_phase(seg2, 3, 0.0, 6.0, &al) == -1);
    CK(dash_solve_phase(seg2, 3, 8.0, 0.0, &al) == -1);
    /* degenerate zero-length segment rejected */
    DPoint deg[3] = {{0,0},{0,0},{5,5}};
    CK(dash_solve_phase(deg, 3, 8.0, 6.0, &al) == -2);
    /* phase out of range: gap too wide, intrusion would exceed on/2 */
    DPoint wide[2] = {{0,0},{5,0}};               /* L=5, on=2, off=10 */
    CK(dash_solve_phase(wide, 2, 2.0, 10.0, &al) == -3);

    /* compromise split: both endpoints intrude equally, within on/2 */
    DPoint cmp[2] = {{0,0},{4,0}};                /* L=4, on=3, off=5 */
    CK(dash_solve_phase(cmp, 2, 3.0, 5.0, &al) == 0);
    CK(al.end_offset == 0.5 && al.end_offset <= 3.0 / 2.0);
    CK(al.phase >= 0.0 && al.phase < al.period);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
