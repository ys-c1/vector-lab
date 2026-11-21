#include "clip.h"
#include "shapes.h"
#include <stdio.h>

int main(void) {
    /* 原有凸窗口演示 */
    CPoint poly[4] = {{0,0},{20,0},{20,10},{0,10}};
    CPoint win[4] = {{5,-5},{15,-5},{15,5},{5,5}};
    CPoly out;
    if (clip_convex(poly,4,win,4,&out) != 0) return 1;
    printf("convex clip: verts=%d area=%.2f\n", out.count, clip_area(&out));

    /* 凹窗口演示：拆分关闭（旧行为，结果错误）vs 拆分打开（正确并集） */
    CPoint uwin[CLIP_MAX_VERTS], cover[4];
    int wn = shape_u_window(uwin);
    int cn = shape_cover(cover);
    CPoly outs[CLIP_MAX_PIECES];
    int nouts = 0, npieces = 0, ep = -1;
    double area = 0;

    if (clip_concave(cover, cn, uwin, wn, 0, outs, CLIP_MAX_PIECES,
                     &nouts, &area, &npieces, &ep) != 0) return 1;
    printf("concave split=OFF: result-polys=%d area=%.2f (wrong: notch kept)\n",
           nouts, area);

    if (clip_concave(cover, cn, uwin, wn, 1, outs, CLIP_MAX_PIECES,
                     &nouts, &area, &npieces, &ep) != 0) return 1;
    printf("concave split=ON : pieces=%d result-polys=%d union-area=%.2f "
           "(hand-computed 72.00)\n", npieces, nouts, area);
    return 0;
}
