/* 基线生成器：只用旧固定 8 段接口（bezier_flatten）导出点序列，
 * 供 baseline/ 使用。此文件不链接任何新逻辑，确保基线独立于自适应实现。
 * 用法：./build/gen_baseline > baseline/fixed8_demo.txt
 */
#include "bezier.h"
#include <stdio.h>

int main(void) {
    /* 与 bezier/demo.c 完全相同的曲线与容差。 */
    BPoint c[4] = {{0,0},{20,40},{60,40},{80,0}};
    BPoly p;
    if (bezier_flatten(c, 0.5, &p) != 0) return 1;
    for (int i = 0; i < p.count; ++i)
        printf("%.17g %.17g\n", p.pts[i].x, p.pts[i].y);
    return 0;
}
