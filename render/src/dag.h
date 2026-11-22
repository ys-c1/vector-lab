#ifndef LDAG_H
#define LDAG_H

/* 图层依赖有向图：adj[u][v] = 1 表示 u 必须先于 v 渲染（u 在 v 之下，v 遮挡 u）。
 * 节点数上限 32，与 RScene 的多边形上限一致。 */
#define LDAG_MAX 32
#define LDAG_NAME 16

typedef struct {
    int n;
    char name[LDAG_MAX][LDAG_NAME];
    int adj[LDAG_MAX][LDAG_MAX];
} LDag;

void dag_init(LDag *g);
int dag_find(const LDag *g, const char *name);
int dag_add_node(LDag *g, const char *name); /* 返回节点下标；重名返回 -1，满返回 -2 */
int dag_add_edge(LDag *g, int u, int v);     /* 重复边幂等忽略；下标非法返回 -1 */
int dag_indegree(const LDag *g, int v);
int dag_outdegree(const LDag *g, int u);

/* 环检测：有环返回 1，cyc 按顺序填写环上节点并在末尾重复起点，
 * 例如 L1 -> L2 -> L1 填 [L1,L2,L1]，*cyc_n 为 3。无环返回 0。 */
int dag_cycle(const LDag *g, int *cyc, int *cyc_n);

/* 拓扑排序定渲染顺序。规则（确定性）：
 * 先求每个节点的最早层级 ee（最长前驱链长度，源点为 1），
 * 每一步在剩余入度为 0 的节点中选 (ee, 下标) 最小者。
 * 有环返回 -1。 */
int dag_topo(const LDag *g, int *order, int *order_n);

/* 关键路径（节点权为 1 的最长路）：终点取 ee 最大、并列按下标最小，
 * 回溯时同样在满足 ee 差 1 的前驱中按下标最小。有环返回 -1。
 * path 按渲染先后填写，*path_n 为路径上的节点数。 */
int dag_critical(const LDag *g, int *path, int *path_n);

/* 孤立图层：入度、出度均为 0。返回填入的节点数。 */
int dag_isolated(const LDag *g, int *ids, int *n);

#endif
