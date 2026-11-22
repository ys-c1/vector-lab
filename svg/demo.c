#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Demo: the same two-cubic lens rendered at 32x32 and 256x256.
 *
 * For each size the demo parses the cubic path, fills it with svg_fill(),
 * writes a PGM, and compares the result against a hand-written polyline
 * reference (the same curve sampled at a fixed 1/64 step).  One table row per
 * size reports the control-point count, the segment numbers of the C commands,
 * the number of flattened segments, and the parse / fill time.
 *
 * Report: demo.txt.  Images: demo_32.pgm, demo_256.pgm.
 */

typedef struct {
    const char *name;
    int ctrl_nums;   /* coordinate numbers given as control points: 2 per C */
    int ctrl_pts;    /* distinct control points + endpoint per C group (3) */
    int seg_first;   /* segment number of the first C */
    int seg_last;    /* segment number of the last C */
    int segs;        /* SV_LINE segments the cubics flattened into */
    double parse_ms;
    double fill_ms;
} Row;

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/*
 * Reference polyline: both cubics sampled at 1/64 steps in 32x32 user units.
 * Each cubic contributes 64 chords; the shared seam point is written once.
 */
static const double REF[128][2] = {
    {4.000000, 16.000000}, {4.017395, 15.446289}, {4.068848, 14.910156}, {4.153259, 14.391602},
    {4.269531, 13.890625}, {4.416565, 13.407227}, {4.593262, 12.941406}, {4.798523, 12.493164},
    {5.031250, 12.062500}, {5.290344, 11.649414}, {5.574707, 11.253906}, {5.883240, 10.875977},
    {6.214844, 10.515625}, {6.568420, 10.172852}, {6.942871, 9.847656}, {7.337097, 9.540039},
    {7.750000, 9.250000}, {8.180481, 8.977539}, {8.627441, 8.722656}, {9.089783, 8.485352},
    {9.566406, 8.265625}, {10.056213, 8.063477}, {10.558105, 7.878906}, {11.070984, 7.711914},
    {11.593750, 7.562500}, {12.125305, 7.430664}, {12.664551, 7.316406}, {13.210388, 7.219727},
    {13.761719, 7.140625}, {14.317444, 7.079102}, {14.876465, 7.035156}, {15.437683, 7.008789},
    {16.000000, 7.000000}, {16.562317, 7.008789}, {17.123535, 7.035156}, {17.682556, 7.079102},
    {18.238281, 7.140625}, {18.789612, 7.219727}, {19.335449, 7.316406}, {19.874695, 7.430664},
    {20.406250, 7.562500}, {20.929016, 7.711914}, {21.441895, 7.878906}, {21.943787, 8.063477},
    {22.433594, 8.265625}, {22.910217, 8.485352}, {23.372559, 8.722656}, {23.819519, 8.977539},
    {24.250000, 9.250000}, {24.662903, 9.540039}, {25.057129, 9.847656}, {25.431580, 10.172852},
    {25.785156, 10.515625}, {26.116760, 10.875977}, {26.425293, 11.253906}, {26.709656, 11.649414},
    {26.968750, 12.062500}, {27.201477, 12.493164}, {27.406738, 12.941406}, {27.583435, 13.407227},
    {27.730469, 13.890625}, {27.846741, 14.391602}, {27.931152, 14.910156}, {27.982605, 15.446289},
    {28.000000, 16.000000}, {27.982605, 16.553711}, {27.931152, 17.089844}, {27.846741, 17.608398},
    {27.730469, 18.109375}, {27.583435, 18.592773}, {27.406738, 19.058594}, {27.201477, 19.506836},
    {26.968750, 19.937500}, {26.709656, 20.350586}, {26.425293, 20.746094}, {26.116760, 21.124023},
    {25.785156, 21.484375}, {25.431580, 21.827148}, {25.057129, 22.152344}, {24.662903, 22.459961},
    {24.250000, 22.750000}, {23.819519, 23.022461}, {23.372559, 23.277344}, {22.910217, 23.514648},
    {22.433594, 23.734375}, {21.943787, 23.936523}, {21.441895, 24.121094}, {20.929016, 24.288086},
    {20.406250, 24.437500}, {19.874695, 24.569336}, {19.335449, 24.683594}, {18.789612, 24.780273},
    {18.238281, 24.859375}, {17.682556, 24.920898}, {17.123535, 24.964844}, {16.562317, 24.991211},
    {16.000000, 25.000000}, {15.437683, 24.991211}, {14.876465, 24.964844}, {14.317444, 24.920898},
    {13.761719, 24.859375}, {13.210388, 24.780273}, {12.664551, 24.683594}, {12.125305, 24.569336},
    {11.593750, 24.437500}, {11.070984, 24.288086}, {10.558105, 24.121094}, {10.056213, 23.936523},
    {9.566406, 23.734375}, {9.089783, 23.514648}, {8.627441, 23.277344}, {8.180481, 23.022461},
    {7.750000, 22.750000}, {7.337097, 22.459961}, {6.942871, 22.152344}, {6.568420, 21.827148},
    {6.214844, 21.484375}, {5.883240, 21.124023}, {5.574707, 20.746094}, {5.290344, 20.350586},
    {5.031250, 19.937500}, {4.798523, 19.506836}, {4.593262, 19.058594}, {4.416565, 18.592773},
    {4.269531, 18.109375}, {4.153259, 17.608398}, {4.068848, 17.089844}, {4.017395, 16.553711},
};

