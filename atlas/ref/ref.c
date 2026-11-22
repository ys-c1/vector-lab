/* 独立参照实现：与 src/atlas.c 不同的代码组织（平铺数组、插入排序、
 * 无 Shelf/Page 结构），只遵循同一份确定性规范：
 *   - 每个图块旋转到高>=宽；
 *   - 货架首次适应（FFDH），页满开新页；
 *   - smart 次序：归一化高降序、面积降序、长宽比降序、编号升序。
 * 用法: ref <tiles.txt> W H input|smart   （输出格式与主 CLI 逐字段一致）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int id, w, h; } T;
typedef struct { int id, bin, x, y, w, h, rot; } P;

static int norm_w(const T *t) { return t->w > t->h ? t->h : t->w; }
static int norm_h(const T *t) { return t->w > t->h ? t->w : t->h; }
static int norm_rot(const T *t) { return t->w > t->h; }

static int before_smart(const T *a, const T *b) {
    int ah = norm_h(a), bh = norm_h(b);
    if (ah != bh) return ah > bh;
    long aa = (long)norm_w(a) * ah, ab = (long)norm_w(b) * bh;
    if (aa != ab) return aa > ab;
    long la = (long)ah * norm_w(b), lb = (long)bh * norm_w(a);
    if (la != lb) return la > lb;
    return a->id < b->id;
}

int main(int argc, char **argv) {
    if (argc != 5) return 1;
    int W = atoi(argv[2]), H = atoi(argv[3]);
    int smart = !strcmp(argv[4], "smart");
    if (!smart && strcmp(argv[4], "input")) return 1;

    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    int cap = 16, n = 0, next_id = 0;
    T *ts = malloc(sizeof(T) * (size_t)cap);
    char line[128]; int cnt, w, h;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%d %d %d", &cnt, &w, &h) != 3) return 4;
        for (int k = 0; k < cnt; ++k) {
            if (n == cap) { cap *= 2; ts = realloc(ts, sizeof(T) * (size_t)cap); }
            ts[n].id = next_id++; ts[n].w = w; ts[n].h = h; ++n;
        }
    }
    fclose(f);

    T *seq = malloc(sizeof(T) * (size_t)n);
    memcpy(seq, ts, sizeof(T) * (size_t)n);
    if (smart) {
        /* 插入排序：参照实现刻意不用 qsort */
        for (int i = 1; i < n; ++i) {
            T key = seq[i]; int j = i - 1;
            while (j >= 0 && before_smart(&key, &seq[j])) { seq[j + 1] = seq[j]; --j; }
            seq[j + 1] = key;
        }
    }

    /* 货架平行数组（所有页连续存放），bin_of[k] 记录货架所属页 */
    int *sy = calloc((size_t)n, sizeof(int));
    int *sh_ = calloc((size_t)n, sizeof(int));
    int *su = calloc((size_t)n, sizeof(int));
    int *sbin = calloc((size_t)n, sizeof(int));
    int nshelf = 0, cur_bin = 0, cur_bottom = 0, reject = -1;

    P *out = malloc(sizeof(P) * (size_t)n);
    int nout = 0;
    for (int i = 0; i < n; ++i) {
        int rw = norm_w(&seq[i]), rh = norm_h(&seq[i]);
        if (rw > W || rh > H) { reject = seq[i].id; break; }
        int pick = -1;
        for (int k = 0; k < nshelf; ++k)
            if (sbin[k] == cur_bin && rh <= sh_[k] && su[k] + rw <= W) { pick = k; break; }
        if (pick < 0) {
            if (cur_bottom + rh > H) {
                ++cur_bin; cur_bottom = 0;
            }
            sy[nshelf] = cur_bottom; sh_[nshelf] = rh; su[nshelf] = 0;
            sbin[nshelf] = cur_bin;
            pick = nshelf; ++nshelf;
            cur_bottom += rh;
        }
        out[nout].id = seq[i].id; out[nout].bin = sbin[pick];
        out[nout].x = su[pick]; out[nout].y = sy[pick];
        out[nout].w = rw; out[nout].h = rh; out[nout].rot = norm_rot(&seq[i]);
        su[pick] += rw; ++nout;
    }

    if (reject >= 0) {
        fprintf(stderr, "ref reject id=%d for %dx%d\n", reject, W, H);
        return 10;
    }

    int bins = 0;
    for (int i = 0; i < nout; ++i) if (out[i].bin + 1 > bins) bins = out[i].bin + 1;
    printf("order=%s atlas=%dx%d tiles=%d bins=%d\n",
           smart ? "smart" : "input", W, H, n, bins);
    printf("seq id bin x y w h rotated\n");
    for (int i = 0; i < nout; ++i)
        printf("%3d %3d %3d %3d %3d %3d %3d %d\n",
               i, out[i].id, out[i].bin, out[i].x, out[i].y,
               out[i].w, out[i].h, out[i].rot);

    /* 浪费率：逐页最大货架底边 */
    long bound = 0, used = 0;
    for (int b = 0; b < bins; ++b) {
        int bottom = 0;
        for (int i = 0; i < nout; ++i) {
            if (out[i].bin != b) continue;
            if (out[i].y + out[i].h > bottom) bottom = out[i].y + out[i].h;
            used += (long)out[i].w * out[i].h;
        }
        bound += (long)bottom * W;
    }
    printf("waste=%.6f\n", (double)(bound - used) / (double)bound);
    return 0;
}
