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
