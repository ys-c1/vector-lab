#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 解析/输出一律走 "C" 语言环境：小数点固定为 '.'，不读调用方的 LC_NUMERIC。
 * 否则在 de_DE 等 locale 下 "10.5" 只解析出 10，场景被悄悄改写。
 * 锁在进程入口而不是依赖环境变量，命令行字面量因此处处等价。 */
static void pin_numeric_locale(void) {
    setlocale(LC_ALL, "C");
}

/* 只认 ASCII 空白：isspace 的分类表来自 LC_CTYPE，随语言环境变化。 */
static int ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* 整串严格解析：拒绝空串、尾随垃圾、溢出。atoi/atof 会把 "160x" 读成
 * 160、把 "1,25" 读成 1，这类静默截断同样属于“环境不同、结果不同”。 */
static int parse_int(const char *s, int *out) {
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (!s || end == s || *end != '\0' || errno == ERANGE) return -1;
    if (v < -2147483647L - 1L || v > 2147483647L) return -1;
    *out = (int)v;
    return 0;
}

/* 场景里只接受 ASCII 十进制：[-+]digits[.digits][e[+-]digits]。
 * strtod 还认 "inf"/"nan"/十六进制浮点，那些值进入坐标后会让比较与
 * 渲染结果依赖实现细节，这里一律拒绝。 */
static int ascii_decimal(const char *s, size_t n) {
    size_t i = 0;
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    size_t int_digits = 0, frac_digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++int_digits; }
    if (i < n && s[i] == '.') { ++i; while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++frac_digits; } }
    if (int_digits + frac_digits == 0) return 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        size_t exp_digits = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++exp_digits; }
        if (exp_digits == 0) return 0;
    }
    return i == n;
}

/* 从 p 读一个坐标，返回新位置；不合法返回 NULL。 */
static const char *read_coord(const char *p, double *out) {
    const char *start = p;
    if (*p == '+' || *p == '-') ++p;
    while (*p && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' ||
                  ((*p == '+' || *p == '-') && (p[-1] == 'e' || p[-1] == 'E')))) ++p;
    if (p == start || !ascii_decimal(start, (size_t)(p - start))) return NULL;
    char tmp[64];
    size_t len = (size_t)(p - start);
    if (len >= sizeof tmp) return NULL;
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *end = NULL;
    errno = 0;
    double v = strtod(tmp, &end);
    if (end != tmp + len || errno == ERANGE) return NULL;
    *out = v;
    return p;
}

static int parse_double(const char *s, double *out) {
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (!s || end == s || *end != '\0' || errno == ERANGE) return -1;
    *out = v;
    return 0;
}

static int parse_size(const char *s, int *out) {
    if (parse_int(s, out) != 0 || *out <= 0) return -1;
    return 0;
}

int main(int argc, char **argv) {
    pin_numeric_locale();
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale\n", argv[0]);
        return 1;
    }
    int w = 0, h = 0;
    double sc = 0.0;
    if (parse_size(argv[3], &w) != 0 || parse_size(argv[4], &h) != 0) {
        fprintf(stderr, "bad size '%s' '%s' (want positive integers)\n", argv[3], argv[4]);
        return 1;
    }
    if (!ascii_decimal(argv[5], strlen(argv[5])) ||
        parse_double(argv[5], &sc) != 0 || sc == 0.0) {
        fprintf(stderr, "bad scale '%s' (want non-zero decimal, '.' as separator)\n", argv[5]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
    RScene s;
    scene_init(&s);
    char line[4096];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        ++lineno;
        const char *p = line;
        while (*p && ascii_space((unsigned char)*p)) ++p;
        if (!*p || *p == '#') continue;
        if (*p != 'p') {
            fprintf(stderr, "line %d: expected 'p', got '%c'\n", lineno, *p);
            fclose(f);
            return 3;
        }
        ++p;
        RPoint pts[64];
        int n = 0;
        for (;;) {
            while (*p && ascii_space((unsigned char)*p)) ++p;
            if (!*p) break;
            if (n >= 64) { fprintf(stderr, "line %d: too many vertices\n", lineno); fclose(f); return 3; }
            double x = 0.0, y = 0.0;
            const char *next = read_coord(p, &x);
            if (!next) {
                fprintf(stderr, "line %d: bad number at '%s'\n", lineno, p);
                fclose(f);
                return 3;
            }
            p = next;
            while (*p && ascii_space((unsigned char)*p)) ++p;
            next = read_coord(p, &y);
            if (!next) {
                fprintf(stderr, "line %d: vertex %d missing/invalid y\n", lineno, n);
                fclose(f);
                return 3;
            }
            p = next;
            pts[n].x = x;
            pts[n].y = y;
            ++n;
        }
        if (scene_add_poly(&s, pts, n) != 0) {
            fprintf(stderr, "line %d: polygon %d has %d vertices (need 3..64)\n", lineno, s.npolys, n);
            fclose(f);
            return 3;
        }
    }
    if (ferror(f)) { fclose(f); return 2; }
    fclose(f);
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
