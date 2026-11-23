/* Sentinel acceptance harness.
 *
 * For each scene it renders repeatedly with different sentinel pre-fill
 * values and reports, per scene:
 *   - differing pixels between the sentinel-A run and the sentinel-B run
 *     (must be 0: output may not depend on uninitialised memory),
 *   - differing pixels across repeated runs (must be 0: determinism),
 *   - differing pixels against the pinned pre-change image (must be 0),
 *   - the number of sentinel fill operations performed,
 *   - elapsed time.
 *
 * It then checks that a deliberately broken kernel, which copies one pixel
 * from the pre-fill, IS caught and reported at the right pixel.  A detector
 * that fails to catch the planted fault makes this program exit non-zero.
 */
#define _POSIX_C_SOURCE 200809L
#include "src/guard.h"
#include "src/pipeline.h"
#include "src/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char *name;
    const char *path;
    int w, h;
    double scale;
} SceneCase;

static const SceneCase CASES[] = {
    {"s1_square",   "scenes/s1_square.txt",    64,  64, 1.0},
    {"s2_two_polys", "scenes/s2_two_polys.txt", 96,  72, 1.0},
    {"s3_concave",  "scenes/s3_concave.txt",  128,  96, 0.8},
    {"s4_dense",    "scenes/s4_dense.txt",    160, 120, 1.25},
};

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    rguard_buffer(s, sizeof *s);
    scene_init(s);
    char tag[16];
    while (fscanf(f, "%15s", tag) == 1) {
        if (tag[0] != 'p') break;
        RPoint pts[64];
        int n = 0;
        while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) {
            ++n;
            int c = fgetc(f);
            if (c == '\n' || c == EOF) break;
            ungetc(c, f);
        }
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

typedef struct {
    RGuardRegion region;   /* payload has sentinel bands on both sides */
    unsigned char *buf;
    size_t n;
    unsigned long long fills;
    unsigned long long fill_bytes;
    int band_ok;
    long band_off;
    double ms;
} Run;

static int render_once(const RScene *s, const SceneCase *c, RBox win,
                       int enabled, unsigned char sentinel, Run *out) {
    memset(out, 0, sizeof *out);
    out->n = (size_t)c->w * (size_t)c->h;
    if (rguard_region_init(&out->region, out->n, 64, sentinel) != 0) return -1;
    out->buf = out->region.payload;
    rguard_configure(enabled, sentinel);
    rguard_reset();
    RPlace place = {0.0, 0.0, 0.0, c->scale};
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    /* The caller pre-fills the output buffer, exactly as the CLI does. */
    rguard_buffer(out->buf, out->n);
    int rc = pipeline_render(s, place, win, c->w, c->h, out->buf);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    out->ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
              (t1.tv_nsec - t0.tv_nsec) / 1e6;
    out->fills = rguard_stats()->fill_ops;
    out->fill_bytes = rguard_stats()->fill_bytes;
    out->band_ok = rguard_region_check(&out->region, &out->band_off);
    return rc == 0 ? 0 : -2;
}

static int read_expected(const char *path, unsigned char **buf, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int w = 0, h = 0, maxv = 0;
    if (fscanf(f, "P5 %d %d %d", &w, &h, &maxv) != 3) { fclose(f); return -1; }
    fgetc(f); /* single whitespace after header */
    *n = (size_t)w * (size_t)h;
    *buf = (unsigned char *)malloc(*n);
    if (!*buf) { fclose(f); return -1; }
    if (fread(*buf, 1, *n, f) != *n) { fclose(f); free(*buf); return -1; }
    fclose(f);
    return 0;
}

static int failures;

