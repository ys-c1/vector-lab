#include "order.h"
#include <stddef.h>

const char *clip_order_err(int code) {
    switch (code) {
    case CLIP_ORD_OK: return "ok";
    case CLIP_ORD_EMPTY: return "窗口为空：边数不足 3 或面积为零";
    case CLIP_ORD_COINCIDENT: return "边重合：存在零长边或共线边";
    case CLIP_ORD_TOOMANY: return "边数超过枚举上限 8";
    default: return "未知错误";
    }
}

static double signed_area2(const CPoint *w, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = w[i], b = w[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s;
}

/* 两条边共线（方向平行且同在一直线上）即视为重合 */
static int edges_coincide(CPoint a, CPoint b, CPoint c, CPoint d) {
    double ux = b.x - a.x, uy = b.y - a.y;
    double vx = d.x - c.x, vy = d.y - c.y;
    if (ux * vy - uy * vx != 0.0) return 0;
    if (ux * (c.y - a.y) - uy * (c.x - a.x) != 0.0) return 0;
    return 1;
}

int clip_check_window(const CPoint *win, int wn) {
    if (!win || wn < 3) return CLIP_ORD_EMPTY;
    if (signed_area2(win, wn) == 0.0) return CLIP_ORD_EMPTY;
    for (int i = 0; i < wn; ++i) {
        CPoint a = win[i], b = win[(i + 1) % wn];
        if (a.x == b.x && a.y == b.y) return CLIP_ORD_COINCIDENT;
        for (int j = i + 1; j < wn; ++j) {
            CPoint c = win[j], d = win[(j + 1) % wn];
            if (edges_coincide(a, b, c, d)) return CLIP_ORD_COINCIDENT;
        }
    }
    return CLIP_ORD_OK;
}

/* 字典序下一个排列；已是最后一个时恢复升序并返回 0 */
static int next_perm(int *a, int n) {
    int i = n - 2;
    while (i >= 0 && a[i] > a[i + 1]) --i;
    if (i < 0) return 0;
    int j = n - 1;
    while (a[j] < a[i]) --j;
    int t = a[i]; a[i] = a[j]; a[j] = t;
    for (int l = i + 1, r = n - 1; l < r; ++l, --r) {
        t = a[l]; a[l] = a[r]; a[r] = t;
    }
    return 1;
}

int clip_best_order(const CPoint *poly, int n, const CPoint *win, int wn,
                    int *best_order, CPoly *out) {
    if (!poly || !out || n < 3) return -1;
    int chk = clip_check_window(win, wn);
    if (chk != CLIP_ORD_OK) return chk;
    if (wn > CLIP_ORD_MAX_EDGES) return CLIP_ORD_TOOMANY;
    int ord[CLIP_ORD_MAX_EDGES];
    for (int i = 0; i < wn; ++i) ord[i] = i;
    CPoly best, cur;
    int have = 0;
    do {
        if (clip_convex_ordered(poly, n, win, wn, ord, &cur, NULL) != 0) return -1;
        if (!have || cur.count < best.count) {
            best = cur;
            if (best_order)
                for (int i = 0; i < wn; ++i) best_order[i] = ord[i];
            have = 1;
        }
    } while (next_perm(ord, wn));
    *out = best;
    return 0;
}
