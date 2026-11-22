/*
 * 二维参数反解命令行：
 *   paramcli <scene.poly> <ref.pgm> <w> <h> [sslo..sshi] [bitslo..bitshi] [min|max]
 *
 * 场景文件：每行一个多边形，首尾自动闭合，形如
 *     p 20 20 120 20 120 120 20 120
 * 或直接一串 "x y"（一行一个顶点）。与 gold 的 poly.txt 兼容。
 *
 * 输出：每个匹配组合一行、选中组合一行、汇总一行；越界参数拒绝并写清字段。
 * 退出码：0 成功（哪怕并列）；2 参数越界（ss/bits）；3 无匹配；
 *         4 文件/IO；5 尺寸不符；6 其它。
 */
#include "src/scene.h"
#include "src/paramsolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 场景读取：按 token 处理。token 为 "p"/"P" 时开始一个新多边形；其余
 * token 必须是数字，成对组成 (x,y)。因此以下两种写法都支持：
 *     p 20 20 120 20 120 120 20 120      （一行一个多边形）
 *     p
 *     20 20
 *     120 20
 *     ...                                 （与 gold 的 poly.txt 同形）
 * 每个多边形首尾自动闭合（不需要重复首点）。
 */
static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    scene_init(s);
    char tok[64];
    RPoint pts[64];
    int n = 0, have = 0;
    double val = 0.0;
    int rc = 0;

#define FLUSH_POLY()                                                       \
    do {                                                                   \
        if (have) { rc = -2; goto done; } /* 落单的半坐标 */                \
        if (n > 0) {                                                       \
            if (n < 3) { rc = -2; goto done; }                             \
            if (scene_add_poly(s, pts, n) != 0) { rc = -3; goto done; }    \
            n = 0;                                                         \
        }                                                                  \
    } while (0)

    while (fscanf(f, "%63s", tok) == 1) {
        if (tok[0] == 'p' || tok[0] == 'P') { FLUSH_POLY(); continue; }
        char *end = NULL;
        val = strtod(tok, &end);
        if (end == tok || *end != '\0') { rc = -2; goto done; }
        if (!have) {
            if (n >= 64) { rc = -3; goto done; }
            pts[n].x = val; have = 1;
        } else {
            pts[n].y = val; ++n; have = 0;
        }
    }
    FLUSH_POLY();
    if (s->npolys == 0) rc = -2;
done:
#undef FLUSH_POLY
    fclose(f);
    return rc;
}

