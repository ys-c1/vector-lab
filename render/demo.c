#include "src/scene.h"
#include "src/pipeline.h"
#include <math.h>
#include <stdio.h>

#define DPI 3.14159265358979323846

int main(void) {
    RPoint sq[4] = {{2,3},{12,3},{12,13},{2,13}};
    RScene s;
    scene_init(&s);
    if (scene_add_poly(&s, sq, 4) != 0) return 1;
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 1;
    int w = 16, h = 16;
    RPlace place = {0, 0, 0, 1.0};
    RPoint corner[4] = {{2,3},{12,3},{12,13},{2,13}};
    /* 给四个角点各定期望转角, 换算出目标像素; -4.538 会触发多圈归一化 */
    double want[4] = {0.5, 1.7, 3.5, -4.538};
    RPoint tgt[4];
    for (int k = 0; k < 4; ++k) {
        double co = cos(want[k]), si = sin(want[k]);
        double ux = corner[k].x * co - corner[k].y * si;
        double uy = corner[k].x * si + corner[k].y * co;
        tgt[k].x = (ux - win.minx) / (win.maxx - win.minx) * w - 0.5;
        tgt[k].y = (uy - win.miny) / (win.maxy - win.miny) * h - 0.5;
    }
    RCorner so[4];
    int rc = pipeline_solve_rot(&s, place, win, w, h, tgt, so);
    printf("solve rc=%d\n", rc);
    if (rc != 0) return 1;
    for (int k = 0; k < 4; ++k) {
        printf("corner %d target_px(%6.2f,%6.2f) angle %+.6f rad (%+8.2f deg) dev %.3g px cost %.1f us",
               k, tgt[k].x, tgt[k].y, so[k].angle, so[k].angle * 180.0 / DPI,
               so[k].dev, so[k].usec);
        if (so[k].turns != 0)
            printf(" [normalized: %+ld full turn(s) removed]", so[k].turns);
        printf("\n");
    }
    RPlace zsc = {0, 0, 0, 0.0};
    printf("scale=0 -> rc=%d (bad param: place.scale)\n",
           pipeline_solve_rot(&s, zsc, win, w, h, tgt, so));
    RBox empty = {0, 0, 0, 0};
    printf("empty win -> rc=%d (bad param: win)\n",
           pipeline_solve_rot(&s, place, empty, w, h, tgt, so));
    return 0;
}
