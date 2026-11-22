/* 交叉验证：关闭合并窗口（新逻辑）时，sweep 的交点表必须与
 * “isect 模块的 isect_seg + 独立书写的逐事件扫描”逐字节相同。
 *
 * 参照实现刻意不复用 sweep.c 的任何扫描/排序/枚举代码：
 *   - 事件表用插入排序构建；
 *   - 活动集用有序链表维护（不是布尔数组快照）；
 *   - 交点用 isect/src/isect.c 的真实 isect_seg（与 sweep 内部同一算式）。
 * 两边各写一份 CSV，再用 memcmp 做逐字节比较；不一致时报首个字节偏移。
 */
#include "sweep.h"
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sets.h"

typedef struct RefEv { double t; int kind; int idx; } RefEv;

typedef struct RefNode { int idx; struct RefNode *next; } RefNode;

static long long g_checks = 0, g_fails = 0;

static int ref_ev_cmp(const void *pa, const void *pb) {
    const RefEv *a = (const RefEv *)pa, *b = (const RefEv *)pb;
    if (a->t != b->t) return a->t < b->t ? -1 : 1;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    if (a->idx != b->idx) return a->idx < b->idx ? -1 : 1;
    return 0;
}

/* 独立的逐事件参照：返回交点表 CSV 文本。 */
static char *ref_legacy_csv(const SwSeg *segs, int n, size_t *len_out) {
    RefEv *ev = malloc(sizeof(RefEv) * (size_t)(2 * n));
    if (!ev) return NULL;
    for (int i = 0; i < n; ++i) {
        double lo = segs[i].x0 < segs[i].x1 ? segs[i].x0 : segs[i].x1;
        double hi = segs[i].x0 < segs[i].x1 ? segs[i].x1 : segs[i].x0;
        ev[2*i] = (RefEv){lo, 0, i};
        ev[2*i+1] = (RefEv){hi, 1, i};
    }
    /* 插入排序：故意与库里的 qsort 不同 */
    for (int i = 1; i < 2 * n; ++i) {
        RefEv key = ev[i];
        int j = i - 1;
        while (j >= 0 && ref_ev_cmp(&ev[j], &key) > 0) { ev[j+1] = ev[j]; --j; }
        ev[j+1] = key;
    }

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { free(ev); return NULL; }

    RefNode *head = NULL;   /* 按 idx 升序的有序链表 */
    for (int e = 0; e < 2 * n; ++e) {
        int idx = ev[e].idx;
        if (ev[e].kind == 0) {
            RefNode *node = malloc(sizeof *node);
            if (!node) { free(buf); free(ev); return NULL; }
            node->idx = idx;
            RefNode **pp = &head;
            while (*pp && (*pp)->idx < idx) pp = &(*pp)->next;
            node->next = *pp;
            *pp = node;
        } else {
            RefNode **pp = &head;
            while (*pp && (*pp)->idx != idx) pp = &(*pp)->next;
            if (*pp) { RefNode *dead = *pp; *pp = dead->next; free(dead); }
        }
        for (RefNode *a = head; a; a = a->next) {
            for (RefNode *b = a->next; b; b = b->next) {
                IHit h;
                IPoint a0 = {segs[a->idx].x0, segs[a->idx].y0};
                IPoint a1 = {segs[a->idx].x1, segs[a->idx].y1};
                IPoint b0 = {segs[b->idx].x0, segs[b->idx].y0};
                IPoint b1 = {segs[b->idx].x1, segs[b->idx].y1};
                if (!isect_seg(a0, a1, b0, b1, &h)) continue;
                char line[256];
                int k = snprintf(line, sizeof line, "%d,%d,%d,%.17g,%.17g,%.17g,%.17g\n",
                                 e, a->idx, b->idx, h.t, h.u, h.at.x, h.at.y);
                if (len + (size_t)k + 1 > cap) {
                    cap *= 2;
                    char *nb = realloc(buf, cap);
                    if (!nb) { free(buf); free(ev); return NULL; }
                    buf = nb;
                }
                memcpy(buf + len, line, (size_t)k);
                len += (size_t)k;
            }
        }
    }
    while (head) { RefNode *nx = head->next; free(head); head = nx; }
    buf[len] = '\0';
    if (len_out) *len_out = len;
    free(ev);
    return buf;
}