static int parse_range(const char *s, int *lo, int *hi) {
    char *end = NULL;
    long a = strtol(s, &end, 10);
    if (end == s) return -1;
    long b = a;
    if (*end == '.' && end[1] == '.') {
        char *e2 = NULL;
        b = strtol(end + 2, &e2, 10);
        if (e2 == end + 2) return -1;
        if (*e2 != '\0') return -1;
    } else if (*end != '\0') {
        return -1;
    }
    *lo = (int)a; *hi = (int)b;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s scene.poly ref.pgm w h [sslo..sshi] [bitslo..bitshi] [min|max]\n",
                argv[0]);
        return 1;
    }
    const char *scene_path = argv[1];
    const char *ref_path = argv[2];
    int w = atoi(argv[3]), h = atoi(argv[4]);
    int ss_lo = PS_SS_MIN, ss_hi = PS_SS_MAX;
    int bits_lo = PS_BITS_MIN, bits_hi = PS_BITS_MAX;
    int prefer_min = 1;
    int positional = 0;
    for (int i = 5; i < argc; ++i) {
        if (!strcmp(argv[i], "min")) { prefer_min = 1; continue; }
        if (!strcmp(argv[i], "max")) { prefer_min = 0; continue; }
        int lo, hi;
        if (parse_range(argv[i], &lo, &hi) != 0) {
            fprintf(stderr, "bad range arg: %s\n", argv[i]);
            return 2;
        }
        if (positional == 0) { ss_lo = lo; ss_hi = hi; }
        else if (positional == 1) { bits_lo = lo; bits_hi = hi; }
        else { fprintf(stderr, "too many range args\n"); return 2; }
        ++positional;
    }

    char field[PS_FIELD_MAX];
    /* 先校验区间，越界直接拒绝并写清超的是哪一项。 */
    int rc = ps_validate_ss(ss_lo, field, sizeof field);
    if (rc == PS_OK) rc = ps_validate_ss(ss_hi, field, sizeof field);
    if (rc == PS_OK && ss_lo > ss_hi) { snprintf(field, sizeof field, "ss"); rc = PS_E_RANGE; }
    if (rc != PS_OK) {
        printf("REJECTED field=%s code=%d detail=%s (ss=%d..%d, 合法域 %d..%d)\n",
               field, rc, ps_strerror(rc), ss_lo, ss_hi, PS_SS_MIN, PS_SS_MAX);
        return 2;
    }
    rc = ps_validate_bits(bits_lo, field, sizeof field);
    if (rc == PS_OK) rc = ps_validate_bits(bits_hi, field, sizeof field);
    if (rc == PS_OK && bits_lo > bits_hi) { snprintf(field, sizeof field, "bits"); rc = PS_E_RANGE; }
    if (rc != PS_OK) {
        printf("REJECTED field=%s code=%d detail=%s (bits=%d..%d, 合法域 %d..%d)\n",
               field, rc, ps_strerror(rc), bits_lo, bits_hi, PS_BITS_MIN, PS_BITS_MAX);
        return 2;
    }

    RScene s;
    if (load_scene(scene_path, &s) != 0) {
        printf("REJECTED field=scene detail=场景读取失败\n");
        return 4;
    }
    if (s.npolys <= 0) { printf("REJECTED field=scene detail=空场景\n"); return 4; }

    unsigned char *ref = NULL;
    int rw = 0, rh = 0;
    rc = ps_load_pgm(ref_path, &ref, &rw, &rh, field, sizeof field);
    if (rc != PS_OK) {
        printf("REJECTED field=%s detail=%s code=%d\n", field, ps_strerror(rc), rc);
        return rc == PS_E_IO || rc == PS_E_PGM ? 4 : 6;
    }
    if (rw != w || rh != h) {
        printf("REJECTED field=ref detail=%s (ref %dx%d, 期望 %dx%d)\n",
               ps_strerror(PS_E_SIZE), rw, rh, w, h);
        free(ref);
        return 5;
    }

    RBox win;
    if (scene_bbox(&s, &win) != 0) { free(ref); return 4; }
    RPlace place = {0.0, 0.0, 0.0, 1.0};

    PSResult out;
    rc = ps_solve(&s, place, win, w, h, ref,
                  ss_lo, ss_hi, bits_lo, bits_hi, prefer_min, &out);
    if (rc != PS_OK) {
        printf("REJECTED code=%d detail=%s nearest_diff_bytes=%ld\n",
               rc, ps_strerror(rc), out.diff_bytes);
        free(ref);
        return rc == PS_E_NOMATCH ? 3 : 6;
    }

    /* 每行一个匹配组合，便于脚本比对与人工核对。 */
    for (int i = 0; i < out.nmatch; ++i)
        printf("MATCH ss=%d bits=%d diff_bytes=%ld\n",
               out.matches[i].ss, out.matches[i].bits, out.matches[i].diff_bytes);
    printf("CHOSEN ss=%d bits=%d diff_bytes=%ld matches=%d rule=%s%s\n",
           out.ss, out.bits, out.diff_bytes, out.nmatch,
           out.prefer_min ? "min(ss,bits)" : "max(ss,bits)",
           out.ambiguous ? " ambiguous" : "");

    /* 旧默认口径 (ss=4, bits=1) 与参照的差异字节：说明“两种设置差多少”。 */
    unsigned char *oldbuf = (unsigned char *)malloc((size_t)w * h);
    if (oldbuf) {
        if (ps_render(&s, place, win, w, h, 4, 1, oldbuf) == PS_OK)
            printf("LEGACY_DEFAULT ss=4 bits=1 diff_bytes=%ld\n",
                   ps_diff(oldbuf, ref, w * h));
        free(oldbuf);
    }
    for (int ss = ss_lo; ss <= ss_hi; ++ss)
        if (out.coverage_ms[ss] >= 0.0)
            printf("TIME ss=%d coverage_ms=%.2f\n", ss, out.coverage_ms[ss]);
    printf("TOTAL ms=%.2f\n", out.total_ms);
    free(ref);
    return 0;
}
