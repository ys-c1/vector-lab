#include "atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 用法: atlas_cli <tiles.txt> <max_waste> [--json]
 * tiles.txt 每行 "w h"，空行与 # 开头的行忽略。 */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s tiles.txt max_waste [--json]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open tiles failed\n"); return 2; }
    Tiles t;
    t.n = 0;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;
        int w, h;
        if (sscanf(p, "%d %d", &w, &h) != 2) { fclose(f); return 3; }
        if (t.n >= ATLAS_MAX_TILES) { fclose(f); fprintf(stderr, "too many tiles\n"); return 4; }
        t.t[t.n].w = w;
        t.t[t.n].h = h;
        t.n++;
    }
    fclose(f);

    double limit = atof(argv[2]);
    int json = (argc > 3 && strcmp(argv[3], "--json") == 0);
    AtlasResult r;
    int rc = atlas_solve(&t, limit, &r);

    if (json) {
        printf("{\"code\":\"%s\",\"tiles\":%d,\"tile_area\":%lld,"
               "\"side_raw\":%d,\"side\":%d,\"pow2_rounded\":%s,"
               "\"waste\":%.6f,\"waste_raw\":%.6f,\"limit\":%.6f,"
               "\"pass\":%s,\"pack_ms\":%.4f,\"solve_ms\":%.4f}\n",
               rc == ATLAS_OK ? "ok" : rc == ATLAS_EWASTE ? "ewaste" :
               rc == ATLAS_EINPUT ? "einput" : rc == ATLAS_ETOOBIG ? "etoobig" :
               rc == ATLAS_EINFEASIBLE ? "einfeasible" : "error",
               t.n, r.tile_area, r.side_raw, r.side,
               r.pow2_rounded ? "true" : "false",
               r.waste, r.waste_raw, r.limit,
               r.pass ? "true" : "false", r.pack_ms, r.ms);
    } else {
        printf("tiles %d tile_area %lld\n", t.n, r.tile_area);
        printf("side_raw %d side %d%s\n", r.side_raw, r.side,
               r.pow2_rounded ? " (rounded up to power of two)" : "");
        printf("waste %.4f limit %.4f %s\n", r.waste, r.limit,
               r.pass ? "PASS" : "FAIL");
        printf("pack_ms %.4f solve_ms %.4f\n", r.pack_ms, r.ms);
    }
    return rc == ATLAS_OK ? 0 : (rc == ATLAS_EWASTE ? 5 : 6);
}
