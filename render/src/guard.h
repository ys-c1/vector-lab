#ifndef RGUARD_H
#define RGUARD_H

#include <stddef.h>

/* Sentinel byte values used to pre-fill buffers before rendering.  Two
 * distinct values are used so that a buffer whose contents actually reach
 * the output can be distinguished: if the exported image depends on which
 * sentinel was in memory, the renderer read uninitialised memory. */
#define RGUARD_SENTINEL_A 0xA5u
#define RGUARD_SENTINEL_B 0x5Au

typedef struct {
    unsigned long long fill_ops;   /* number of sentinel fill operations  */
    unsigned long long fill_bytes; /* number of bytes filled with sentinel */
} RGuardStats;

/* Sentinel fill is on by default.  When disabled, buffers are zero-filled
 * exactly as before the guard was introduced, so the normal path stays
 * byte-identical.  The sentinel value is configurable so a harness can run
 * the same scene twice with two different pre-fill values. */
void rguard_configure(int enabled, unsigned char sentinel);
int rguard_enabled(void);
unsigned char rguard_sentinel_value(void);

/* Fill a working buffer according to the current configuration and count the
 * operation.  Zero-fill when disabled (not counted as a sentinel fill). */
void rguard_buffer(void *p, size_t n);

/* Counters are process-wide and reset explicitly by the caller. */
void rguard_reset(void);
const RGuardStats *rguard_stats(void);

/* Fill n bytes at p with v, counting the operation. */
void rguard_fill(void *p, size_t n, unsigned char v);

/* Look for bytes equal to v (i.e. sentinel bytes that were never written).
 * Returns 1 if any were found, 0 otherwise.  *first_off receives the lowest
 * matching offset and *count the number of matches. */
int rguard_scan(const void *p, size_t n, unsigned char v,
                size_t *first_off, size_t *count);

/* Look for bytes NOT equal to v (i.e. sentinel bytes that were modified).
 * Same return/offset contract as rguard_scan. */
int rguard_scan_not(const void *p, size_t n, unsigned char v,
                    size_t *first_off, size_t *count);

/* Compare two equally sized buffers.  Returns the number of differing bytes;
 * *first_off receives the first differing offset (0 when equal). */
size_t rguard_diff(const unsigned char *a, const unsigned char *b, size_t n,
                   size_t *first_off);

/* A payload surrounded by sentinel bands.  The payload itself is also
 * sentinel-filled at init.  Writes into either band are out-of-bounds writes
 * and are reported by rguard_region_check with an offset relative to the
 * payload: -1 is the byte immediately before it, payload_n the byte
 * immediately after it. */
typedef struct {
    unsigned char *base;
    unsigned char *payload;
    size_t payload_n;
    size_t band;
    unsigned char sentinel;
} RGuardRegion;

int rguard_region_init(RGuardRegion *r, size_t payload_n, size_t band,
                       unsigned char sentinel);
void rguard_region_free(RGuardRegion *r);

/* Returns 1 if both bands are intact, 0 if either was modified.  On failure
 * *offset receives the relative offset described above. */
int rguard_region_check(const RGuardRegion *r, long *offset);

/* Validate the detector itself: plant known sentinel modifications and
 * out-of-bounds writes and require the detectors to report exactly the
 * expected offsets.  Returns 0 when every planted fault was detected at the
 * right offset, -1 otherwise (a detector that cannot detect is a failure). */
int rguard_selftest(int verbose);

/* Fault injection for acceptance testing.  When a non-negative pixel index
 * is set, the rasteriser deliberately skips writing that pixel, so it keeps
 * its sentinel pre-fill.  This lets an end-to-end test prove the detectors
 * catch a real uninitialised read at a known offset.  -1 disables. */
void rguard_inject_skip_pixel(int index);
int rguard_injected_skip_pixel(void);

#endif
