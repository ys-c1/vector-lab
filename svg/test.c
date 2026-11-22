#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int same_path(const VPath *a, const VPath *b) {
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; ++i)
        if (a->segs[i].a.x != b->segs[i].a.x || a->segs[i].a.y != b->segs[i].a.y
         || a->segs[i].b.x != b->segs[i].b.x || a->segs[i].b.y != b->segs[i].b.y) return 0;
    return 1;
}

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
    CK(svg_parse("M0,0 S1,1 2,2", &p) == -3); /* 旧接口不受影响 */

#if SVG_SMOOTH
    /* 平滑段：反射控制点与逐段手写控制点表比对 */
    VPath a, b; VSeams sm; int eseg = -1;
    CK(svg_parse2("M0,0 C10,0 20,10 20,20 S30,40 40,40", &a, 8, &eseg, &sm) == 0);
    CK(a.count == 16);
    CK(sm.count == 1 && sm.at[0] == 8);
    /* 手写：c1 = 2*(20,20)-(20,10) = (20,30) */
    CK(svg_parse2("M0,0 C10,0 20,10 20,20 C20,30 30,40 40,40", &b, 8, 0, 0) == 0);
    CK(same_path(&a, &b));
    CK(svg_parse2("M0,0 Q10,0 20,10 T40,10", &a, 8, 0, &sm) == 0);
    CK(a.count == 16 && sm.count == 1 && sm.at[0] == 8);
    /* 手写：q1 = 2*(20,10)-(10,0) = (30,20) */
    CK(svg_parse2("M0,0 Q10,0 20,10 Q30,20 40,10", &b, 8, 0, 0) == 0);
    CK(same_path(&a, &b));
    /* 相对命令 s/t 走同一反射 */
    CK(svg_parse2("M0,0 c10,0 20,10 20,20 s10,20 20,20", &a, 8, 0, 0) == 0);
    CK(svg_parse2("M0,0 C10,0 20,10 20,20 C20,30 30,40 40,40", &b, 8, 0, 0) == 0);
    CK(same_path(&a, &b));
    CK(svg_parse2("M0,0 q10,0 20,10 t20,0", &a, 8, 0, 0) == 0);
    CK(svg_parse2("M0,0 Q10,0 20,10 Q30,20 40,10", &b, 8, 0, 0) == 0);
    CK(same_path(&a, &b));
    /* 两档细分段数 */
    CK(svg_parse2("M0,0 C10,0 20,10 20,20 S30,40 40,40", &a, 32, 0, &sm) == 0);
    CK(a.count == 64 && sm.at[0] == 32);
    /* 前一段不是匹配曲线：拒绝并给出段号 */
    eseg = -1;
    CK(svg_parse2("M0,0 L10,0 S20,0 30,0", &a, 8, &eseg, 0) == -6);
    CK(eseg == 1);
    eseg = -1;
    CK(svg_parse2("M0,0 T10,10", &a, 8, &eseg, 0) == -6);
    CK(eseg == 0);
    eseg = -1;
    CK(svg_parse2("M0,0 Q10,0 20,10 S30,30 40,40", &a, 8, &eseg, 0) == -6);
    CK(eseg == 1);
    eseg = -1;
    CK(svg_parse2("M0,0 C10,0 20,10 20,20 T40,40", &a, 8, &eseg, 0) == -6);
    CK(eseg == 1);
    /* 平滑路径填图可走同一 svg_fill */
    CK(svg_parse2("M2,2 C8,2 18,8 18,10 S8,18 2,18 Z", &a, 8, 0, 0) == 0);
    CK(svg_fill(&a, 20, 20, buf) == 0);
#else
    /* 关掉新分支：svg_parse2 与 svg_parse 行为一致 */
    VPath a, b;
    CK(svg_parse2("M0,0 L10,0 L10,10 Z", &a, 8, 0, 0) == 0);
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &b) == 0);
    CK(same_path(&a, &b));
    CK(svg_parse2("M0,0 C1,2 3,4 5,6", &a, 8, 0, 0) == -3);
    CK(svg_parse2("M0,0 S1,1 2,2", &a, 8, 0, 0) == -3);
#endif
    free(buf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
