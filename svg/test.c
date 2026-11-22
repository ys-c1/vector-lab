#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    VPath p;
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &p) == 0);
    CK(p.count == 3);
    unsigned char *buf = (unsigned char*)malloc(20 * 20);
    CK(svg_fill(&p, 20, 20, buf) == 0);
    int on = 0;
    for (int i = 0; i < 400; ++i) if (buf[i]) ++on;
    CK(on > 40 && on < 60);
    CK(svg_parse("L0,0", &p) == -2);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    CK(svg_parse("M0,0 L", &p) == -4);
#if SVG_REL
    int es = 0;
    /* 相对/绝对混用：h/v/l 按当前笔位累加 */
    CK(svg_parse_ex("M1,1 h9 v8 l-9,0 Z", &p, &es) == 0);
    CK(p.count == 4);
    CK(fabs(p.segs[0].b.x - 10) < 1e-9 && fabs(p.segs[0].b.y - 1) < 1e-9);
    CK(fabs(p.segs[1].b.x - 10) < 1e-9 && fabs(p.segs[1].b.y - 9) < 1e-9);
    CK(fabs(p.segs[2].b.x - 1) < 1e-9 && fabs(p.segs[2].b.y - 9) < 1e-9);
    /* 与手写绝对版本逐点一致 */
    VPath q;
    CK(svg_parse_ex("M1,1 L10,1 L10,9 L1,9 Z", &q, 0) == 0);
    CK(q.count == p.count);
    double dev = 0;
    for (int i = 0; i < p.count; ++i) {
        double d = fabs(p.segs[i].b.x - q.segs[i].b.x);
        if (d > dev) dev = d;
        d = fabs(p.segs[i].b.y - q.segs[i].b.y);
        if (d > dev) dev = d;
    }
    CK(dev == 0.0);
    /* 相对量缺失 / 非法指令：报错并指出段号 */
    CK(svg_parse_ex("M0,0 L5,5 h", &p, &es) == -4 && es == 3);
    CK(svg_parse_ex("M0,0 l5", &p, &es) == -4 && es == 2);
    CK(svg_parse_ex("M0,0 v", &p, &es) == -4 && es == 2);
    CK(svg_parse_ex("M0,0 q1,1", &p, &es) == -3 && es == 2);
    CK(svg_parse_ex("L0,0", &p, &es) == -2 && es == 1);
    /* 第二个子路径的 Z 闭合到自己的起点，而非首个子路径起点 */
    CK(svg_parse_ex("M0,0 L4,0 Z M10,10 L14,10 Z", &p, &es) == 0);
    CK(p.count == 4);
    CK(fabs(p.segs[3].b.x - 10) < 1e-9 && fabs(p.segs[3].b.y - 10) < 1e-9);
#endif
    free(buf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
