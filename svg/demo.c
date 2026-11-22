#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 双环：外框 40×40 + 内框 20×20。
 * 同向（都逆时针）时奇偶规则挖洞、非零规则不挖；
 * 反向时两规则都挖洞。手算外框 1600、内框 400、环面积（洞面积）400。 */
static const char *RING_SAME =
    "M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z";
static const char *RING_OPP =
    "M5,5 L45,5 L45,45 L5,45 Z M15,15 L15,35 L35,35 L35,15 Z";

#define REPS 200

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static const char *rule_name(int r) {
    return r == SVG_FILL_EVENODD ? "evenodd"
         : r == SVG_FILL_NONZERO ? "nonzero"
         : "legacy";
}

static int count_on(const unsigned char *buf, int n) {
    int on = 0;
    for (int i = 0; i < n; ++i) if (buf[i]) ++on;
    return on;
}

/* 主表一栏一个指标：size | rule | holes | hole subpaths | hole area | fill ms */
static int run_case(const char *tag, const char *d, FILE *csv) {
    VSubPaths p;
    int rc = svg_parse_ex(d, &p);
    if (rc != 0) {
        printf("%s: parse failed: %s\n", tag, svg_strerror(rc));
        return 1;
    }
    printf("\n== %s（子路径 %d，手算外框 1600、内框 400、环面积 400）==\n",
           tag, p.sub_count);
    printf("| size | rule | holes | hole subpaths | hole area | fill ms | area | px |\n");
    printf("|-----:|:-----|------:|:--------------|----------:|--------:|-----:|---:|\n");

    double area[2] = {0.0, 0.0};
    int sizes[2] = {64, 512};

    for (int s = 0; s < 2; ++s) {
        int w = sizes[s], h = sizes[s];
        unsigned char *buf = (unsigned char *)malloc((size_t)w * h);
        if (!buf) return 1;
        for (int r = 0; r < 2; ++r) {
            int rule = r == 0 ? SVG_FILL_EVENODD : SVG_FILL_NONZERO;
            SVGStats st;
            double t0 = now_sec();
            for (int it = 0; it < REPS; ++it)
                rc = svg_fill_rule(&p, w, h, buf, rule, &st);
            double ms = (now_sec() - t0) * 1e3 / REPS;
            if (rc != 0) {
                printf("fill failed: %s (subpath %d)\n",
                       svg_strerror(rc), st.bad_sub);
                free(buf);
                return 1;
            }
            if (svg_fill_area(&p, rule, &area[r], NULL) != 0) {
                free(buf);
                return 1;
            }
            char hole_subs[64];
            size_t pos = 0;
            hole_subs[0] = '\0';
            for (int j = 0; j < st.subpaths; ++j) {
                if (!st.sub_is_hole[j]) continue;
                pos += (size_t)snprintf(hole_subs + pos, sizeof hole_subs - pos,
                                        "%s%d", pos ? "," : "", j + 1);
            }
            if (st.holes == 0) snprintf(hole_subs, sizeof hole_subs, "-");
            printf("| %4d | %-7s | %5d | %-13s | %9.1f | %7.3f | %4.1f | %4d |\n",
                   w, rule_name(rule), st.holes, hole_subs, st.hole_area, ms,
                   area[r], count_on(buf, w * h));
            if (csv) {
                for (int j = 0; j < st.subpaths; ++j)
                    fprintf(csv,
                            "%s,%d,%s,%d,%.1f,%d,%.1f,%.3f\n",
                            tag, w, rule_name(rule), j + 1, st.sub_area[j],
                            st.sub_is_hole[j],
                            st.sub_is_hole[j] ? st.sub_area[j] : 0.0, ms);
            }
        }
        free(buf);
    }

    double diff = fabs(area[0] - area[1]);
    double hand_ring = 400.0;
    printf("规则差值 |evenodd - nonzero| = %.1f，手算环面积 %.0f -> %s\n",
           diff, hand_ring, diff <= hand_ring + 1e-9 ? "上界成立" : "上界被突破");
    return 0;
}

int main(void) {
    FILE *csv = fopen("demo.csv", "w");
    if (!csv) { printf("cannot write demo.csv\n"); return 1; }
    fprintf(csv, "case,size,rule,subpath,sub_area,is_hole,hole_area,fill_ms\n");

    int bad = 0;
    bad |= run_case("ring same winding", RING_SAME, csv);
    bad |= run_case("ring opposite winding", RING_OPP, csv);

    /* --- 退化：旧解析 + 旧填充 vs LEGACY 子路径填充，逐字节一致 --- */
    const char *cases[3] = {
        "M5,5 L45,5 L45,45 L5,45 Z",
        "M0,0 L10,10 L10,0 L0,10 Z",
        RING_SAME
    };
    int all_same = 1, n = 50;
    for (int k = 0; k < 3; ++k) {
        VPath old;
        VSubPaths sub;
        if (svg_parse(cases[k], &old) != 0 ||
            svg_parse_legacy(cases[k], &sub) != 0) { all_same = 0; continue; }
        unsigned char *a = (unsigned char *)malloc((size_t)n * n);
        unsigned char *b = (unsigned char *)malloc((size_t)n * n);
        svg_fill(&old, n, n, a);
        SVGStats s2;
        svg_fill_rule(&sub, n, n, b, SVG_FILL_LEGACY, &s2);
        if (memcmp(a, b, (size_t)n * n) != 0) all_same = 0;
        free(a); free(b);
    }
    printf("\n退化路径逐字节：%s（3 个路径 × 50×50，旧接口 vs LEGACY）\n",
           all_same ? "identical" : "MISMATCH");
    if (!all_same) ++bad;

    /* --- 报错：空路径 / 未闭合子路径号 --- */
    VSubPaths e;
    int rc = svg_parse_ex("", &e);
    printf("空路径       -> %d %s, bad_sub=%d\n", rc, svg_strerror(rc), e.bad_sub);
    rc = svg_parse_ex("M0,0 L10,0 L10,10 Z M20,20 L30,20 L30,30", &e);
    printf("未闭合子路径 -> %d %s, bad_sub=%d\n", rc, svg_strerror(rc), e.bad_sub);

    fclose(csv);
    printf("\ncsv -> demo.csv（逐子路径面积/是否洞/耗时明细）\n");
    return bad;
}
