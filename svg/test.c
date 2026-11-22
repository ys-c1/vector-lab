#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* ---- legacy byte-identity gates, hashes taken from the pre-cubic parser ---- */

typedef struct { const char *d; int w, h; int segs, on; unsigned long long fill_fnv, seg_fnv; } Legacy;
static const Legacy LEGACY[] = {
    {"M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z", 50, 50, 8, 1165,
     0x8a1c4d04489fe760ULL, 0xee474ff0c8e13463ULL},
    {"M0,0 L10,0 L10,10 Z", 20, 20, 3, 55,
     0x7c2470958e5202bcULL, 0x67fecf35cd4bac51ULL},
    {"M3,3 L29,3 L29,29 L3,29 Z M9,9 L23,9 L23,23 L9,23 Z", 32, 32, 8, 464,
     0x81b1481cc5479bd1ULL, 0x0a570b1ce5df4183ULL},
    {"M0,0 L100,0 L100,50 L0,80 Z", 256, 256, 4, 6500,
     0x046e60d4fc26bed9ULL, 0x09c736148d0fe2d3ULL},
    {"M2.5,2.5 l10,0 l0,10 Z m2,2 l4,0 l0,4 z", 32, 32, 6, 50,
     0xfb57329f16b63103ULL, 0x7d8b11fbf0d17e03ULL},
};

