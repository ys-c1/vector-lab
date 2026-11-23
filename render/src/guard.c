#include "guard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RGuardStats g_stats;
static int g_enabled = 1;
static unsigned char g_sentinel = RGUARD_SENTINEL_A;
static int g_skip_pixel = -1;

void rguard_inject_skip_pixel(int index) { g_skip_pixel = index; }
int rguard_injected_skip_pixel(void) { return g_skip_pixel; }

void rguard_configure(int enabled, unsigned char sentinel) {
    g_enabled = enabled ? 1 : 0;
    g_sentinel = sentinel;
}

int rguard_enabled(void) { return g_enabled; }

unsigned char rguard_sentinel_value(void) { return g_sentinel; }

void rguard_buffer(void *p, size_t n) {
    if (!p || n == 0) return;
    if (g_enabled) {
        rguard_fill(p, n, g_sentinel);
    } else {
        memset(p, 0, n);
    }
}

void rguard_reset(void) {
    g_stats.fill_ops = 0;
    g_stats.fill_bytes = 0;
}

const RGuardStats *rguard_stats(void) { return &g_stats; }

void rguard_fill(void *p, size_t n, unsigned char v) {
    if (!p || n == 0) return;
    memset(p, (int)v, n);
    ++g_stats.fill_ops;
    g_stats.fill_bytes += (unsigned long long)n;
}

int rguard_scan(const void *p, size_t n, unsigned char v,
                size_t *first_off, size_t *count) {
    const unsigned char *b = (const unsigned char *)p;
    size_t c = 0, first = 0;
    int have = 0;
    if (!p) {
        if (first_off) *first_off = 0;
        if (count) *count = 0;
        return 0;
    }
    for (size_t i = 0; i < n; ++i)
        if (b[i] == v) {
            if (!have) { first = i; have = 1; }
            ++c;
        }
    if (first_off) *first_off = have ? first : 0;
    if (count) *count = c;
    return have;
}

int rguard_scan_not(const void *p, size_t n, unsigned char v,
                    size_t *first_off, size_t *count) {
    const unsigned char *b = (const unsigned char *)p;
    size_t c = 0, first = 0;
    int have = 0;
    if (!p) {
        if (first_off) *first_off = 0;
        if (count) *count = 0;
        return 0;
    }
    for (size_t i = 0; i < n; ++i)
        if (b[i] != v) {
            if (!have) { first = i; have = 1; }
            ++c;
        }
    if (first_off) *first_off = have ? first : 0;
    if (count) *count = c;
    return have;
}

size_t rguard_diff(const unsigned char *a, const unsigned char *b, size_t n,
                   size_t *first_off) {
    size_t c = 0, first = 0;
    int have = 0;
    if (first_off) *first_off = 0;
    if (!a || !b) return 0;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) {
            if (!have) { first = i; have = 1; }
            ++c;
        }
    if (first_off && have) *first_off = first;
    return c;
}

int rguard_region_init(RGuardRegion *r, size_t payload_n, size_t band,
                       unsigned char sentinel) {
    if (!r || payload_n == 0) return -1;
    if (band == 0) band = 1;
    unsigned char *base = (unsigned char *)malloc(payload_n + 2 * band);
    if (!base) return -2;
    r->base = base;
    r->payload = base + band;
    r->payload_n = payload_n;
    r->band = band;
    r->sentinel = sentinel;
    rguard_fill(base, band, sentinel);
    rguard_fill(r->payload, payload_n, sentinel);
    rguard_fill(r->payload + payload_n, band, sentinel);
    return 0;
}

void rguard_region_free(RGuardRegion *r) {
    if (!r) return;
    free(r->base);
    r->base = r->payload = NULL;
    r->payload_n = r->band = 0;
}

int rguard_region_check(const RGuardRegion *r, long *offset) {
    size_t off = 0, cnt = 0;
    unsigned char v;
    if (offset) *offset = 0;
    if (!r || !r->base) return 1;
    v = r->sentinel;
    if (rguard_scan_not(r->base, r->band, v, &off, &cnt)) {
        if (offset) *offset = -(long)(r->band - off);
        return 0;
    }
    if (rguard_scan_not(r->payload + r->payload_n, r->band, v, &off, &cnt)) {
        if (offset) *offset = (long)r->payload_n + (long)off;
        return 0;
    }
    return 1;
}

/* --- detector validation ------------------------------------------------- */

static int st_fail;
static int st_run;

static void st_report(int verbose, const char *name, int ok,
                      long expect, long got) {
    ++st_run;
    if (!ok) ++st_fail;
    if (verbose)
        printf("  selftest %-22s %s (expected offset %ld, got %ld)\n",
               name, ok ? "PASS" : "FAIL", expect, got);
}

