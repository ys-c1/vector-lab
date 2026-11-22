#define _DEFAULT_SOURCE
#include "contour.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static char tdir[] = "/tmp/contour-test-XXXXXX";

static int write_file(const char *name, const char *body) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", tdir, name);
    FILE *f = fopen(path, "w");
    fputs(body, f);
    fclose(f);
    return 0;
}

static void path_of(const char *name, char *out, size_t n) {
    snprintf(out, n, "%s/%s", tdir, name);
}

int main(void) {
    if (!mkdtemp(tdir)) { perror("mkdtemp"); return 2; }

    write_file("good.txt",
        "L 0\n0 0\n10 0\n10 10\n0 10\n"
        "\n"
        "L 1\n2 2\n8 2\n8 8\n2 8\n"
        "L 2\n3 3\n7 3\n7 7\n3 7\n");
    write_file("collinear.txt",
        "L 0\n0 0\n10 0\n10 10\n0 10\n"
        "L 1\n0 0\n5 0\n10 0\n");                 /* 全共线 */
    write_file("coincident.txt",
        "L 0\n1 1\n1 1\n1 1\n");                 /* 全重合 = 退化共线 */
    write_file("toofew.txt",
        "L 0\n0 0\n10 0\n10 10\n0 10\n"
        "L 1\n2 2\n8 2\n");                      /* 只有 2 点 */
    write_file("gap.txt",
        "L 0\n0 0\n10 0\n10 10\n"
        "L 2\n2 2\n8 2\n8 8\n");                 /* 跳号：缺 1 */
    write_file("starts1.txt",
        "L 1\n0 0\n10 0\n10 10\n");              /* 不从 0 起 */
    write_file("badfmt.txt",
        "L 0\n0 0\n10\n10 10\n0 10\n");          /* 缺 y */
    write_file("loose.txt",
        "0 0\n10 0\n10 10\n");                   /* 层头之前出现顶点 */
    write_file("mono.txt",
        "L 0\n0 0\n5 0\n5 5\n0 5\n"              /* 面积 25 */
        "L 1\n0 0\n10 0\n10 10\n0 10\n");        /* 面积 100，外层反而更大 */

    char path[256];
    CTSet set;
    CTError err;

    /* 正常加载：3 层、点数、鞋带面积、单调不增、内存倍率 */
    path_of("good.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_OK);
    CK(set.layers == 3);
    CK(set.cs[0].count == 4 && set.cs[1].count == 4 && set.cs[2].count == 4);
    CK(set.cs[0].layer == 0 && set.cs[2].layer == 2);
    CK(contour_validate(&set, &err) == CT_OK);
    double a[3];
    for (int l = 0; l < 3; ++l) a[l] = contour_area(set.cs[l].pts, set.cs[l].count);
    CK(fabs(a[0] - 100.0) < 1e-9);
    CK(fabs(a[1] - 36.0) < 1e-9);
    CK(fabs(a[2] - 16.0) < 1e-9);
    CK(contour_check_mono(&set, a, &err) == CT_OK);
    CK((double)set.peak_bytes <= 6.0 * (double)set.input_bytes);
    contour_free(&set);

    /* 全共线拒绝，指出层号 1 */
    path_of("collinear.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_OK);
    CK(contour_validate(&set, &err) == CT_ERR_COLLINEAR);
    CK(err.layer == 1);
    contour_free(&set);

    /* 全重合按共线拒绝，层号 0 */
    path_of("coincident.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_OK);
    CK(contour_validate(&set, &err) == CT_ERR_COLLINEAR);
    CK(err.layer == 0);
    contour_free(&set);

    /* 不足三点拒绝，指出层号 1 */
    path_of("toofew.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_OK);
    CK(contour_validate(&set, &err) == CT_ERR_TOOFEW);
    CK(err.layer == 1);
    contour_free(&set);

    /* 跳号拒绝，指出期望层号 1 */
    path_of("gap.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_ERR_GAP);
    CK(err.layer == 1);

    /* 层号不从 0 起：期望 0 */
    path_of("starts1.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_ERR_GAP);
    CK(err.layer == 0);

    /* 格式错误带当前层号 */
    path_of("badfmt.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_ERR_FORMAT);
    CK(err.layer == 0);

    /* 层头之前的顶点：格式错误，layer = -1 */
    path_of("loose.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_ERR_FORMAT);
    CK(err.layer == -1);

    /* 面积递增：单调性拒绝，指出第一个违例层 1 */
    path_of("mono.txt", path, sizeof(path));
    CK(contour_load(path, &set, &err) == CT_OK);
    CK(contour_validate(&set, &err) == CT_OK);
    double b[2] = {
        contour_area(set.cs[0].pts, set.cs[0].count),
        contour_area(set.cs[1].pts, set.cs[1].count),
    };
    CK(fabs(b[0] - 25.0) < 1e-9 && fabs(b[1] - 100.0) < 1e-9);
    CK(contour_check_mono(&set, b, &err) == CT_ERR_MONO);
    CK(err.layer == 1);
    contour_free(&set);

    /* 打不开文件 */
    CK(contour_load("/tmp/contour-test-XXXXXX/nope", &set, &err) == CT_ERR_IO);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
