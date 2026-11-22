/* 故障注入：真实裁剪实现改名为 clip_convex_impl，本文件包一层，
 * 在第 FAULT_CALL 次调用时轻微缩放输出多边形，用来验证账目能停在
 * 正确的步号上。只用于测试构建（build/ledger_fault），不进产物。 */
#include "clip.h"
#include <math.h>

int clip_convex_impl(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);

#ifndef FAULT_CALL
#define FAULT_CALL 2
#endif
#ifndef FAULT_SCALE
#define FAULT_SCALE 1.0002
#endif

static int calls = 0;

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    int rc = clip_convex_impl(poly, n, win, wn, out);
    ++calls;
    if (rc == 0 && calls == FAULT_CALL && out->count >= 3) {
        double cx = 0.0, cy = 0.0;
        for (int i = 0; i < out->count; ++i) { cx += out->pts[i].x; cy += out->pts[i].y; }
        cx /= out->count;
        cy /= out->count;
        for (int i = 0; i < out->count; ++i) {
            out->pts[i].x = cx + (out->pts[i].x - cx) * FAULT_SCALE;
            out->pts[i].y = cy + (out->pts[i].y - cy) * FAULT_SCALE;
        }
    }
    return rc;
}
