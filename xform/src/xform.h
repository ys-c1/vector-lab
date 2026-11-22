#ifndef XFORM_H
#define XFORM_H
typedef struct { double a, b, c, d, e, f; } XForm;
typedef struct { double x, y; } XPoint;
XForm xform_identity(void);
XPoint xform_apply(XForm m, XPoint p);
XForm xform_mul(XForm m, XForm n);
#endif
