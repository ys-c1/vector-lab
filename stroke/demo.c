#include "stroke.h"
#include <stdio.h>

static void report(const char *name, const SPoint *p, int n, double w) {
    SStrokeResult r;
    int rc = stroke_resolve(p, n, w, 1, &r);
    printf("== %s ==\n", name);
    if (rc != STROKE_OK) {
        printf("  拒绝裁剪: %s (rc=%d, 交点对号 %d)\n",
               stroke_strerror(rc), rc, r.bad_pair);
        if (r.bad_pair >= 0) {
            const SStrokeHit *h = &r.hits[r.bad_pair];
            printf("  对号 %d: 边 %d 与边 %d, 类型 %d, 交点 (%.4f, %.4f)\n",
                   h->pair, h->ea, h->eb, h->kind, h->at.x, h->at.y);
        }
        return;
    }
    printf("  轮廓点 %d，交点 %d，保留环 %d，裁掉环 %d\n",
           r.raw_count, r.hit_count, r.kept_count, r.loop_count);
    printf("  原始面积 %.6f = 保留 %.6f + 裁掉 %.6f (差 %.2e)\n",
           r.raw_area, r.kept_area, r.removed_area, r.closure_error);
    for (int i = 0; i < r.loop_count; ++i) {
        const SStrokeLoop *l = &r.loops[i];
        printf("  环 #%d: 面积 %.6f, 切点 (%.4f, %.4f), 由交点对",
               l->index, l->area, l->at.x, l->at.y);
        for (int q = 0; q < l->hit_pair_count; ++q)
            printf("%s%d", q ? "," : " ", l->hit_pairs[q]);
        printf(" 界定（边 %d-%d）\n", l->ea, l->eb);
    }
}

int main(void) {
    /* 旧接口输出保持不变。 */
    SPoint p[4] = {{0,0},{30,0},{30,20},{0,20}};
    SPoly out;
    if (stroke_polyline(p, 4, 3.0, &out) != 0) return 1;
    printf("outline %d\n", out.count);
    for (int i = 0; i < out.count; ++i)
        printf("  %.2f %.2f\n", out.pts[i].x, out.pts[i].y);

    SPoint L[3] = {{0,0},{10,0},{10,10}};
    report("直角 L 形，宽 2", L, 3, 2.0);

    SPoint S4[4] = {{0,0},{10,0},{10,10},{20,10}};
    report("四点 S 形，宽 2", S4, 4, 2.0);

    SPoint stair[6] = {{0,0},{10,0},{0,4},{10,8},{0,12},{10,16}};
    report("六点阶梯，宽 3", stair, 6, 3.0);

    return 0;
}
