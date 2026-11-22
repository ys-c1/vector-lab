/* 交叉验证：关闭新逻辑（-DKEYFRAME_MIN_ACTION=0）时，key_solve_default
 * 的节点表与中点必须与 baseline/linear_ref.c 的旧线性实现逐字节一致。
 * linear_ref 与本目录的 src/ 没有任何共享代码，节点与中点各自按
 * %.17g 打印；这里把主实现的结果按同一格式写盘，再逐字节比对。
 * 用法：compat <ref_dir>（ref_*.txt 由 Makefile 先生成）。 */
#include "keyframe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

typedef struct { const char *name; int nkeys, levels; KeyFrame k[8]; } Case;

static const Case cases[] = {
    {"square2", 5, 2, {
        {0.0,  0.0,  0.0}, {1.0, 10.0,  0.0}, {2.0, 10.0, 10.0},
        {3.0,  0.0, 10.0}, {4.0,  0.0,  0.0}
    }},
    {"square4", 5, 4, {
        {0.0,  0.0,  0.0}, {1.0, 10.0,  0.0}, {2.0, 10.0, 10.0},
        {3.0,  0.0, 10.0}, {4.0,  0.0,  0.0}
    }},
    {"uneven", 4, 3, {
        {0.0,  0.0,  0.0}, {0.7, 10.0,  0.0}, {2.0, 10.0, 10.0},
        {3.1,  0.0, 10.0}
    }},
    {"two", 2, 5, {
        {0.0, 0.0, 0.0}, {4.0, 8.0, 6.0}
    }},
};
#define NCASES ((int)(sizeof cases / sizeof cases[0]))

static int dump_ours(const char *path, const KeyPlan *p) {
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "%d %d %d\n", p->nkeys, p->levels, p->nnodes);
    for (int j = 0; j < p->nnodes; ++j)
        fprintf(fp, "%.17g %.17g %.17g\n",
                p->nodes[j].t, p->nodes[j].x, p->nodes[j].y);
    for (int i = 0; i < p->nspans; ++i)
        fprintf(fp, "mid %.17g %.17g %.17g\n",
                p->spans[i].mid.t, p->spans[i].mid.x, p->spans[i].mid.y);
    fclose(fp);
    return 0;
}

static int same_bytes(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
    int same = 1;
    for (;;) {
        int ca = fgetc(fa), cb = fgetc(fb);
        if (ca != cb) { same = 0; break; }
        if (ca == EOF) break;
    }
    fclose(fa); fclose(fb);
    return same;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s ref_dir\n", argv[0]); return 1; }
    for (int c = 0; c < NCASES; ++c) {
        const Case *cs = &cases[c];
        KeyPlan p;
        int e = -1;
        CK(key_solve_default(cs->k, cs->nkeys, cs->levels, &p, &e) == KEY_OK);
        CK(dump_ours("ref_ours.txt", &p) == 0);
        char ref[512];
        snprintf(ref, sizeof ref, "%s/ref_%s.txt", argv[1], cs->name);
        int same = same_bytes("ref_ours.txt", ref);
        if (!same) printf("MISMATCH %s\n", cs->name);
        CK(same);
        remove("ref_ours.txt");

        /* 关闭开关时平滑轨就是线性轨：作用量两轨相同，降幅为 0。 */
        CK(p.total_action == p.total_linear_action);
        CK(p.total_reduction == 0.0);
        for (int i = 0; i < p.nspans; ++i) {
            CK(p.spans[i].action == p.spans[i].linear_action);
            CK(p.spans[i].reduction == 0.0);
        }
    }

    /* 关闭时中点仍由两端关键帧直接算出，不经节点表。 */
    KeyPlan p;
    CK(key_solve_default(cases[0].k, 5, 2, &p, NULL) == KEY_OK);
    CK(p.spans[0].mid.x == 5.0 && p.spans[0].mid.y == 0.0);
    CK(p.spans[3].mid.x == 0.0 && p.spans[3].mid.y == 5.0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
