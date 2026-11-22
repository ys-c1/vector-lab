#ifndef SVGPATH_H
#define SVGPATH_H

#define SV_MAX_SEGS 256
#define SV_MAX_SUBS 64

typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
/* sub is the 0-based subpath index the segment belongs to. */
typedef struct { VSegKind kind; VPoint a, b; int sub; } VSeg;

typedef struct {
    int count;                       /* total segments */
    VSeg segs[SV_MAX_SEGS];
    int nsubs;                       /* finalized subpaths */
    int sub_first[SV_MAX_SUBS];      /* first segment of each subpath */
    int sub_nseg[SV_MAX_SUBS];       /* segments in each subpath */
    int sub_closed[SV_MAX_SUBS];     /* 1 when terminated by Z/z */
} VPath;

/* AUTO picks NONZERO or EVENODD from the sign sum of the subpath
 * signed areas; see svg_rule(). */
typedef enum {
    SV_FILL_AUTO = 0,
    SV_FILL_NONZERO = 1,
    SV_FILL_EVENODD = 2
} VFillRule;

typedef enum {
    SV_DIAG_NONE = 0,
    SV_DIAG_EMPTY_PATH = 1,
    SV_DIAG_EMPTY_SUB = 2,
    SV_DIAG_OPEN_SUB = 3,
    SV_DIAG_SYNTAX = 4,
    SV_DIAG_OVERFLOW = 5
} VDiagCode;

/* diag.code == SV_DIAG_NONE on success.  diag.sub is the 1-based
 * number of the offending subpath (0 when the path itself is empty). */
typedef struct { VDiagCode code; int sub; int pos; } VDiag;

typedef struct { double minx, miny, maxx, maxy; } VBox;

int svg_parse(const char *d, VPath *out);
int svg_parse_diag(const char *d, VPath *out, VDiag *diag);

int svg_bbox(const VPath *p, VBox *out);

/* Signed winding number of one subpath / of the whole path at (x, y).
 * contrib, when non-NULL, receives one entry per subpath (SV_MAX_SUBS
 * entries) so callers can see how each subpath contributes. */
int svg_winding_sub(const VPath *p, int sub, double x, double y);
int svg_winding(const VPath *p, double x, double y, int *contrib);

/* Shoelace signed area of one closed subpath. */
double svg_sub_signed_area(const VPath *p, int sub);

/* Number of proper self crossings inside one subpath. */
int svg_self_intersections(const VPath *p, int sub);

/* Sum of the signs of the subpath signed areas, and the derived fill
 * rule: NONZERO when every non-degenerate subpath has the same
 * orientation (|sum| == count), EVENODD otherwise (cancelling or
 * mixed orientations). */
int svg_rule_signed_sum(const VPath *p);
int svg_rule(const VPath *p);

/* Classify a sample by winding under a concrete rule. */
int svg_sample_filled(int rule, int winding);

/* Approximate area over box: samples x samples grid, cell centres. */
double svg_area(const VPath *p, VBox box, int rule, int samples);

/* Render box to w x h, ss x ss subsamples per pixel, pixel on when at
 * least half of its subsamples are filled.  rule may be AUTO. */
int svg_raster(const VPath *p, VBox box, int w, int h, int ss, int rule,
               unsigned char *buf);

/* Legacy entry point: pixel centres in [0,w]x[0,h], auto rule. */
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);

int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);
#endif
