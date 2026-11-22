#include "dag.h"
#include <string.h>

void dag_init(LDag *g) {
    if (!g) return;
    g->n = 0;
    memset(g->adj, 0, sizeof(g->adj));
}

int dag_find(const LDag *g, const char *name) {
    if (!g || !name) return -1;
    for (int i = 0; i < g->n; ++i)
        if (strncmp(g->name[i], name, LDAG_NAME) == 0) return i;
    return -1;
}

int dag_add_node(LDag *g, const char *name) {
    if (!g || !name || name[0] == '\0') return -1;
    int id = dag_find(g, name);
    if (id >= 0) return -1;
    if (g->n >= LDAG_MAX) return -2;
    id = g->n++;
    /* 截断到 LDAG_NAME-1，保证 NUL 结尾 */
    size_t k = 0;
    while (k + 1 < LDAG_NAME && name[k]) { g->name[id][k] = name[k]; ++k; }
    g->name[id][k] = '\0';
    return id;
}

int dag_add_edge(LDag *g, int u, int v) {
    if (!g || u < 0 || v < 0 || u >= g->n || v >= g->n || u == v) return -1;
    g->adj[u][v] = 1;
    return 0;
}

int dag_indegree(const LDag *g, int v) {
    if (!g || v < 0 || v >= g->n) return -1;
    int d = 0;
    for (int u = 0; u < g->n; ++u) d += g->adj[u][v];
    return d;
}

int dag_outdegree(const LDag *g, int u) {
    if (!g || u < 0 || u >= g->n) return -1;
    int d = 0;
    for (int v = 0; v < g->n; ++v) d += g->adj[u][v];
    return d;
}

/* ---- 环检测：DFS 三色标记，灰栈记录当前递归路径 ---- */
static int dfs_cycle(const LDag *g, int u, int *color, int *stack, int sp,
                     int *cyc, int *cyc_n) {
    color[u] = 1;
    stack[sp] = u;
    for (int v = 0; v < g->n; ++v) {
        if (!g->adj[u][v]) continue;
        if (color[v] == 1) {                 /* 找到回边 u -> v */
            int k = 0;
            while (stack[k] != v) ++k;       /* v 在栈中的位置 */
            int m = 0;
            for (int i = k; i <= sp; ++i) cyc[m++] = stack[i];
            cyc[m++] = v;                    /* 末尾重复起点闭合环 */
            *cyc_n = m;
            return 1;
        }
        if (color[v] == 0 && dfs_cycle(g, v, color, stack, sp + 1, cyc, cyc_n))
            return 1;
    }
    color[u] = 2;
    return 0;
}

int dag_cycle(const LDag *g, int *cyc, int *cyc_n) {
    if (!g || !cyc || !cyc_n) return -1;
    int color[LDAG_MAX] = {0};
    int stack[LDAG_MAX];
    *cyc_n = 0;
    for (int s = 0; s < g->n; ++s)
        if (color[s] == 0 &&
            dfs_cycle(g, s, color, stack, 0, cyc, cyc_n))
            return 1;
    return 0;
}

/* 最早层级 ee：ee(v) = max(1, 1 + max ee(u))，u 为 v 的前驱 */
static void earliest(const LDag *g, int *ee) {
    int indeg[LDAG_MAX];
    for (int v = 0; v < g->n; ++v) {
        indeg[v] = 0;
        ee[v] = 1;
    }
    for (int u = 0; u < g->n; ++u)
        for (int v = 0; v < g->n; ++v)
            indeg[v] += g->adj[u][v];

    int removed = 0;
    while (removed < g->n) {
        int pick = -1;
        for (int v = 0; v < g->n; ++v)
            if (indeg[v] == 0 && (pick < 0 || v < pick)) pick = v;
        if (pick < 0) break;                 /* 有环（外部通常先做环检测） */
        indeg[pick] = -1;
        ++removed;
        for (int v = 0; v < g->n; ++v)
            if (g->adj[pick][v]) {
                if (ee[pick] + 1 > ee[v]) ee[v] = ee[pick] + 1;
                --indeg[v];
            }
    }
}

int dag_topo(const LDag *g, int *order, int *order_n) {
    if (!g || !order || !order_n) return -1;
    int cyc[LDAG_MAX + 1], cyc_n;
    if (dag_cycle(g, cyc, &cyc_n) != 0) return -1;

    int ee[LDAG_MAX];
    earliest(g, ee);

    int indeg[LDAG_MAX];
    for (int v = 0; v < g->n; ++v) {
        indeg[v] = 0;
        for (int u = 0; u < g->n; ++u) indeg[v] += g->adj[u][v];
    }

    int done = 0;
    while (done < g->n) {
        int pick = -1;
        for (int v = 0; v < g->n; ++v) {
            if (indeg[v] != 0) continue;
            if (pick < 0 || ee[v] < ee[pick] ||
                (ee[v] == ee[pick] && v < pick))
                pick = v;
        }
        if (pick < 0) return -1;             /* 理论不可达：前面已过环检测 */
        order[done++] = pick;
        indeg[pick] = -1;
        for (int v = 0; v < g->n; ++v)
            if (g->adj[pick][v]) --indeg[v];
    }
    *order_n = done;
    return 0;
}

int dag_critical(const LDag *g, int *path, int *path_n) {
    if (!g || !path || !path_n) return -1;
    int cyc[LDAG_MAX + 1], cyc_n;
    if (dag_cycle(g, cyc, &cyc_n) != 0) return -1;

    int ee[LDAG_MAX];
    earliest(g, ee);

    /* 终点：ee 最大，并列取最小下标 */
    int end = 0;
    for (int v = 1; v < g->n; ++v)
        if (ee[v] > ee[end] || (ee[v] == ee[end] && v < end)) end = v;

    /* 回溯：每一步在满足 ee(u)+1==ee(cur) 的前驱里取最小下标 */
    int rev[LDAG_MAX], rn = 0;
    int cur = end;
    rev[rn++] = cur;
    while (ee[cur] > 1) {
        int pre = -1;
        for (int u = 0; u < g->n; ++u) {
            if (g->adj[u][cur] && ee[u] + 1 == ee[cur] && (pre < 0 || u < pre))
                pre = u;
        }
        if (pre < 0) return -1;
        rev[rn++] = pre;
        cur = pre;
    }
    for (int i = 0; i < rn; ++i) path[i] = rev[rn - 1 - i];
    *path_n = rn;
    return 0;
}

int dag_isolated(const LDag *g, int *ids, int *n) {
    if (!g || !ids || !n) return -1;
    int m = 0;
    for (int v = 0; v < g->n; ++v)
        if (dag_indegree(g, v) == 0 && dag_outdegree(g, v) == 0)
            ids[m++] = v;
    *n = m;
    return 0;
}
