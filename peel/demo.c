#include "peel.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    /* 三层同心方环：外 10、中 6、内 2，每层 4 点 */
    PPoint p[12] = {
        {0, 0}, {10, 0}, {10, 10}, {0, 10},
        {2, 2}, {8, 2}, {8, 8}, {2, 8},
        {4, 4}, {6, 4}, {6, 6}, {4, 6},
    };
    PLayer L[PEEL_MAX_LAYERS];
    PSummary s;
    PPeelOut out;
    memset(&out, 0, sizeof out);
    out.layers = L;
    out.layer_cap = PEEL_MAX_LAYERS;

    if (peel_run(p, 12, &out, &s) != 0) return 1;
    printf("points %d unique %d layers %d\n", s.input_points, s.unique_points, s.layer_count);
    for (int i = 0; i < s.layer_count; ++i)
        printf("  L%d verts %d removed %d area %.2f\n",
               L[i].layer, L[i].hull_verts, L[i].removed, L[i].area);

    double ratio = 1.0;
    int k = peel_cover_layer(L, s.layer_count, 0.95, &ratio);
    if (k > 0) printf("cover 95%% at layer %d (rest %.2f%%)\n", k, 100.0 * ratio);
    return 0;
}
