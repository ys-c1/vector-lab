/* 交叉验证：旧接口在改动前后逐字节一致。
 *
 * 本目标把 HEAD 版本的 svgpath.c 原样编译进来（符号改名以避免冲突），
 * 与新实现同进程对比：
 *   - svg_parse 对 M/L/Z 路径的输出整表一致；对 A/a 都返回旧错误码 -3；
 *   - svg_fill 对纯线段路径的像素缓冲逐字节一致（memcmp）；
 *   - 旧实现不认识弧段，新 svg_fill 在兼容模式下对弧段画整圆，
 *     但纯线段路径的结果必须与旧实现没有任何差异。
 */
#include "svgpath.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* HEAD 版本的符号 */
int legacy_parse(const char *d, VPath *out);
int legacy_fill(const VPath *p, int w, int h, unsigned char *buf);

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

typedef struct { int w, h; const char *d; } Case;

int main(void) {
    Case cs[] = {
        {50, 50, "M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z"},
        {40, 40, "M0,0 L40,0 L40,40 L0,40 Z"},
        {33, 29, "M1,1 L31,1 L31,27 L1,27 Z M8,8 L24,8 L24,20 L8,20 Z"},
        {20, 20, "M0,0 L10,0 L10,10 Z"},
        {17, 23, "M2,3 L15,4 L9,20 Z M5,6 L13,7 L8,17 Z"},
        {64, 64, "M0,0 L63,2 L60,63 L3,60 Z"},
    };
    int ncase = (int)(sizeof cs / sizeof cs[0]);
    for (int i = 0; i < ncase; ++i) {
        VPath a, b;
        CK(legacy_parse(cs[i].d, &a) == 0);
        CK(svg_parse(cs[i].d, &b) == 0);
        CK(a.count == b.count);
        for (int k = 0; k < a.count && k < b.count; ++k) {
            CK(a.segs[k].kind == b.segs[k].kind);
            CK(a.segs[k].a.x == b.segs[k].a.x && a.segs[k].a.y == b.segs[k].a.y);
            CK(a.segs[k].b.x == b.segs[k].b.x && a.segs[k].b.y == b.segs[k].b.y);
        }
        size_t n = (size_t)cs[i].w * (size_t)cs[i].h;
        unsigned char *pa = malloc(n), *pb = malloc(n);
        CK(pa && pb);
        memset(pa, 0xA5, n);
        memset(pb, 0xA5, n);
        CK(legacy_fill(&a, cs[i].w, cs[i].h, pa) == 0);
        CK(svg_fill(&b, cs[i].w, cs[i].h, pb) == 0);
        CK(memcmp(pa, pb, n) == 0);
        int on = 0;
        for (size_t k = 0; k < n; ++k) if (pb[k]) ++on;
        printf("case %d: segs %d on %d bitwise %s\n", i, b.count, on,
               memcmp(pa, pb, n) == 0 ? "identical" : "MISMATCH");
        free(pa);
        free(pb);
    }

    /* 旧解析器对 A/a 报 -3；新 svg_parse 保持同一错误码 */
    VPath p;
    CK(legacy_parse("M0,0 A5,5 0 0 1 10,0", &p) == -3);
    CK(svg_parse("M0,0 A5,5 0 0 1 10,0", &p) == SVG_E_CMD);
    CK(legacy_parse("L0,0", &p) == -2);
    CK(svg_parse("L0,0", &p) == SVG_E_START);
    CK(legacy_parse("M0,0 L", &p) == -4);
    CK(svg_parse("M0,0 L", &p) == SVG_E_NUM);
    CK(legacy_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == SVG_E_CMD);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
