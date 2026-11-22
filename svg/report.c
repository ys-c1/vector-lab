#define _POSIX_C_SOURCE 199309L
#include "src/svgpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    rewind(f);
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

static const char *diag_name(VDiagCode c) {
    switch (c) {
    case SV_DIAG_EMPTY_PATH: return "empty_path";
    case SV_DIAG_EMPTY_SUB: return "empty_subpath";
    case SV_DIAG_OPEN_SUB: return "unclosed_subpath";
    case SV_DIAG_SYNTAX: return "syntax";
    case SV_DIAG_OVERFLOW: return "overflow";
    default: return "none";
    }
}

static int load_pgm(const char *path, unsigned char **buf, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char magic[3] = {0};
    int ww = 0, hh = 0, maxv = 0;
    if (fscanf(f, "%2s %d %d %d", magic, &ww, &hh, &maxv) != 4
        || strcmp(magic, "P5") != 0 || ww <= 0 || hh <= 0 || maxv != 255) {
        fclose(f);
        return -2;
    }
    fgetc(f);   /* single whitespace after maxval */
    unsigned char *b = (unsigned char*)malloc((size_t)ww * hh);
    if (!b) { fclose(f); return -3; }
    size_t got = fread(b, 1, (size_t)ww * hh, f);
    fclose(f);
    if (got != (size_t)ww * hh) { free(b); return -4; }
    *buf = b; *w = ww; *h = hh;
    return 0;
}

