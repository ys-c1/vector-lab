#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#include "meta.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 固定字段顺序，任何调用方都不得重排。 */
static const char *const KEYS[5] = {
    "width", "height", "vertices", "sequence", "elapsed_ms"
};

#define SEQ_MAX 9007199254740992LL   /* 2^53，留足余量，永不回绕 */

long long meta_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    long long ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
    return ms;
}

int meta_validate(const RMeta *m) {
    if (!m) return RMETA_E_VALUE;
    if (m->width <= 0 || m->height <= 0) return RMETA_E_VALUE;
    if (m->vertices < 0) return RMETA_E_VALUE;
    if (m->sequence <= 0 || m->sequence > SEQ_MAX) return RMETA_E_VALUE;
    if (m->elapsed_ms < 0) return RMETA_E_VALUE;
    return 0;
}

static int write_tmp_replace(const char *path, const char *body, size_t len) {
    char tmp[256];
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof tmp) return RMETA_E_IO;
    FILE *f = fopen(tmp, "wb");
    if (!f) return RMETA_E_IO;
    int ok = fwrite(body, 1, len, f) == len;
    if (fflush(f) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return RMETA_E_IO; }
    if (rename(tmp, path) != 0) { remove(tmp); return RMETA_E_IO; }
    return 0;
}

int meta_seq_load(const char *path, long long *last, long long *peak) {
    if (!path || !last || !peak) return RMETA_E_VALUE;
    *last = 0;
    *peak = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) return 0;
        return RMETA_E_IO;
    }
    long long a = 0, b = 0;
    int c1 = fscanf(f, "%lld", &a);
    int c2 = fscanf(f, "%lld", &b);
    if (fclose(f) != 0) return RMETA_E_IO;
    if (c1 != 1 || c2 != 1 || a < 0 || b < 0 || a > SEQ_MAX || b > SEQ_MAX || b < a)
        return RMETA_E_PARSE;
    *last = a;
    *peak = b;
    return 0;
}

int meta_seq_next(const char *path, long long last, long long peak, long long *next) {
    if (!path || !next) return RMETA_E_VALUE;
    if (last < 0 || peak < 0 || last > SEQ_MAX || peak > SEQ_MAX || peak < last)
        return RMETA_E_VALUE;
    /* 以文件持久状态为唯一真值，拒绝调用方与文件不一致的任何认知 */
    long long flast = 0, fpeak = 0;
    int rc = meta_seq_load(path, &flast, &fpeak);
    if (rc != 0) return rc;
    if (last != flast || peak != fpeak) return RMETA_E_ORDER;
    if (peak >= SEQ_MAX) return RMETA_E_ORDER;       /* 再进一步即回绕 */
    long long v = last + 1;
    if (v <= last) return RMETA_E_ORDER;             /* 算术回绕兜底 */
    /* 高水位：只有正常递增（last==peak）才放行；last 被拨小后 v 落不回峰值之下 */
    if (last < peak) return RMETA_E_ORDER;
    long long newpeak = v;                           /* 此路径必有 v == peak+1 */
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%lld\n%lld\n", v, newpeak);
    if (n <= 0 || (size_t)n >= sizeof buf) return RMETA_E_IO;
    rc = write_tmp_replace(path, buf, (size_t)n);
    if (rc != 0) return rc;
    *next = v;
    return 0;
}

int meta_write(const char *path, const RMeta *m) {
    if (!path) return RMETA_E_VALUE;
    if (meta_validate(m) != 0) return RMETA_E_VALUE;
    char buf[512];
    int n = snprintf(buf, sizeof buf,
        "{\n"
        "  \"%s\": %d,\n"
        "  \"%s\": %d,\n"
        "  \"%s\": %d,\n"
        "  \"%s\": %lld,\n"
        "  \"%s\": %lld\n"
        "}\n",
        KEYS[0], m->width,
        KEYS[1], m->height,
        KEYS[2], m->vertices,
        KEYS[3], m->sequence,
        KEYS[4], m->elapsed_ms);
    if (n <= 0 || (size_t)n >= sizeof buf) return RMETA_E_IO;
    return write_tmp_replace(path, buf, (size_t)n);
}