static void build_reference(int w, VPath *out) {
    double k = (double)w / 32.0;
    out->count = 0;
    for (int i = 1; i < 128; ++i) {
        VSeg s = {SV_LINE, {REF[i - 1][0] * k, REF[i - 1][1] * k},
                  {REF[i][0] * k, REF[i][1] * k}};
        out->segs[out->count++] = s;
    }
    {
        VSeg z = {SV_LINE, {REF[127][0] * k, REF[127][1] * k},
                  {REF[0][0] * k, REF[0][1] * k}};
        out->segs[out->count++] = z;
    }
}

/* Chessboard distance from each pixel to the nearest on pixel (8-neighbour). */
static void dist_to_on(const unsigned char *b, int w, int h, unsigned char *d) {
    int n = w * h, *q = malloc((size_t)n * sizeof *q), qh = 0, qt = 0;
    for (int i = 0; i < n; ++i) { d[i] = b[i] ? 0 : 255; if (b[i]) q[qt++] = i; }
    while (qh < qt) {
        int i = q[qh++], x = i % w, y = i / w;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int u = x + dx, v = y + dy, j;
                if (u < 0 || v < 0 || u >= w || v >= h) continue;
                j = v * w + u;
                if (d[j] == 255) { d[j] = (unsigned char)(d[i] + 1); q[qt++] = j; }
            }
    }
    free(q);
}

static int one_sided_dev(const unsigned char *a, const unsigned char *b, int w, int h) {
    int n = w * h, m = 0;
    unsigned char *d = malloc((size_t)n);
    dist_to_on(b, w, h, d);
    for (int i = 0; i < n; ++i) if (a[i] && d[i] > m) m = d[i];
    free(d);
    return m;
}

static int run_size(int w, int reps, Row *row, int *dev_out, int *diff_out) {
    int h = w;
    double k = (double)w / 32.0;
    char path[256], file[32];
    VParseOpt opt;
    VPath parsed, ref;
    unsigned char *a = malloc((size_t)w * h), *b = malloc((size_t)w * h);
    double t0, t1, t2;
    int diff = 0, es = 0, r = 0;

    if (!a || !b) { free(a); free(b); return -1; }
    snprintf(path, sizeof path,
             "M%.6f,%.6f C%.6f,%.6f %.6f,%.6f %.6f,%.6f "
             "C%.6f,%.6f %.6f,%.6f %.6f,%.6f Z",
             4 * k, 16 * k, 4 * k, 4 * k, 28 * k, 4 * k, 28 * k, 16 * k,
             28 * k, 28 * k, 4 * k, 28 * k, 4 * k, 16 * k);
    opt.enable_cubic = 1;
    opt.tol = 1.0; /* one pixel in the output raster */

    t0 = now_ms();
    for (int i = 0; i < reps; ++i) r = svg_parse_ex(path, &parsed, &opt, &es);
    t1 = now_ms();
    if (r != 0) { free(a); free(b); return r; }
    for (int i = 0; i < reps; ++i) svg_fill(&parsed, w, h, a);
    t2 = now_ms();

    build_reference(w, &ref);
    svg_fill(&ref, w, h, b);
    for (int i = 0; i < w * h; ++i) if (!!a[i] != !!b[i]) ++diff;
    *diff_out = diff;
    {
        int d1 = one_sided_dev(a, b, w, h), d2 = one_sided_dev(b, a, w, h);
        *dev_out = d1 > d2 ? d1 : d2;
    }
    snprintf(file, sizeof file, "demo_%d.pgm", w);
    if (svg_save_pgm(file, a, w, h) != 0) { free(a); free(b); return -2; }

    row->name = w == 32 ? "32x32" : "256x256";
    row->ctrl_nums = 6; /* 3 coordinate pairs per C group */
    row->ctrl_pts = 3;  /* 2 control points + 1 endpoint */
    row->seg_first = 1; /* M/m takes no segment number */
    row->seg_last = 2;
    row->segs = parsed.count;
    row->parse_ms = (t1 - t0) / reps;
    row->fill_ms = (t2 - t1) / reps;
    free(a);
    free(b);
    return 0;
}

int main(void) {
    Row rows[2];
    int dev[2], diff[2];
    int reps[2] = {20000, 200};
    int sizes[2] = {32, 256};

    for (int i = 0; i < 2; ++i) {
        int r = run_size(sizes[i], reps[i], &rows[i], &dev[i], &diff[i]);
        if (r != 0) { printf("size %d failed: %d\n", sizes[i], r); return 1; }
    }
    printf("%-8s %-9s %-9s %-7s %-6s %-10s %-9s %-5s %-3s\n",
           "size", "ctrl_nums", "ctrl_pts", "segno", "segs",
           "parse_ms", "fill_ms", "diff", "dev");
    for (int i = 0; i < 2; ++i)
        printf("%-8s %-9d %-9d %d..%d   %-6d %-10.5f %-9.5f %-5d %-3d\n",
               rows[i].name, rows[i].ctrl_nums, rows[i].ctrl_pts,
               rows[i].seg_first, rows[i].seg_last, rows[i].segs,
               rows[i].parse_ms, rows[i].fill_ms, diff[i], dev[i]);
    printf("wrote demo_32.pgm demo_256.pgm\n");
    return 0;
}