static const char *orient(double a) {
    if (a > 1e-9) return "ccw";
    if (a < -1e-9) return "cw";
    return "degenerate";
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s scene.d out.pgm [w h ss samples ref.pgm]\n", argv[0]);
        return 1;
    }
    const char *scene_path = argv[1];
    const char *out_path = argv[2];
    int w = (argc > 3) ? atoi(argv[3]) : 256;
    int h = (argc > 4) ? atoi(argv[4]) : 256;
    int ss = (argc > 5) ? atoi(argv[5]) : 8;
    int samples = (argc > 6) ? atoi(argv[6]) : 512;
    const char *ref_path = (argc > 7) ? argv[7] : 0;

    char *d = read_file(scene_path);
    if (!d) { fprintf(stderr, "open scene failed\n"); return 2; }

    VPath p;
    VDiag diag;
    double t0 = now_us();
    int rc = svg_parse_diag(d, &p, &diag);
    double t_parse = now_us() - t0;
    if (rc != 0) {
        printf("scene %s\n", scene_path);
        printf("status reject\n");
        printf("reject %s sub %d pos %d rc %d\n",
               diag_name(diag.code), diag.sub, diag.pos, rc);
        free(d);
        return 3;
    }

    VBox box;
    svg_bbox(&p, &box);

    printf("scene %s\n", scene_path);
    printf("status ok\n");
    printf("subs %d\n", p.nsubs);
    for (int s = 0; s < p.nsubs; ++s) {
        double a = svg_sub_signed_area(&p, s);
        printf("sub %d verts %d signed_area %.6f orient %s self_cross %d\n",
               s + 1, p.sub_nseg[s] + 1, a, orient(a),
               svg_self_intersections(&p, s));
    }
    int sum = svg_rule_signed_sum(&p);
    int rule = svg_rule(&p);
    printf("signed_sum %d rule %s\n", sum,
           rule == SV_FILL_NONZERO ? "nonzero" : "evenodd");
    printf("bbox %.6f %.6f %.6f %.6f\n",
           box.minx, box.miny, box.maxx, box.maxy);

    /* A self-intersecting subpath can wind more than once, so the range
     * must cover +/- total edge count, not just +/- subpath count. */
    int wmax = p.count > 0 ? p.count : 1;
    int span = 2 * wmax + 1;
    int *hist = (int*)calloc((size_t)span, sizeof(int));
    int *sub_hist = (int*)calloc((size_t)p.nsubs * (size_t)span, sizeof(int));
    int *contrib = (int*)malloc((size_t)p.nsubs * sizeof(int));
    if (!hist || !sub_hist || !contrib) { free(d); return 4; }

    long joint_zero = 0;
    long *joint = 0;
    int jb0 = 0, jb1 = 0;
    if (p.nsubs == 2) {
        jb0 = p.sub_nseg[0] + 1;
        jb1 = p.sub_nseg[1] + 1;
        joint = (long*)calloc((size_t)(2 * jb0 + 1) * (size_t)(2 * jb1 + 1),
                              sizeof(long));
        if (!joint) { free(d); return 4; }
    }

    double t1 = now_us();
    double dx = (box.maxx - box.minx) / samples;
    double dy = (box.maxy - box.miny) / samples;
    long zero = 0;
    long hit_even = 0, hit_nonzero = 0, hit_derived = 0;

    for (int j = 0; j < samples; ++j) {
        double py = box.miny + (j + 0.5) * dy;
        for (int i = 0; i < samples; ++i) {
            double px = box.minx + (i + 0.5) * dx;
            int tw = svg_winding(&p, px, py, contrib);
            if (tw < -wmax) tw = -wmax;
            if (tw > wmax) tw = wmax;
            hist[tw + wmax] += 1;
            if (tw == 0) ++zero;
            if (svg_sample_filled(SV_FILL_EVENODD, tw)) ++hit_even;
            if (svg_sample_filled(SV_FILL_NONZERO, tw)) ++hit_nonzero;
            if (svg_sample_filled(rule, tw)) ++hit_derived;
            for (int s = 0; s < p.nsubs; ++s) {
                int c = contrib[s];
                if (c < -wmax) c = -wmax;
                if (c > wmax) c = wmax;
                sub_hist[(size_t)s * span + c + wmax] += 1;
            }
            if (p.nsubs == 2 && joint) {
                int a = contrib[0], b = contrib[1];
                if (a == 0 && b == 0) ++joint_zero;
                if (a >= -jb0 && a <= jb0 && b >= -jb1 && b <= jb1)
                    joint[(size_t)(a + jb0) * (2 * jb1 + 1) + (b + jb1)] += 1;
            }
        }
    }
    double t_sample = now_us() - t1;

    printf("grid %d cell_area %.9f\n", samples, dx * dy);
    printf("hist");
    for (int k = 0; k < span; ++k)
        if (hist[k]) printf(" %d:%d", k - wmax, hist[k]);
    printf("\n");
    printf("winding_zero %ld\n", zero);
    for (int s = 0; s < p.nsubs; ++s) {
        printf("subcontrib %d", s + 1);
        for (int k = 0; k < span; ++k) {
            int c = sub_hist[(size_t)s * span + k];
            if (c) printf(" %d:%d", k - wmax, c);
        }
        printf("\n");
    }
    if (joint) {
        printf("joint_zero %ld\n", joint_zero);
        printf("joint");
        for (int a = -jb0; a <= jb0; ++a)
            for (int b = -jb1; b <= jb1; ++b) {
                long c = joint[(size_t)(a + jb0) * (2 * jb1 + 1) + (b + jb1)];
                if (c) printf(" (%d,%d):%ld", a, b, c);
            }
        printf("\n");
    }

    double cell = dx * dy;
    printf("area evenodd %.6f nonzero %.6f derived %.6f\n",
           (double)hit_even * cell, (double)hit_nonzero * cell,
           (double)hit_derived * cell);

    /* ---- rasterise the derived rule and compare with the reference ---- */
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) { free(d); return 4; }
    double t2 = now_us();
    int rr = svg_raster(&p, box, w, h, ss, rule, buf);
    double t_raster = now_us() - t2;
    if (rr != 0) {
        printf("status raster_error rc %d\n", rr);
        free(d); free(buf);
        return 5;
    }
    if (svg_save_pgm(out_path, buf, w, h) != 0) {
        fprintf(stderr, "write %s failed\n", out_path);
        free(d); free(buf);
        return 6;
    }
    int on = 0;
    for (int i = 0; i < w * h; ++i) if (buf[i]) ++on;
    printf("raster %dx%d ss %d on %d total %d\n", w, h, ss, on, w * h);

    if (ref_path) {
        unsigned char *ref = 0;
        int rw = 0, rh = 0;
        int lr = load_pgm(ref_path, &ref, &rw, &rh);
        if (lr != 0 || rw != w || rh != h) {
            printf("differ na ref_error %d ref %dx%d\n", lr, rw, rh);
        } else {
            long diff = 0;
            for (int i = 0; i < w * h; ++i)
                if ((buf[i] != 0) != (ref[i] != 0)) ++diff;
            long budget = (long)(w * h) / 2000;
            printf("differ %ld budget %ld verdict %s\n",
                   diff, budget, (diff <= budget) ? "ok" : "over");
            free(ref);
        }
    } else {
        printf("differ na\n");
    }

    double t_total = now_us() - t0;
    printf("time parse_us %.1f sample_us %.1f raster_us %.1f total_us %.1f\n",
           t_parse, t_sample, t_raster, t_total);

    free(hist); free(sub_hist); free(contrib); free(joint); free(buf); free(d);
    return 0;
}