/* 严格解析一个非负整数字面量：只接受 [-+]?digits，拒绝小数/指数/尾缀。 */
static int parse_int(const char *s, long long *out) {
    if (!s || !*s) return -1;
    const char *p = s;
    if (*p == '-' || *p == '+') ++p;
    if (!*p) return -1;
    for (const char *q = p; *q; ++q)
        if (!isdigit((unsigned char)*q)) return -1;
    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno != 0 || !end || *end != '\0') return -1;
    *out = v;
    return 0;
}

/* 取一行中 "key": value 之后的 token，去空白。
 * 只接受 '  "key": value,' 或 '  "key": value' 两种形态。 */
static int extract(const char *line, const char *key, char *tok, size_t cap) {
    size_t klen = strlen(key);
    const char *p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') return -1;
    ++p;
    if (strncmp(p, key, klen) != 0 || p[klen] != '"') return -1;
    p += klen + 1;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != ':') return -1;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    const char *start = p;
    while (*p && *p != ',' && *p != '\r' && *p != '\n') ++p;
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    size_t n = (size_t)(end - start);
    if (n == 0 || n + 1 >= cap) return -1;
    memcpy(tok, start, n);
    tok[n] = '\0';
    /* 值之后只允许：单个尾随逗号，或直接行尾 */
    if (*p == ',') ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p != '\0') return -1;
    return 0;
}

int meta_read(const char *path, RMeta *m) {
    if (!path || !m) return RMETA_E_VALUE;
    FILE *f = fopen(path, "rb");
    if (!f) return RMETA_E_IO;
    char line[256];
    long long val[5];
    int rc = 0;

    if (!fgets(line, sizeof line, f)) { rc = RMETA_E_PARSE; goto out; }
    if (strcmp(line, "{\n") != 0 && strcmp(line, "{") != 0) { rc = RMETA_E_PARSE; goto out; }

    for (int i = 0; i < 5; ++i) {
        if (!fgets(line, sizeof line, f)) { rc = RMETA_E_PARSE; goto out; }
        /* 行过长（无换行且未到 EOF）说明格式被篡改，拒绝 */
        if (strchr(line, '\n') == NULL && !feof(f)) { rc = RMETA_E_PARSE; goto out; }
        char tok[128];
        if (extract(line, KEYS[i], tok, sizeof tok) != 0) { rc = RMETA_E_PARSE; goto out; }
        if (parse_int(tok, &val[i]) != 0) { rc = RMETA_E_PARSE; goto out; }
        /* 前四行必须带尾随逗号，最后一行不带 */
        int has_comma = (strchr(line, ',') != NULL);
        if (i < 4 && !has_comma) { rc = RMETA_E_PARSE; goto out; }
        if (i == 4 && has_comma) { rc = RMETA_E_PARSE; goto out; }
    }
    if (!fgets(line, sizeof line, f)) { rc = RMETA_E_PARSE; goto out; }
    if (strcmp(line, "}\n") != 0 && strcmp(line, "}") != 0) { rc = RMETA_E_PARSE; goto out; }
    if (fgets(line, sizeof line, f) != NULL) { rc = RMETA_E_PARSE; goto out; }

    /* 防 int 截断：画幅/顶点数必须落在 int 范围内 */
    if (val[0] <= 0 || val[0] > INT_MAX || val[1] <= 0 || val[1] > INT_MAX ||
        val[2] < 0 || val[2] > INT_MAX || val[3] <= 0 || val[3] > SEQ_MAX ||
        val[4] < 0) { rc = RMETA_E_VALUE; goto out; }

    m->width = (int)val[0];
    m->height = (int)val[1];
    m->vertices = (int)val[2];
    m->sequence = val[3];
    m->elapsed_ms = val[4];
    if (meta_validate(m) != 0) { rc = RMETA_E_VALUE; goto out; }
out:
    fclose(f);
    return rc;
}