int main(int argc, char **argv) {
    int verbose = argc > 1 && !strcmp(argv[1], "-v");

    printf("== detector selftest (planted faults must be caught) ==\n");
    if (rguard_selftest(verbose) != 0) {
        printf("SELFTEST FAILED: detector cannot detect planted faults\n");
        return 1;
    }
    printf("selftest OK\n");

    printf("\n%-13s %8s %8s %8s %8s %8s %8s %10s\n",
           "scene", "unsent", "diffAB", "repdiff", "vsExp", "fills",
           "fillkB", "ms");
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; ++i) {
        const SceneCase *c = &CASES[i];
        RScene s;
        if (load_scene(c->path, &s) != 0) {
            printf("%-13s LOAD FAILED\n", c->name);
            ++failures;
            continue;
        }
        RBox win;
        if (scene_bbox(&s, &win) != 0) {
            printf("%-13s EMPTY SCENE\n", c->name);
            ++failures;
            continue;
        }

        Run a, b, r1, r2, r3, off;
        if (render_once(&s, c, win, 1, RGUARD_SENTINEL_A, &a) ||
            render_once(&s, c, win, 1, RGUARD_SENTINEL_B, &b) ||
            render_once(&s, c, win, 1, RGUARD_SENTINEL_A, &r1) ||
            render_once(&s, c, win, 1, RGUARD_SENTINEL_A, &r2) ||
            render_once(&s, c, win, 1, RGUARD_SENTINEL_A, &r3) ||
            render_once(&s, c, win, 0, RGUARD_SENTINEL_A, &off)) {
            printf("%-13s RENDER FAILED\n", c->name);
            ++failures;
            continue;
        }

        /* Sentinel bytes that survived into the output = pixels no code wrote. */
        size_t uoff = 0, ucnt = 0;
        int unsent = rguard_scan(a.buf, a.n, RGUARD_SENTINEL_A, &uoff, &ucnt);

        size_t d_ab_off = 0, d_rep_off = 0, d_exp_off = 0, d_off_off = 0;
        size_t d_ab = rguard_diff(a.buf, b.buf, a.n, &d_ab_off);
        size_t d_rep = rguard_diff(r1.buf, r2.buf, r1.n, &d_rep_off);
        if (rguard_diff(r2.buf, r3.buf, r2.n, &d_rep_off)) ++d_rep;
        size_t d_off = rguard_diff(a.buf, off.buf, a.n, &d_off_off);

        unsigned char *exp = NULL;
        size_t exp_n = 0;
        char epath[512];
        snprintf(epath, sizeof epath, "scenes/expected/%s.pgm", c->name);
        size_t d_exp = 0;
        if (read_expected(epath, &exp, &exp_n) == 0 && exp_n == a.n) {
            d_exp = rguard_diff(a.buf, exp, a.n, &d_exp_off);
        } else {
            d_exp = (size_t)-1; /* expected image missing or wrong size */
        }
        free(exp);

        printf("%-13s %8s %8zu %8zu %8s %8llu %8llu %10.3f\n",
               c->name, unsent ? "YES" : "no", d_ab, d_rep,
               d_exp == (size_t)-1 ? "MISSING" : "0",
               a.fills, a.fill_bytes / 1024, a.ms);

        int ok = 1;
        if (unsent) {
            printf("  FAIL sentinel survived into output at pixel %zu (%zu px)\n",
                   uoff, ucnt);
            ok = 0;
        }
        if (d_ab) {
            printf("  FAIL output depends on sentinel value: %zu px differ, "
                   "first at pixel %zu\n", d_ab, d_ab_off);
            ok = 0;
        }
        if (d_rep) {
            printf("  FAIL repeated runs differ: %zu px (first at %zu)\n",
                   d_rep, d_rep_off);
            ok = 0;
        }
        if (d_exp != 0) {
            if (d_exp == (size_t)-1)
                printf("  FAIL expected image %s missing\n", epath);
            else
                printf("  FAIL differs from pre-change image: %zu px, "
                       "first at pixel %zu\n", d_exp, d_exp_off);
            ok = 0;
        }
        if (d_off) {
            printf("  FAIL guard=off output differs from guard=on: %zu px\n",
                   d_off);
            ok = 0;
        }
        if (!a.band_ok || !b.band_ok || !r1.band_ok || !r2.band_ok ||
            !r3.band_ok || !off.band_ok) {
            printf("  FAIL guard band written: out-of-bounds write at "
                   "relative offset %ld\n", a.band_off);
            ok = 0;
        }
        if (!ok) ++failures;

        free(a.region.base); free(b.region.base); free(r1.region.base);
        free(r2.region.base); free(r3.region.base); free(off.region.base);

        /* End-to-end fault injection: skip one real pixel write and require
         * the same detectors to flag exactly that pixel.  A harness that
         * cannot detect here is failing the acceptance criterion. */
        {
            int px = c->w * 3 + c->w / 2;
            Run ia, ib;
            rguard_inject_skip_pixel(px);
            int rc = render_once(&s, c, win, 1, RGUARD_SENTINEL_A, &ia) ||
                     render_once(&s, c, win, 1, RGUARD_SENTINEL_B, &ib);
            rguard_inject_skip_pixel(-1);
            if (rc) {
                printf("  FAIL injected render failed\n");
                ++failures;
            } else {
                size_t soff = 0, scnt = 0;
                int su = rguard_scan(ia.buf, ia.n, RGUARD_SENTINEL_A,
                                     &soff, &scnt);
                size_t doff = 0;
                size_t dd = rguard_diff(ia.buf, ib.buf, ia.n, &doff);
                int caught = su && scnt == 1 && soff == (size_t)px &&
                             dd == 1 && doff == (size_t)px;
                printf("  injected skip at pixel %d: %s "
                       "(sentinel@%zu x%zu, diff@%zu x%zu)\n",
                       px, caught ? "caught" : "MISSED", soff, scnt, doff, dd);
                if (!caught) ++failures;
            }
            free(ia.region.base); free(ib.region.base);
        }
    }

    /* Planted fault: a kernel that copies one pixel from the pre-fill must be
     * detected, at that pixel offset.  This proves the harness has teeth. */
    {
        const size_t N = 256, plant = 91;
        RGuardRegion ra, rb;
        unsigned char *oa, *ob;
        if (rguard_region_init(&ra, N, 16, RGUARD_SENTINEL_A) != 0 ||
            rguard_region_init(&rb, N, 16, RGUARD_SENTINEL_B) != 0) {
            printf("\nplanted-fault region alloc failed -> FAIL\n");
            return 1;
        }
        oa = (unsigned char *)malloc(N);
        ob = (unsigned char *)malloc(N);
        for (size_t i = 0; i < N; ++i) {
            oa[i] = (i == plant) ? ra.payload[i] : (unsigned char)(i * 13u);
            ob[i] = (i == plant) ? rb.payload[i] : (unsigned char)(i * 13u);
        }
        size_t off = 0;
        size_t d = rguard_diff(oa, ob, N, &off);
        printf("\nplanted uninitialised read at pixel %zu: ", plant);
        int ok = (d == 1 && off == plant);
        if (ok) {
            printf("DETECTED (offset %zu)\n", off);
        } else {
            printf("NOT DETECTED (diff %zu, offset %zu) -> FAIL\n", d, off);
            ++failures;
        }
        free(oa); free(ob);
        rguard_region_free(&ra);
        rguard_region_free(&rb);
    }

    printf("\n%s\n", failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures ? 1 : 0;
}
