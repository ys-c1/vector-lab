#include "atlas.h"
#include <stdio.h>
#include <stdlib.h>

static const char *err_name(int code) {
    switch (code) {
    case ATLAS_OK: return "ok";
    case ATLAS_EINPUT: return "EINPUT";
    case ATLAS_ETOOBIG: return "ETOOBIG";
    case ATLAS_EINFEASIBLE: return "EINFEASIBLE";
    case ATLAS_ENOMEM: return "ENOMEM";
    case ATLAS_EWASTE: return "EWASTE";
    default: return "?";
    }
}

static void report(const char *name, const Tiles *t, double limit) {
    AtlasResult r;
    int rc = atlas_solve(t, limit, &r);
    printf("== %s ==  tiles %d  tile_area %lld  limit %.2f%%  -> %s\n",
           name, t->n, r.tile_area, limit * 100.0, err_name(rc));
    if (rc == ATLAS_EINPUT) {
        printf("   reject: empty atlas / non-positive tile size / limit out of [0,1]\n\n");
        return;
    }
    if (rc == ATLAS_ETOOBIG) {
        printf("   reject: a tile is larger than the %d px side cap\n\n", ATLAS_MAX_SIDE);
        return;
    }
    if (rc == ATLAS_EINFEASIBLE) {
        printf("   reject: no side up to the cap packs this set\n\n");
        return;
    }
    printf("   side: raw %d -> %d%s\n", r.side_raw, r.side,
           r.pow2_rounded ? "  (not a power of two, rounded up)" : "  (power of two already)");
    printf("   waste: %.2f%%  [%s]   packing %.3f ms   total solve %.3f ms\n",
           r.waste * 100.0, r.waste <= limit ? "PASS" : "FAIL: over limit",
           r.pack_ms, r.ms);
    if (r.pow2_rounded)
        printf("   rounding cost: %.2f%% -> %.2f%% (raw side would have used %.2f%%)\n",
               r.waste_raw * 100.0, r.waste * 100.0, r.waste_raw * 100.0);
    printf("\n");
}

int main(int argc, char **argv) {
    double limit = argc > 1 ? atof(argv[1]) : 0.35;

    Tiles t;
    atlas_sample_icons(&t);   report("icons", &t, limit);
    atlas_sample_mixed(&t);   report("mixed", &t, limit);
    atlas_sample_strips(&t);  report("strips", &t, limit);
    atlas_sample_uniform(&t); report("uniform", &t, limit);

    /* 拒绝路径。 */
    Tiles empty = {0, {{0, 0}}};
    report("empty", &empty, limit);

    Tiles big = {1, {{ATLAS_MAX_SIDE + 1, 4}}};
    report("tile wider than side cap", &big, limit);

    /* 单块正好占满边界。 */
    Tiles one = {1, {{256, 256}}};
    report("single 256x256", &one, 0.0);

    return 0;
}
