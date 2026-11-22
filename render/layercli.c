#define _POSIX_C_SOURCE 200809L
#include "src/dag.h"
#include "src/lscene.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void print_names(const LScene *s, const int *ids, int n) {
    if (n == 0) { printf("(none)\n"); return; }
    for (int i = 0; i < n; ++i)
        printf("%s%s", i ? " " : "", s->dag.name[ids[i]]);
    putchar('\n');
}

static int cmd_auto(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: layercli auto <scene.txt> <out.pgm> <w> <h>\n");
        return 2;
    }
    LScene s; char err[128];
    if (lscene_load(argv[1], &s, err, sizeof(err)) != 0) {
        fprintf(stderr, "load failed: %s\n", err);
        return 3;
    }
    int cyc[LDAG_MAX + 1], cyc_n;
    if (dag_cycle(&s.dag, cyc, &cyc_n) != 0) {
        fprintf(stderr, "REJECT cycle: ");
        for (int i = 0; i < cyc_n; ++i)
            fprintf(stderr, "%s%s", i ? " -> " : "", s.dag.name[cyc[i]]);
        fputc('\n', stderr);
        return 4;
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    unsigned char *buf = malloc((size_t)w * h);
    if (!buf) return 5;

    double t0 = now_s();
    int order[LDAG_MAX], order_n;
    if (dag_topo(&s.dag, order, &order_n) != 0) { free(buf); return 6; }
    int path[LDAG_MAX], path_n;
    if (dag_critical(&s.dag, path, &path_n) != 0) { free(buf); return 7; }
    int iso[LDAG_MAX], iso_n;
    dag_isolated(&s.dag, iso, &iso_n);
    if (lscene_render(&s, order, order_n, w, h, buf) != 0) { free(buf); return 8; }
    double t1 = now_s();

    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 9; }

    int ne = 0;
    for (int u = 0; u < s.dag.n; ++u)
        for (int v = 0; v < s.dag.n; ++v) ne += s.dag.adj[u][v];
    printf("layers %d edges %d\n", s.n, ne);
    printf("critical %d: ", path_n);
    print_names(&s, path, path_n);
    printf("order: ");
    print_names(&s, order, order_n);
    printf("isolated: ");
    print_names(&s, iso, iso_n);
    printf("elapsed_us %.1f\n", (t1 - t0) * 1e6);
    free(buf);
    return 0;
}

static int cmd_manual(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: layercli manual <scene.txt> <out.pgm> <w> <h>\n");
        return 2;
    }
    LScene s; char err[128];
    if (lscene_load(argv[1], &s, err, sizeof(err)) != 0) {
        fprintf(stderr, "load failed: %s\n", err);
        return 3;
    }
    if (s.order_n != s.n) {
        fprintf(stderr, "manual mode needs a full 'order' record in %s\n", argv[1]);
        return 4;
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    unsigned char *buf = malloc((size_t)w * h);
    if (!buf) return 5;

    double t0 = now_s();
    if (lscene_render(&s, s.order, s.order_n, w, h, buf) != 0) { free(buf); return 6; }
    double t1 = now_s();

    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("manual-order: ");
    print_names(&s, s.order, s.order_n);
    printf("elapsed_us %.1f\n", (t1 - t0) * 1e6);
    free(buf);
    return 0;
}

static int cmd_diff(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: layercli diff <a.pgm> <b.pgm>\n");
        return 2;
    }
    long na = 0, nb = 0;
    int d = lscene_pgm_diff(argv[1], argv[2], &na, &nb);
    if (d < 0) { fprintf(stderr, "pgm header mismatch or missing file\n"); return 3; }
    printf("DIFFPIX %d / %ld\n", d, na);
    return d == 0 ? 0 : 1;
}

static int cmd_dot(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: layercli dot <scene.txt>\n");
        return 2;
    }
    LScene s; char err[128];
    if (lscene_load(argv[1], &s, err, sizeof(err)) != 0) {
        fprintf(stderr, "load failed: %s\n", err);
        return 3;
    }
    int cyc[LDAG_MAX + 1], cyc_n;
    if (dag_cycle(&s.dag, cyc, &cyc_n) != 0) {
        fprintf(stderr, "REJECT cycle: ");
        for (int i = 0; i < cyc_n; ++i)
            fprintf(stderr, "%s%s", i ? " -> " : "", s.dag.name[cyc[i]]);
        fputc('\n', stderr);
        return 4;
    }
    int path[LDAG_MAX], path_n;
    if (dag_critical(&s.dag, path, &path_n) != 0) return 5;
    int iso[LDAG_MAX], iso_n;
    dag_isolated(&s.dag, iso, &iso_n);

    printf("digraph layers {\n  rankdir=LR;\n");
    for (int i = 0; i < iso_n; ++i)
        printf("  %s [style=dashed,color=gray];\n", s.dag.name[iso[i]]);
    for (int u = 0; u < s.dag.n; ++u)
        for (int v = 0; v < s.dag.n; ++v) {
            if (!s.dag.adj[u][v]) continue;
            int on_cp = 0;
            for (int k = 0; k + 1 < path_n; ++k)
                if (path[k] == u && path[k + 1] == v) on_cp = 1;
            printf("  %s -> %s%s;\n", s.dag.name[u], s.dag.name[v],
                   on_cp ? " [color=red,penwidth=2.0]" : "");
        }
    printf("}\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: layercli auto|manual|diff|dot ...\n");
        return 2;
    }
    if (!strcmp(argv[1], "auto")) return cmd_auto(argc - 1, argv + 1);
    if (!strcmp(argv[1], "manual")) return cmd_manual(argc - 1, argv + 1);
    if (!strcmp(argv[1], "diff")) return cmd_diff(argc - 1, argv + 1);
    if (!strcmp(argv[1], "dot")) return cmd_dot(argc - 1, argv + 1);
    fprintf(stderr, "unknown subcommand %s\n", argv[1]);
    return 2;
}
