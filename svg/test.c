#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    VPath p;
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &p) == 0);
    CK(p.count == 3);
    unsigned char *buf = (unsigned char *)malloc(20 * 20);
    CK(svg_fill(&p, 20, 20, buf) == 0);
    int on = 0;
    for (int i = 0; i < 400; ++i) if (buf[i]) ++on;
    /* 8x8 覆盖率 + 包围盒映射：三角形占包围盒约一半，即约 200/400 像素。 */
    CK(on > 150 && on < 230);
    int full = 0, partial = 0;
    for (int i = 0; i < 400; ++i) {
        if (buf[i] == 255) ++full;
        else if (buf[i] > 0) ++partial;
    }
    CK(full > 0);
    CK(partial > 0); /* 证明不再是单点 0/255 判定 */

    /* 宽或高为零/负：当场失败并由返回码指名参数非法。 */
    CK(svg_fill(&p, 0, 20, buf) == -1);
    CK(svg_fill(&p, 20, 0, buf) == -1);
    CK(svg_fill(&p, -5, 20, buf) == -1);
    CK(svg_fill(&p, 20, -5, buf) == -1);
    CK(svg_fill(0, 20, 20, buf) == -1);
    CK(svg_fill(&p, 20, 20, 0) == -1);

    /* 空路径：没有段可栅格化。 */
    VPath empty;
    empty.count = 0;
    CK(svg_fill(&empty, 20, 20, buf) == -2);

    /* 输出缓冲未被超采样中间态污染：零尺寸调用前内容不被改写。 */
    memset(buf, 0x7A, 400);
    CK(svg_fill(&p, 0, 20, buf) == -1);
    CK(buf[0] == 0x7A);

    CK(svg_parse("L0,0", &p) == -2);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    CK(svg_parse("M0,0 L", &p) == -4);

    /* PGM 落盘头与像素数。 */
    CK(svg_save_pgm("build/test.pgm", buf + 0, 1, 1) == 0);
    CK(svg_save_pgm("build/test.pgm", buf, 20, 0) == -1);
    free(buf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
