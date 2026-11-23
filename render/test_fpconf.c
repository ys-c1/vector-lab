#include "src/fpconf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static char TDIR[256];

static void writefile(const char *name, const char *body) {
    char path[320];
    snprintf(path, sizeof path, "%s/%s", TDIR, name);
    FILE *f = fopen(path, "wb");
    fputs(body, f);
    fclose(f);
}

/* 期望解析成功且字段符合 */
static void ok(const char *name, int det, int fma, int order) {
    char path[320], err[256] = {0};
    RFPConf c;
    snprintf(path, sizeof path, "%s/%s", TDIR, name);
    int rc = fpconf_parse(path, &c, err, sizeof err);
    CK(rc == 0);
    CK(c.fp_deterministic == det && c.fma == fma && c.eval_order == order);
}

/* 期望解析失败，且报错里出现配置名 key */
static void bad(const char *name, const char *key) {
    char path[320], err[256] = {0};
    RFPConf c;
    snprintf(path, sizeof path, "%s/%s", TDIR, name);
    int rc = fpconf_parse(path, &c, err, sizeof err);
    CK(rc != 0);
    CK(strstr(err, key) != NULL);
}

int main(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = ".";
    snprintf(TDIR, sizeof TDIR, "%s/fpconf_test", base);
    mkdir(TDIR, 0755);  /* 已存在则忽略 EEXIST */

    writefile("on.conf",  "fp_deterministic=on\nfma=off\neval_order=fixed\n");
    writefile("off.conf", "fp_deterministic=off\nfma=on\neval_order=default\n");
    writefile("spaced.conf", "  fp_deterministic = on \n fma = off \n eval_order = fixed \n# comment\n\n");
    writefile("missing.conf", "fp_deterministic=on\nfma=off\n");
    writefile("illegal.conf", "fp_deterministic=on\nfma=off\neval_order=yes\n");
    writefile("fmaill.conf", "fp_deterministic=on\nfma=maybe\neval_order=fixed\n");
    writefile("unknown.conf", "fp_deterministic=on\nfma=off\neval_order=fixed\ncontract=off\n");
    writefile("dup.conf", "fp_deterministic=on\nfma=off\neval_order=fixed\nfma=off\n");
    writefile("combo1.conf", "fp_deterministic=on\nfma=on\neval_order=fixed\n");
    writefile("combo2.conf", "fp_deterministic=off\nfma=on\neval_order=fixed\n");
    writefile("combo3.conf", "fp_deterministic=off\nfma=off\neval_order=default\n");
    writefile("malformed.conf", "fp_deterministic_on_no_equals\n");
    writefile("emptyval.conf", "fp_deterministic=\nfma=off\neval_order=fixed\n");

    ok("on.conf", 1, 0, 1);
    ok("off.conf", 0, 1, 0);
    ok("spaced.conf", 1, 0, 1);

    bad("missing.conf", "eval_order");
    bad("illegal.conf", "eval_order");
    bad("fmaill.conf", "fma");
    bad("unknown.conf", "contract");
    bad("dup.conf", "fma");
    bad("combo1.conf", "fma");
    bad("combo2.conf", "eval_order");
    bad("combo3.conf", "fma");
    bad("malformed.conf", "fp_deterministic_on_no_equals");
    bad("emptyval.conf", "fp_deterministic");

    /* 快照文本稳定 */
    RFPConf c = {1, 0, 1};
    char snap[160];
    fpconf_snapshot_str(&c, snap, sizeof snap);
    CK(strcmp(snap, "fp_deterministic=on fma=off eval_order=fixed") == 0);

    printf("%d checks, %d failures (tmp=%s)\n", checks, fails, TDIR);
    return fails ? 1 : 0;
}
