#include "src/atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 用法:
 *   atlas_cli <tiles.txt> <W> <H> <input|smart>
 * 数据每行 "count w h"，# 开头为注释；编号按读取顺序分配。
 * 输出: 状态行 + 每个图块的放置 + 浪费率，重定向即可得到确定性文本产物。
 */
int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s tiles.txt W H input|smart\n", argv[0]);
        return 1;
    }
    int W = atoi(argv[2]), H = atoi(argv[3]);
    int smart;
    if (!strcmp(argv[4], "input")) smart = ATLAS_ORDER_INPUT;
    else if (!strcmp(argv[4], "smart")) smart = ATLAS_ORDER_SMART;
    else { fprintf(stderr, "order must be input or smart\n"); return 1; }

    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open tiles failed\n"); return 2; }

    int cap = 16, n = 0;
    ATile *tiles = (ATile *)malloc(sizeof(ATile) * (size_t)cap);
    if (!tiles) { fclose(f); return 3; }
    int count, w, h, id = 0, rc = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%d %d %d", &count, &w, &h) != 3) {
            fprintf(stderr, "bad line: %s", line); rc = 4; break;
        }
        for (int k = 0; k < count; ++k) {
            if (n == cap) {
                cap *= 2;
                ATile *nt = (ATile *)realloc(tiles, sizeof(ATile) * (size_t)cap);
                if (!nt) { rc = 3; break; }
                tiles = nt;
            }
            tiles[n++] = (ATile){id++, w, h};
        }
        if (rc) break;
    }
    fclose(f);
    if (rc) { free(tiles); return rc; }

    AResult r;
    rc = atlas_pack(tiles, n, W, H, smart, &r);
    if (rc == ATLAS_E_TOO_LARGE) {
        fprintf(stderr,
                "REJECT: tile id=%d (in input %s) larger than atlas %dx%d "
                "in either orientation; packing aborted, no placement emitted\n",
                r.reject_id, argv[1], W, H);
        free(tiles);
        atlas_result_free(&r);
        return 10;
    }
    if (rc != ATLAS_OK) { fprintf(stderr, "pack failed rc=%d\n", rc); free(tiles); return rc; }

    printf("order=%s atlas=%dx%d tiles=%d bins=%d\n",
           smart == ATLAS_ORDER_SMART ? "smart" : "input", W, H, n, r.bins);
    printf("seq id bin x y w h rotated\n");
    for (int i = 0; i < r.count; ++i) {
        const APlace *p = &r.items[i];
        printf("%3d %3d %3d %3d %3d %3d %3d %d\n",
               i, p->id, p->bin, p->x, p->y, p->w, p->h, p->rotated);
    }
    printf("waste=%.6f\n", atlas_waste(&r));

    free(tiles);
    atlas_result_free(&r);
    return 0;
}
