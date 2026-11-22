#include "xform.h"
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static unsigned long long rng = 0x9e3779b97f4a7c15ull;
static double frand(void) { /* xorshift64*，确定性伪随机 */
    rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
    return (double)(rng * 0x2545F4914F6CDD1Dull >> 11) / (double)(1ull << 52) * 2000.0 - 1000.0;
}

int main(void) {
    XForm id = xform_identity();
    XPoint p = {2, 3};
    XPoint q = xform_apply(id, p);
    CK(q.x == 2 && q.y == 3);
    XForm t = {1, 0, 0, 1, 10, 20};
    XPoint r = xform_apply(t, p);
    CK(r.x == 12 && r.y == 23);
    XForm s = {2, 0, 0, 2, 0, 0};
    XForm m = xform_mul(s, t);
    XPoint u = xform_apply(m, p);
    CK(u.x == 24 && u.y == 46);

    /* 拒绝路径：空数组、零块宽，各自指名参数 */
    XPoint one = {1, 1};
    CK(xform_batch_inplace(m, NULL, 10, 4) != NULL &&
       strcmp(xform_batch_inplace(m, NULL, 10, 4), "pts") == 0);
    CK(strcmp(xform_batch_inplace(m, &one, 0, 4), "n") == 0);
    CK(strcmp(xform_batch_inplace(m, &one, 1, 0), "chunk") == 0);

    /* 分块批量与逐点循环逐位一致（同一组运算，无重排） */
    enum { N = 301 };
    XPoint a[N], b[N];
    for (int i = 0; i < N; ++i) { a[i].x = frand(); a[i].y = frand(); }
    memcpy(b, a, sizeof a);
    for (int i = 0; i < N; ++i) a[i] = xform_apply(m, a[i]);
    size_t chunks[] = {1, 3, 4, 7, 64, 1000};
    for (size_t k = 0; k < sizeof chunks / sizeof chunks[0]; ++k) {
        XPoint c[N];
        memcpy(c, b, sizeof c);
        CK(xform_batch_inplace(m, c, N, chunks[k]) == NULL);
        CK(memcmp(a, c, sizeof a) == 0);
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