/* Intentionally incorrect kernel: one output pixel is copied straight from
 * the pre-fill (i.e. uninitialised) buffer.  This reproduces the reported
 * failure mode and lets us verify the differential detector catches it. */
static void buggy_kernel(const unsigned char *feed, unsigned char *out,
                         size_t n, size_t planted) {
    for (size_t i = 0; i < n; ++i)
        out[i] = (i == planted) ? feed[i] : (unsigned char)(i * 7u);
}

int rguard_selftest(int verbose) {
    st_fail = 0;
    st_run = 0;

    /* 1. Untouched-sentinel scan on a clean buffer: nothing to report. */
    {
        unsigned char buf[64];
        rguard_fill(buf, sizeof buf, RGUARD_SENTINEL_A);
        size_t off = 111, cnt = 111;
        int hit = rguard_scan(buf, sizeof buf, RGUARD_SENTINEL_A, &off, &cnt);
        st_report(verbose, "scan-clean", hit && cnt == 64 && off == 0, 0,
                  hit ? (long)off : -1);
    }

    /* 2. Sentinel modified at a known offset must be found there. */
    {
        unsigned char buf[64];
        rguard_fill(buf, sizeof buf, RGUARD_SENTINEL_A);
        buf[21] = 0x00;
        size_t off = 0, cnt = 0;
        int hit = rguard_scan_not(buf, sizeof buf, RGUARD_SENTINEL_A, &off, &cnt);
        st_report(verbose, "scan-modified-1", hit && cnt == 1 && off == 21,
                  21, hit ? (long)off : -1);
    }

    /* 3. Several modifications: report the first offset and the count. */
    {
        unsigned char buf[64];
        rguard_fill(buf, sizeof buf, RGUARD_SENTINEL_A);
        buf[5] = 0x11;
        buf[41] = 0x22;
        size_t off = 0, cnt = 0;
        int hit = rguard_scan_not(buf, sizeof buf, RGUARD_SENTINEL_A, &off, &cnt);
        st_report(verbose, "scan-modified-2", hit && cnt == 2 && off == 5,
                  5, hit ? (long)off : -1);
    }

    /* 4. Write in the band before the payload. */
    {
        RGuardRegion r;
        long off = 0;
        int ok = rguard_region_init(&r, 97, 7, RGUARD_SENTINEL_A) == 0;
        ok = ok && rguard_region_check(&r, &off) == 1;
        if (ok) {
            r.payload[-1] = 0x00;
            ok = rguard_region_check(&r, &off) == 0 && off == -1;
        } else {
            off = -999;
        }
        st_report(verbose, "band-before", ok, -1, off);
        rguard_region_free(&r);
    }

    /* 5. Write in the band after the payload. */
    {
        RGuardRegion r;
        long off = 0;
        int ok = rguard_region_init(&r, 97, 7, RGUARD_SENTINEL_A) == 0;
        ok = ok && rguard_region_check(&r, &off) == 1;
        if (ok) {
            r.payload[97 + 2] = 0x00;
            ok = rguard_region_check(&r, &off) == 0 && off == 99;
        } else {
            off = -999;
        }
        st_report(verbose, "band-after", ok, 97 + 2, off);
        rguard_region_free(&r);
    }

    /* 6. A renderer that reads its pre-fill at one pixel must be exposed by
     *    the two-sentinel differential, at exactly that pixel offset. */
    {
        enum { N = 128, PLANT = 37 };
        unsigned char feed_a[N], feed_b[N], out_a[N], out_b[N];
        rguard_fill(feed_a, N, RGUARD_SENTINEL_A);
        rguard_fill(feed_b, N, RGUARD_SENTINEL_B);
        buggy_kernel(feed_a, out_a, N, PLANT);
        buggy_kernel(feed_b, out_b, N, PLANT);
        size_t off = 0;
        size_t d = rguard_diff(out_a, out_b, N, &off);
        st_report(verbose, "diff-uninit-read", d == 1 && off == PLANT,
                  PLANT, (long)off);
    }

    /* 7. A correct, fill-independent renderer must produce no difference. */
    {
        enum { N = 128 };
        unsigned char a[N], b[N];
        for (size_t i = 0; i < N; ++i) {
            a[i] = (unsigned char)(i * 7u);
            b[i] = (unsigned char)(i * 7u);
        }
        size_t off = 0;
        size_t d = rguard_diff(a, b, N, &off);
        st_report(verbose, "diff-clean", d == 0, 0, (long)d);
    }

    if (verbose)
        printf("  selftest: %d/%d checks passed\n", st_run - st_fail, st_run);
    return st_fail ? -1 : 0;
}
