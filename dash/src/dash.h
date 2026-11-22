#ifndef DASH_H
#define DASH_H
typedef struct { double x, y; } DPoint;
typedef struct { int count; DPoint pts[128]; } DPoly;

/* Result of inverse-solving the dash phase so both polyline endpoints
 * land on solid (on) ink. */
typedef struct {
    double total_len;   /* total arc length of the polyline            */
    double period;      /* on + off                                    */
    double phase;       /* solved phase offset, in [0, period)         */
    double end_offset;  /* max intrusion of either endpoint into a gap;
                           0 when both endpoints sit exactly on solid  */
} DashAlign;

/* Original sampler, phase 0, output unchanged. */
int dash_polyline(const DPoint *pts, int n, double on, double off, DPoly *out);

/* Solve the phase offset from the endpoint-on-solid condition.
 * Returns 0 on success, -1 bad args / zero period, -2 degenerate
 * zero-length segment, -3 phase out of range (no shift keeps both
 * endpoints within half a dash segment of solid ink). */
int dash_solve_phase(const DPoint *pts, int n, double on, double off,
                     DashAlign *al);

/* Sampler with an explicit phase offset added to the arc-length
 * parameter. phase == 0.0 is byte-identical to dash_polyline. */
int dash_polyline_phase(const DPoint *pts, int n, double on, double off,
                        double phase, DPoly *out);
#endif
