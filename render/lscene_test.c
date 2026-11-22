#include "src/lscene.h"
#include "src/dag.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* 两个交叠矩形：A 在下，B 在上，B 灰度应覆盖 A */
    LScene s;
    char err[128];
    int rc = lscene_load("cases/lscene-unit.txt", &s, err, sizeof(err));
    if (rc != 0) { printf("load failed: %s\n", err); return 1; }
    CK(s.n == 2);
    CK(dag_find(&s.dag, "A") == 0 && dag_find(&s.dag, "B") == 1);
    CK(s.dag.adj[0][1] == 1);
    CK(s.has_win == 1);

    int w = 10, h = 10;
    unsigned char *buf = malloc((size_t)w * h);
    int order[2] = {0, 1};
    CK(lscene_render(&s, order, 2, w, h, buf) == 0);
    /* 中心像素 (5,5) 同时落在 A、B 内，取后渲染者 B 的灰度 200 */
    CK(buf[5 * w + 5] == 200);
    /* 像素 (1,1) 中心 (1.5,1.5) 只在 A 内（B 从 3 开始） */
    CK(buf[1 * w + 1] == 100);
    /* 像素 (0,0) 中心 (0.5,0.5) 在所有图层之外 */
    CK(buf[0] == 0);
    CK(export_pgm("build/lscene-a.pgm", buf, w, h) == 0);

    /* 反序渲染：A 后渲染，中心像素变回 100，两图必然存在差异 */
    int rev[2] = {1, 0};
    unsigned char *buf2 = malloc((size_t)w * h);
    CK(lscene_render(&s, rev, 2, w, h, buf2) == 0);
    CK(buf2[5 * w + 5] == 100);
    CK(export_pgm("build/lscene-b.pgm", buf2, w, h) == 0);
    long na = 0, nb = 0;
    int d = lscene_pgm_diff("build/lscene-a.pgm", "build/lscene-b.pgm", &na, &nb);
    CK(d > 0 && na == (long)w * h && nb == (long)w * h);
    CK(lscene_pgm_diff("build/lscene-a.pgm", "build/lscene-a.pgm", &na, &nb) == 0);
    CK(lscene_pgm_diff("build/lscene-a.pgm", "nonexistent.pgm", &na, &nb) == -1);

    /* 手排顺序解析：合法全量顺序 */
    CK(s.order_n == 2 && s.order[0] == 0 && s.order[1] == 1);

    /* 错误夹具：order 漏列图层 */
    LScene bad;
    CK(lscene_load("cases/lscene-badorder.txt", &bad, err, sizeof(err)) != 0);

    free(buf);
    free(buf2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
