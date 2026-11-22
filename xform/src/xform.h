#ifndef XFORM_H
#define XFORM_H
typedef struct { double a, b, c, d, e, f; } XForm;
typedef struct { double x, y; } XPoint;

/* |det| below this is treated as a zero scale (singular transform). */
#define XFORM_DET_EPS 1e-12

XForm xform_identity(void);
XPoint xform_apply(XForm m, XPoint p);
XForm xform_mul(XForm m, XForm n);
double xform_det(XForm m);
/* Normal transform: (ox,oy) = M^-T * (nx,ny), un-normalised.
   Returns 0 on success, -1 on null output, -2 if |det| < XFORM_DET_EPS. */
int xform_inverse_transpose(XForm m, double nx, double ny, double *ox, double *oy);
#endif
