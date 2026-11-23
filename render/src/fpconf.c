#include "fpconf.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* 三个受支持的配置项名字，顺序即快照顺序。 */
#define KEY_DET   "fp_deterministic"
#define KEY_FMA   "fma"
#define KEY_ORDER "eval_order"

static void seterr(char *err, size_t n, const char *fmt, ...) {
    if (!err || n == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* 去除尾部空白（不含换行，fgets 已按行读入）。 */
static void rstrip(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

int fpconf_parse(const char *path, RFPConf *out, char *err, size_t errsz) {
    if (!path || !out) return -1;
    FILE *f = fopen(path, "r");
    if (!f) { seterr(err, errsz, "config: cannot open '%s'", path); return -2; }

    int seen_det = 0, seen_fma = 0, seen_order = 0;
    int v_det = 0, v_fma = 0, v_order = 0;
    char line[256];
    int lineno = 0;

    while (fgets(line, sizeof line, f)) {
        ++lineno;
        rstrip(line);
        char *p = line;
        while (*p && isspace((unsigned char)*p)) ++p;
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) {
            seterr(err, errsz, "config line %d: expected key=value, got '%s'", lineno, line);
            fclose(f);
            return -3;
        }
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        size_t kn = strlen(key);
        while (kn && isspace((unsigned char)key[kn - 1])) key[--kn] = '\0';
        while (*val && isspace((unsigned char)*val)) ++val;

        if (key[0] == '\0') {
            seterr(err, errsz, "config line %d: empty key name", lineno);
            fclose(f); return -3;
        }
        /* 只接受 on/off，拒绝 on/off 之外的任何尾巴（如 onn、1、true）。 */
        int valb;
        if (!strcmp(val, "on")) valb = 1;
        else if (!strcmp(val, "off")) valb = 0;
        else if (!strcmp(key, KEY_ORDER) && (!strcmp(val, "fixed") || !strcmp(val, "default")))
            valb = !strcmp(val, "fixed");
        else {
            seterr(err, errsz, "config '%s': illegal value '%s' (allowed: on/off%s)",
                   key, val, !strcmp(key, KEY_ORDER) ? " or fixed/default" : "");
            fclose(f); return -4;
        }

        if (!strcmp(key, KEY_DET)) {
            if (seen_det) { seterr(err, errsz, "config '%s': duplicate key", key); fclose(f); return -5; }
            seen_det = 1; v_det = valb;
        } else if (!strcmp(key, KEY_FMA)) {
            if (seen_fma) { seterr(err, errsz, "config '%s': duplicate key", key); fclose(f); return -5; }
            seen_fma = 1; v_fma = valb;
        } else if (!strcmp(key, KEY_ORDER)) {
            if (seen_order) { seterr(err, errsz, "config '%s': duplicate key", key); fclose(f); return -5; }
            seen_order = 1; v_order = valb;
        } else {
            seterr(err, errsz, "config '%s': unknown key (allowed: %s, %s, %s)",
                   key, KEY_DET, KEY_FMA, KEY_ORDER);
            fclose(f); return -6;
        }
    }
    fclose(f);

    /* 三个键缺一不可，报错要指出具体名字。 */
    if (!seen_det || !seen_fma || !seen_order) {
        const char *missing = !seen_det ? KEY_DET : (!seen_fma ? KEY_FMA : KEY_ORDER);
        seterr(err, errsz, "config '%s': missing required key", missing);
        return -7;
    }

    /* 组合合法性：
     * 确定性开 -> fma 必须 off（禁用乘加合并），eval_order 必须 fixed；
     * 确定性关（回落基线）-> fma 必须 on，eval_order 必须 default。 */
    if (v_det) {
        if (v_fma) { seterr(err, errsz, "config '%s': must be off when fp_deterministic=on", KEY_FMA); return -8; }
        if (!v_order) { seterr(err, errsz, "config '%s': must be fixed when fp_deterministic=on", KEY_ORDER); return -8; }
    } else {
        if (!v_fma) { seterr(err, errsz, "config '%s': must be on when fp_deterministic=off (baseline)", KEY_FMA); return -8; }
        if (v_order) { seterr(err, errsz, "config '%s': must be default when fp_deterministic=off (baseline)", KEY_ORDER); return -8; }
    }

    out->fp_deterministic = v_det;
    out->fma = v_fma;
    out->eval_order = v_order;
    return 0;
}

void fpconf_snapshot_str(const RFPConf *c, char *buf, size_t n) {
    if (!c || !buf || n == 0) return;
    snprintf(buf, n, "%s=%s %s=%s %s=%s",
             KEY_DET, c->fp_deterministic ? "on" : "off",
             KEY_FMA, c->fma ? "on" : "off",
             KEY_ORDER, c->eval_order ? "fixed" : "default");
}

void fpconf_snapshot(const RFPConf *c, FILE *out) {
    char buf[160];
    fpconf_snapshot_str(c, buf, sizeof buf);
    fprintf(out, "%s\n", buf);
}
