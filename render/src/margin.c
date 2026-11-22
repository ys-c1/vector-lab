#include "margin.h"
#include <math.h>

/* 反解留边的不等式组。
 *
 * 像素精度（世界单位/像素）：
 *   dx = (win.maxx - win.minx) / w
 *   dy = (win.maxy - win.miny) / h
 *
 * 包围盒探出窗口的距离（<= 0 表示未探出）：
 *   左 dl = win.minx - bb.minx    右 dr = bb.maxx - win.maxx
 *   下 db = win.miny - bb.miny    上 dt = bb.maxy - win.maxy
 *
 * 要不切掉场景，每边留边 m（像素）须盖住探出量：
 *   m * dx >= dl   即   m >= dl / dx      （其余三边同理）
 * 最小整数解 m = max(0, ceil(dl / dx - eps))，eps 吸收浮点噪声，
 * 使恰好贴边（dl == 0 或 dl 为 k*dx 的整倍数）不解出多余的一像素。
 *
 * 拒绝条件：任一边留边超过对应画面尺寸的一成（m > w/10 或 m > h/10）。
 */
static int side(double gap, double px) {
    if (gap <= 0.0) return 0;
    int m = (int)ceil(gap / px - 1e-9);
    return m < 0 ? 0 : m;
}

int margin_solve(const RBox *bb, const RBox *win, int w, int h, RMargin *out) {
    if (!bb || !win || !out || w <= 0 || h <= 0) return RMARGIN_BADARG;
    if (!(win->maxx > win->minx) || !(win->maxy > win->miny)) return RMARGIN_BADARG;
    double dx = (win->maxx - win->minx) / w;
    double dy = (win->maxy - win->miny) / h;
    RMargin m;
    m.left   = side(win->minx - bb->minx, dx);
    m.right  = side(bb->maxx - win->maxx, dx);
    m.bottom = side(win->miny - bb->miny, dy);
    m.top    = side(bb->maxy - win->maxy, dy);
    *out = m;
    if (m.left > w / 10 || m.right > w / 10 ||
        m.bottom > h / 10 || m.top > h / 10) return RMARGIN_REJECT;
    return RMARGIN_OK;
}

RBox margin_expand(const RBox *win, int w, int h, RMargin m) {
    double dx = (win->maxx - win->minx) / w;
    double dy = (win->maxy - win->miny) / h;
    RBox r;
    r.minx = win->minx - m.left * dx;
    r.maxx = win->maxx + m.right * dx;
    r.miny = win->miny - m.bottom * dy;
    r.maxy = win->maxy + m.top * dy;
    return r;
}