/* 用库的旧路径生成同样格式的 CSV 文本。 */
static char *lib_legacy_csv(const SwSeg *segs, int n, size_t *len_out) {
    int nh = 0;
    SwStats st;
    int rc = sweep_legacy(segs, n, NULL, 0, &nh, &st);
    if (rc != SW_OK) return NULL;
    if (nh == 0) {
        char *b = calloc(1, 1);
        if (len_out) *len_out = 0;
        return b;
    }
    SwHit *hits = malloc(sizeof(SwHit) * (size_t)nh);
    if (!hits) return NULL;
    rc = sweep_legacy(segs, n, hits, nh, &nh, &st);
    if (rc != SW_OK) { free(hits); return NULL; }

    size_t cap = (size_t)nh * 96 + 64, len = 0;
    char *buf = malloc(cap);
    if (!buf) { free(hits); return NULL; }
    for (int i = 0; i < nh; ++i) {
        const SwHit *h = &hits[i];
        int k = snprintf(buf + len, cap - len, "%d,%d,%d,%.17g,%.17g,%.17g,%.17g\n",
                         h->snap, h->ia, h->ib, h->t, h->u, h->x, h->y);
        len += (size_t)k;
    }
    free(hits);
    if (len_out) *len_out = len;
    return buf;
}

static int compare_bytes(const char *tag, const char *a, size_t na,
                         const char *b, size_t nb) {
    ++g_checks;
    if (na != nb || (na && memcmp(a, b, na) != 0)) {
        ++g_fails;
        size_t lim = na < nb ? na : nb, off = 0;
        while (off < lim && a[off] == b[off]) ++off;
        printf("FAIL %s: 长度 ref=%zu lib=%zu，首个差异偏移 %zu\n", tag, na, nb, off);
        return 1;
    }
    printf("%-28s %8zu 字节 逐字节相同\n", tag, na);
    return 0;
}

int main(void) {
    SwSegSet sets[4];
    swset_all(sets);
    int bad = 0;
    for (int k = 0; k < 4; ++k) {
        size_t nr = 0, nl = 0;
        char *ref = ref_legacy_csv(sets[k].segs, sets[k].n, &nr);
        char *lib = lib_legacy_csv(sets[k].segs, sets[k].n, &nl);
        if (!ref || !lib) { printf("alloc failed\n"); free(ref); free(lib); return 2; }

        char tag[96];
        snprintf(tag, sizeof tag, "%s：逐事件交点表", sets[k].name);
        bad |= compare_bytes(tag, ref, nr, lib, nl);

        /* 关闭新逻辑的 sweep_run（flags=0）也应与逐事件路径一致 */
        int nh = 0;
        SwStats st;
        if (sweep_run(sets[k].segs, sets[k].n, 123.0, 0u, NULL, 0, &nh, &st) != SW_OK) {
            printf("FAIL sweep_run flags=0\n");
            bad = 1;
        } else {
            ++g_checks;
            int lib_hits = 0;
            SwStats st2;
            sweep_legacy(sets[k].segs, sets[k].n, NULL, 0, &lib_hits, &st2);
            if (nh != lib_hits || st.cand_legacy != st2.cand_legacy) {
                ++g_fails;
                printf("FAIL sweep_run(flags=0) 与 sweep_legacy 记录数/候选数不一致\n");
                bad = 1;
            }
        }
        free(ref);
        free(lib);
    }
    printf("%lld checks, %lld failures\n", g_checks, g_fails);
    return (bad || g_fails) ? 1 : 0;
}
