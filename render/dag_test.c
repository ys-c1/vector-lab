#include "src/dag.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* 菱形：A -> B,D -> C，另加孤立节点 E */
    LDag g;
    dag_init(&g);
    int A = dag_add_node(&g, "A");
    int B = dag_add_node(&g, "B");
    int C = dag_add_node(&g, "C");
    int D = dag_add_node(&g, "D");
    int E = dag_add_node(&g, "E");
    CK(A == 0 && B == 1 && C == 2 && D == 3 && E == 4);
    CK(dag_add_node(&g, "A") == -1);            /* 重名拒绝 */
    CK(dag_add_edge(&g, A, B) == 0);
    CK(dag_add_edge(&g, A, D) == 0);
    CK(dag_add_edge(&g, B, C) == 0);
    CK(dag_add_edge(&g, D, C) == 0);
    CK(dag_add_edge(&g, A, B) == 0);            /* 重复边幂等 */
    CK(dag_add_edge(&g, A, A) == -1);           /* 自环拒绝 */
    CK(dag_indegree(&g, C) == 2);
    CK(dag_outdegree(&g, A) == 2);

    int cyc[LDAG_MAX + 1], cn = 0;
    CK(dag_cycle(&g, cyc, &cn) == 0);

    int order[LDAG_MAX], on = 0;
    CK(dag_topo(&g, order, &on) == 0 && on == 5);
    CK(order[0] == A);                          /* 唯一源点先渲染 */
    CK(order[on - 1] == C);                     /* 汇合点最后渲染 */
    CK(on == 5);

    int path[LDAG_MAX], pn = 0;
    CK(dag_critical(&g, path, &pn) == 0);
    CK(pn == 3);                                /* 关键路径 3 个节点 */
    CK(path[0] == A && path[2] == C);
    CK(path[1] == B);                           /* 并列时按下标最小取 B */

    int iso[LDAG_MAX], in = 0;
    CK(dag_isolated(&g, iso, &in) == 0 && in == 1 && iso[0] == E);

    /* 有环：C -> B 与 B -> C 成环 */
    CK(dag_add_edge(&g, C, B) == 0);
    CK(dag_cycle(&g, cyc, &cn) == 1);
    CK(cn >= 3 && cyc[0] == cyc[cn - 1]);       /* 环路径首尾同点闭合 */
    int names_ok = 0;
    for (int i = 0; i + 1 < cn; ++i)
        if ((cyc[i] == B && cyc[i + 1] == C) || (cyc[i] == C && cyc[i + 1] == B))
            names_ok = 1;
    CK(names_ok);
    CK(dag_topo(&g, order, &on) == -1);         /* 有环拒绝拓扑 */
    CK(dag_critical(&g, path, &pn) == -1);     /* 有环拒绝关键路径 */

    /* 三节点链的关键路径长度 */
    LDag h;
    dag_init(&h);
    int x = dag_add_node(&h, "x"), y = dag_add_node(&h, "y"), z = dag_add_node(&h, "z");
    dag_add_edge(&h, x, y);
    dag_add_edge(&h, y, z);
    CK(dag_critical(&h, path, &pn) == 0 && pn == 3);
    CK(path[0] == x && path[1] == y && path[2] == z);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
