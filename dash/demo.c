#define _POSIX_C_SOURCE 199309L
#include "dash.h"
#include <stdio.h>
#include <time.h>

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void report(const char *name, const DPoint *p, int n,
                   double on, double off) {
    DashAlign al;
    DPoly out;
    double t0 = now_us();
    int rc = dash_solve_phase(p, n, on, off, &al);
    int rc2 = (rc == 0) ? dash_polyline_phase(p, n, on, off, al.phase, &out) : -9;
    double us = now_us() - t0;
    if (rc != 0) {
        const char *why = rc == -1 ? "zero period / bad args"
                        : rc == -2 ? "degenerate zero-length segment"
                        :            "phase out of range";
        printf("%-14s REJECTED (%s)  %.1f us\n", name, why, us);
        return;
    }
    printf("%-14s phase=%.3f  end_offset=%.3f  len=%.1f  period=%.1f"
           "  dash_pts=%d  %.1f us\n",
           name, al.phase, al.end_offset, al.total_len, al.period,
           rc2 == 0 ? out.count : -1, us);
}

int main(void) {
    DPoint seg2[3] = {{0,0},{50,0},{50,45}};                       /* L=95  */
    DPoint seg4[5] = {{0,0},{30,0},{30,20},{60,20},{60,47}};       /* L=107 */
    DPoint loop[5] = {{0,0},{20,0},{20,20},{0,20},{0,0}};          /* L=80  */
    DPoint degen[3] = {{0,0},{0,0},{5,5}};
    report("2-segment",  seg2, 3, 8.0, 6.0);
    report("4-segment",  seg4, 5, 8.0, 6.0);
    report("closed-loop", loop, 5, 8.0, 6.0);
    report("degenerate", degen, 3, 8.0, 6.0);
    return 0;
}