static unsigned long long fnv(const void *v, size_t n) {
    const unsigned char *p = (const unsigned char *)v;
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static void check_legacy(void) {
    for (size_t t = 0; t < sizeof LEGACY / sizeof LEGACY[0]; ++t) {
        const Legacy *L = &LEGACY[t];
        VPath p;
        unsigned char *buf = malloc((size_t)L->w * L->h);
        unsigned long long seg_h = 1469598103934665603ULL;
        int on = 0;
        if (!buf) return;
        CK(svg_parse(L->d, &p) == 0);
        CK(p.count == L->segs);
        CK(svg_fill(&p, L->w, L->h, buf) == 0);
        for (int i = 0; i < L->w * L->h; ++i) if (buf[i]) ++on;
        CK(on == L->on);
        CK(fnv(buf, (size_t)L->w * L->h) == L->fill_fnv);
        /* Segment-level hash, mixed in the same order as the baseline dump:
           kind, a.x, a.y, b.x, b.y per segment. */
        seg_h = 1469598103934665603ULL;
        for (int i = 0; i < p.count; ++i) {
            int kind = (int)p.segs[i].kind;
            unsigned char chunk[sizeof(int) + 4 * sizeof(double)];
            memcpy(chunk, &kind, sizeof(int));
            memcpy(chunk + sizeof(int), &p.segs[i].a.x, sizeof(double));
            memcpy(chunk + sizeof(int) + sizeof(double), &p.segs[i].a.y, sizeof(double));
            memcpy(chunk + sizeof(int) + 2 * sizeof(double), &p.segs[i].b.x, sizeof(double));
            memcpy(chunk + sizeof(int) + 3 * sizeof(double), &p.segs[i].b.y, sizeof(double));
            for (size_t k = 0; k < sizeof chunk; ++k) {
                seg_h ^= chunk[k];
                seg_h *= 1099511628211ULL;
            }
        }
        CK(seg_h == L->seg_fnv);
        free(buf);
    }
}

/* ---- cubic subdivision vs a hand-written polyline reference ---- */

/*
 * Reference polygon: the two-cubic lens M4,16 C4,4 28,4 28,16 C28,28 4,28 4,16 Z,
 * sampled by hand at 1/64 of each cubic (64 points each, shared seam point
 * written once) in 32x32 user units.  Used at both output sizes by scaling.
 * The reference is denser than any subdivided output, so the pixel comparison
 * measures the subdivision, not the reference.
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

/* Chessboard distance from each pixel to the nearest on pixel, 8-neighbour. */
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

/* Largest distance from an on pixel of a to an on pixel of b, in pixels. */
static int one_sided_dev(const unsigned char *a, const unsigned char *b, int w, int h) {
    int n = w * h, m = 0;
    unsigned char *d = malloc((size_t)n);
    dist_to_on(b, w, h, d);
    for (int i = 0; i < n; ++i) if (a[i] && d[i] > m) m = d[i];
    free(d);
    return m;
}

static void build_reference(int w, int h, VPath *out) {
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
    (void)h;
}

/* One cubic group spread over `w`; deviation must stay within one pixel. */
static void check_cubic_vs_reference(int w, double tol_px) {
    int h = w;
    char path[256];
    double k = (double)w / 32.0;
    VParseOpt o;
    VPath parsed, ref;
    unsigned char *a = malloc((size_t)w * h), *b = malloc((size_t)w * h);
    if (!a || !b) { free(a); free(b); return; }
    snprintf(path, sizeof path,
             "M%.6f,%.6f C%.6f,%.6f %.6f,%.6f %.6f,%.6f "
             "C%.6f,%.6f %.6f,%.6f %.6f,%.6f Z",
             4 * k, 16 * k, 4 * k, 4 * k, 28 * k, 4 * k, 28 * k, 16 * k,
             28 * k, 28 * k, 4 * k, 28 * k, 4 * k, 16 * k);
    o.enable_cubic = 1;
    o.tol = tol_px; /* user units are pixels here because the path is scaled */
    CK(svg_parse_ex(path, &parsed, &o, 0) == 0);
    CK(parsed.count > 8);
    build_reference(w, h, &ref);
    CK(svg_fill(&parsed, w, h, a) == 0);
    CK(svg_fill(&ref, w, h, b) == 0);
    {
        int d1 = one_sided_dev(a, b, w, h), d2 = one_sided_dev(b, a, w, h);
        int dev = d1 > d2 ? d1 : d2;
        if (dev > 1) printf("  dev at %dx%d tol %.2f: %d\n", w, h, tol_px, dev);
        CK(dev <= 1);
    }
    free(a);
    free(b);
}

/* Error reporting, segment numbers, tolerance validation. */
static void check_errors(void) {
    VParseOpt on = {1, 0.1}, off = {0, 0.0};
    VPath p;
    int seg = -1;

    seg = -1;
    CK(svg_parse_ex("M0,0 C1,2 3,4", &p, &on, &seg) == SVG_ERR_CTRL_COUNT);
    CK(seg == 1);
    seg = -1;
    CK(svg_parse_ex("M0,0 L1,1 C1,2 3,4", &p, &on, &seg) == SVG_ERR_CTRL_COUNT);
    CK(seg == 2);
    seg = -1;
    CK(svg_parse_ex("M0,0 C1,2 3,4 5,6 C1,2", &p, &on, &seg) == SVG_ERR_CTRL_COUNT);
    CK(seg == 2);
    seg = -1;
    CK(svg_parse_ex("M0,0 C1,2 3,4 5,6 1,2 3,4", &p, &on, &seg) == SVG_ERR_CTRL_COUNT);
    CK(seg == 1);
    seg = -1;
    CK(svg_parse_ex("M0,0 C1,2 3,4 nan,6", &p, &on, &seg) == SVG_ERR_NOT_FINITE);
    CK(seg == 1);
    seg = -1;
    CK(svg_parse_ex("M0,0 L1,1 C1,2 inf,4 5,6", &p, &on, &seg) == SVG_ERR_NOT_FINITE);
    CK(seg == 2);
    /* Large but finite coordinates stay legal (controls on the chord). */
    CK(svg_parse_ex("M0,0 C300000,0 600000,0 900000,0", &p, &on, 0) == 0);
    CK(p.count == 1);

    /* C still refused when cubic is off, with the old code. */
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    seg = -1;
    CK(svg_parse_ex("M0,0 C1,2 3,4 5,6", &p, &off, &seg) == -3);
    CK(seg == 1);

    /* Tolerance must be usable. */
    CK(svg_parse_ex("M0,0 L1,1", &p, &(VParseOpt){1, 0.0}, 0) == SVG_ERR_TOL);
    CK(svg_parse_ex("M0,0 L1,1", &p, &(VParseOpt){1, -1.0}, 0) == SVG_ERR_TOL);
    CK(svg_parse_ex("M0,0 L1,1", &p, &(VParseOpt){1, NAN}, 0) == SVG_ERR_TOL);

    /* Repeated groups are not SVG shorthand here: every C takes exactly one. */
    seg = -1;
    CK(svg_parse_ex("M0,0 C1,2 3,4 5,6 7,8 9,10 11,12", &p, &on, &seg) == SVG_ERR_CTRL_COUNT);
    CK(seg == 1);
    CK(svg_parse_ex("M0,0 c1,2 3,4 5,6", &p, &on, 0) == 0);
    CK(p.count >= 2);
    /* A stray number after a complete group is also a count error. */
    seg = -1;
    CK(svg_parse_ex("M0,0 L1,1 C1,2 3,4 5,6 7", &p, &on, &seg) == SVG_ERR_CTRL_COUNT);
    CK(seg == 2);

    /* Subdivision that would exceed the segment cap reports -5 on that C. */
    seg = -1;
    CK(svg_parse_ex("M0,0 C0,0 1000,1000 2000,0", &p, &(VParseOpt){1, 1e-9}, &seg) == -5);
    CK(seg == 1);

    /* Error text is populated for every code we expose. */
    {
        int codes[] = {0, -1, -2, -3, -4, -5, SVG_ERR_CTRL_COUNT,
                       SVG_ERR_NOT_FINITE, SVG_ERR_TOL, -999};
        for (size_t i = 0; i < sizeof codes / sizeof codes[0]; ++i)
            CK(svg_strerror(codes[i]) != 0 && svg_strerror(codes[i])[0]);
    }
}

int main(void) {
    check_legacy();
    check_errors();
    check_cubic_vs_reference(32, 0.5);
    check_cubic_vs_reference(32, 1.0);
    check_cubic_vs_reference(64, 1.0);
    check_cubic_vs_reference(256, 0.5);
    check_cubic_vs_reference(256, 1.0);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
