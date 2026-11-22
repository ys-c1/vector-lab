#ifndef SVGPATH_H
#define SVGPATH_H
typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;

/* Extra errors reported by svg_parse_ex() on top of the legacy -1..-5. */
#define SVG_ERR_CTRL_COUNT (-6) /* C/c group does not have exactly 3 pairs */
#define SVG_ERR_NOT_FINITE (-7) /* NaN or infinity in a coordinate */
#define SVG_ERR_TOL (-8)        /* cubic on, but tol is not finite and > 0 */

/*
 * Cubic support is opt-in so legacy output never moves.  With
 * enable_cubic == 0 svg_parse_ex() is exactly the old svg_parse(): C/c is
 * rejected with -3 and nothing extra is validated.
 *
 * With enable_cubic != 0 each C/c takes exactly one group of three coordinate
 * pairs (control 1, control 2, endpoint).  A group is adaptively subdivided
 * until both control points are within `tol` of the chord, then appended as
 * SV_LINE segments.  Any other number of coordinate pairs is a control-count
 * error; repeated groups are not accepted (unlike SVG shorthand repetition).
 * `tol` is in the path's user units and must be finite and > 0, otherwise
 * SVG_ERR_TOL is returned.
 *
 * *err_seg, when non-NULL, receives the 1-based number of the drawing segment
 * at fault.  L/l, each C/c letter and Z/z take one number each; M/m is a move
 * and takes no number, so an error inside M/m reports the number the next
 * drawing segment would get.  0 means the error is not tied to a command.
 */
typedef struct {
    int enable_cubic;
    double tol;
} VParseOpt;

int svg_parse(const char *d, VPath *out);
int svg_parse_ex(const char *d, VPath *out, const VParseOpt *opt, int *err_seg);
const char *svg_strerror(int code);
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);
#endif
