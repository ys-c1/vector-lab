#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int box_ok(VBox b) { return b.maxx > b.minx && b.maxy > b.miny; }

int main(void) {
    VPath p;
    VDiag d;

    /* --- parsing and subpath bookkeeping --- */
    CK(svg_parse_diag("M0,0 L10,0 L10,10 Z", &p, &d) == 0);
    CK(d.code == SV_DIAG_NONE && p.count == 2 && p.nsubs == 1);
    CK(p.sub_first[0] == 0 && p.sub_nseg[0] == 2 && p.sub_closed[0] == 1);
    CK(p.segs[0].sub == 0);

    CK(svg_parse_diag("M0,0 L10,0 L10,10 Z M20,0 L30,0 L30,10 Z", &p, &d) == 0);
    CK(p.nsubs == 2 && p.sub_nseg[0] == 2 && p.sub_nseg[1] == 2);
    CK(p.segs[2].sub == 1 && p.sub_first[1] == 2);

    /* implicit lineto after M, relative commands, multiple pairs per command */
    CK(svg_parse_diag("m 0 0 10 0 10 10 z", &p, &d) == 0);
    CK(p.nsubs == 1 && p.sub_nseg[0] == 2);

    /* --- rejection diagnostics point at the offending subpath --- */
    CK(svg_parse_diag("M0,0 Z", &p, &d) == -6);
    CK(d.code == SV_DIAG_EMPTY_SUB && d.sub == 1);
    CK(svg_parse_diag("M0,0 L10,0 L10,10", &p, &d) == -7);
    CK(d.code == SV_DIAG_OPEN_SUB && d.sub == 1);
    CK(svg_parse_diag("M0,0 L10,0 L10,10 Z M20,20 L30,20 L30,30", &p, &d) == -7);
    CK(d.code == SV_DIAG_OPEN_SUB && d.sub == 2);
    CK(svg_parse_diag("M0,0 L10,0 L10,10 Z M20,20 Z", &p, &d) == -6);
    CK(d.code == SV_DIAG_EMPTY_SUB && d.sub == 2);
    CK(svg_parse_diag("", &p, &d) == -2);
    CK(d.code == SV_DIAG_EMPTY_PATH);
    CK(svg_parse_diag("L0,0", &p, &d) == -2);
    CK(d.code == SV_DIAG_SYNTAX);

    /* --- winding + derived rule on the four canonical shapes --- */
    /* single CCW square: winding +1 inside, rule nonzero */
    CK(svg_parse("M0,0 L10,0 L10,10 L0,10 Z", &p) == 0);
    CK(svg_winding(&p, 5, 5, 0) == 1);
    CK(svg_winding(&p, 15, 5, 0) == 0);
    CK(svg_rule(&p) == SV_FILL_NONZERO);

    /* two disjoint CCW squares: nonzero keeps both */
    CK(svg_parse("M0,0 L10,0 L10,10 L0,10 Z M20,0 L30,0 L30,10 L20,10 Z", &p) == 0);
    CK(svg_rule_signed_sum(&p) == 2);
    CK(svg_rule(&p) == SV_FILL_NONZERO);
    int contrib[SV_MAX_SUBS];
    CK(svg_winding(&p, 5, 5, contrib) == 1 && contrib[0] == 1 && contrib[1] == 0);
    CK(svg_winding(&p, 25, 5, contrib) == 1 && contrib[0] == 0 && contrib[1] == 1);

    /* nested CCW + CW: hole, sum cancels to evenodd */
    CK(svg_parse("M0,0 L30,0 L30,30 L0,30 Z M10,10 L10,20 L20,20 L20,10 Z", &p) == 0);
    CK(svg_sub_signed_area(&p, 0) > 0 && svg_sub_signed_area(&p, 1) < 0);
    CK(svg_rule_signed_sum(&p) == 0);
    CK(svg_rule(&p) == SV_FILL_EVENODD);
    CK(svg_winding(&p, 5, 5, 0) == 1);       /* ring */
    CK(svg_winding(&p, 15, 15, 0) == 0);     /* hole */

    /* nested same orientation: winding +2, nonzero and evenodd agree */
    CK(svg_parse("M0,0 L30,0 L30,30 L0,30 Z M10,10 L20,10 L20,20 L10,20 Z", &p) == 0);
    CK(svg_rule_signed_sum(&p) == 2);
    CK(svg_rule(&p) == SV_FILL_NONZERO);
    CK(svg_winding(&p, 15, 15, 0) == 2);
    CK(svg_sample_filled(SV_FILL_NONZERO, 2) == 1);
    CK(svg_sample_filled(SV_FILL_EVENODD, 2) == 0);

    /* star: nested winding levels 1 and 2, self intersections nonzero */
    CK(svg_parse("M50,5 L76.45,86.41 L7.20,36.09 L92.80,36.09 L23.55,86.41 Z", &p) == 0);
    CK(svg_self_intersections(&p, 0) == 5);
    CK(svg_rule(&p) == SV_FILL_NONZERO);
    CK(svg_winding(&p, 50, 50, 0) == 2);

    /* --- raster: rule actually changes the image --- */
    CK(svg_parse("M0,0 L30,0 L30,30 L0,30 Z M10,10 L20,10 L20,20 L10,20 Z", &p) == 0);
    VBox b;
    CK(svg_bbox(&p, &b) == 0 && box_ok(b));
    unsigned char buf_a[32 * 32], buf_b[32 * 32];
    CK(svg_raster(&p, b, 32, 32, 8, SV_FILL_NONZERO, buf_a) == 0);
    CK(svg_raster(&p, b, 32, 32, 8, SV_FILL_EVENODD, buf_b) == 0);
    long diff = 0;
    for (int i = 0; i < 32 * 32; ++i) if (buf_a[i] != buf_b[i]) ++diff;
    CK(diff > 0);
    CK(svg_raster(&p, b, 32, 32, 8, SV_FILL_AUTO, buf_a) == 0);

    /* area sanity: unit square area ~ 1 under either rule */
    CK(svg_parse("M0,0 L1,0 L1,1 L0,1 Z", &p) == 0);
    VBox ub = {0, 0, 1, 1};
    CK(fabs(svg_area(&p, ub, SV_FILL_NONZERO, 512) - 1.0) < 0.01);

    /* raster refuses unclosed input */
    VPath bad;
    CK(svg_parse("M0,0 L10,0 L10,10 Z M0,0 L5,5 L10,0 Z", &bad) == 0);
    bad.sub_closed[1] = 0;
    CK(svg_raster(&bad, b, 8, 8, 1, SV_FILL_AUTO, buf_a) == -3);

    /* legacy svg_fill still works over the 20x20 viewport */
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &p) == 0);
    unsigned char *legacy = (unsigned char*)malloc(20 * 20);
    CK(legacy != 0);
    CK(svg_fill(&p, 20, 20, legacy) == 0);
    int on = 0;
    for (int i = 0; i < 400; ++i) if (legacy[i]) ++on;
    CK(on > 40 && on < 60);
    free(legacy);

    CK(svg_parse("L0,0", &p) == -2);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    CK(svg_parse("M0,0 L", &p) == -4);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
