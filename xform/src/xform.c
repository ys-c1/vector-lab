#include "xform.h"

XForm xform_identity(void) {
    XForm m = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    return m;
}

XPoint xform_apply(XForm m, XPoint p) {
    XPoint r;
    r.x = m.a * p.x + m.c * p.y + m.e;
    r.y = m.b * p.x + m.d * p.y + m.f;
    return r;
}

XForm xform_mul(XForm m, XForm n) {
    XForm r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.e = m.a * n.e + m.c * n.f + m.e;
    r.f = m.b * n.e + m.d * n.f + m.f;
    return r;
}

const char *xform_batch_inplace(XForm m, XPoint *pts, size_t n, size_t chunk) {
    if (!pts) return "pts";
    if (n == 0) return "n";
    if (chunk == 0) return "chunk";
    /* 块宽向上取整到缓存行倍数，一块一块在 pts 上就地写回。 */
    size_t per = XFORM_POINTS_PER_LINE;
    size_t blk = (chunk + per - 1) / per * per;
    for (size_t base = 0; base < n; base += blk) {
        size_t hi = base + blk;
        if (hi > n) hi = n;
        for (size_t i = base; i < hi; ++i)
            pts[i] = xform_apply(m, pts[i]);
    }
    return NULL;
}
