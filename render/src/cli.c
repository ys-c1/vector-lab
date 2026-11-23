#include "cli.h"
#include "scene.h"
#include "pipeline.h"
#include "export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RVEC_LEGACY_ONLY
/* ===================== 新子命令入口 ===================== */

/* ------------------------------------------------------------------ */
/* 参数规格表：解析与帮助都由同一张表驱动，帮助文本不可能与参数表脱节。 */
/* ------------------------------------------------------------------ */

typedef enum { OPT_STR, OPT_INT, OPT_FLAG } OptKind;

typedef struct {
    const char *name;      /* 含前导 "--"，或 "scene" 表示位置参数 */
    OptKind     kind;
    int         min, max;  /* int 选项闭区间；flag/str 忽略 */
    int         required;  /* 非 flag 选项是否必填；必填项帮助不加方括号 */
    const char *help;      /* 帮助行，参数表与 --help 共用 */
} OptSpec;

typedef struct {
    const char  *name;
    const char  *summary;
    const OptSpec *opts;
    int           nopts;
    int (*run)(const CliArgs *a);
} Command;

static const OptSpec g_parse_opts[] = {
    { "scene", OPT_STR, 0, 0, 1, "场景文件路径（位置参数，必填）" },
};
static const OptSpec g_diag_opts[] = {
    { "scene",   OPT_STR,  0, 0, 1, "场景文件路径（位置参数，必填）" },
    { "--verbose", OPT_FLAG, 0, 0, 0, "逐条打印全部校验结果" },
};
static const OptSpec g_render_opts[] = {
    { "scene", OPT_STR,  0, 0, 1, "场景文件路径（位置参数，必填）" },
    { "--ss",  OPT_INT,  1, 8, 0, "超采样阶数（1..8，默认 4）" },
};
static const OptSpec g_export_opts[] = {
    { "scene",   OPT_STR,  0, 0, 1, "场景文件路径（位置参数，必填）" },
    { "--out",   OPT_STR,  0, 0, 1, "输出 PGM 路径（必填）" },
    { "--ss",    OPT_INT,  1, 8, 0, "超采样阶数（1..8，默认 4）" },
    { "--force", OPT_FLAG, 0, 0, 0, "允许覆盖已存在的输出文件" },
};

static int cmd_parse(const CliArgs *a);
static int cmd_diag(const CliArgs *a);
static int cmd_render(const CliArgs *a);
static int cmd_export(const CliArgs *a);

static const Command g_cmds[] = {
    { "parse",   "解析并校验场景文件，打印结构摘要", g_parse_opts,  1, cmd_parse },
    { "diagnose","对场景文件跑完整诊断，逐条报告必填段与字段", g_diag_opts, 2, cmd_diag },
    { "render",  "渲染到内存并报告统计，不写文件", g_render_opts,  2, cmd_render },
    { "export",  "渲染并导出 PGM 文件", g_export_opts, 4, cmd_export },
};
#define NCMDS (int)(sizeof g_cmds / sizeof g_cmds[0])

/* ------------------------------------------------------------------ */
/* 错误输出                                                            */
/* ------------------------------------------------------------------ */

static const char *exit_code_name(int code) {
    switch (code) {
    case EXIT_USAGE:     return "E_USAGE";
    case EXIT_BAD_FIELD: return "E_FIELD";
    case EXIT_OUTPUT:    return "E_OUTPUT";
    default:             return sf_code_name(code);
    }
}

void cli_report_error(int code, const char *field, int line, const char *detail) {
    fprintf(stderr, "error %s field=%s line=%d blocked=yes: %s\n",
            exit_code_name(code), field ? field : "-", line,
            detail ? detail : "");
}

static int fail_usage(const char *field, const char *detail) {
    cli_report_error(EXIT_USAGE, field, 0, detail);
    return EXIT_USAGE;
}

static int fail_field(const char *field, const char *detail) {
    cli_report_error(EXIT_BAD_FIELD, field, 0, detail);
    return EXIT_BAD_FIELD;
}

static int report_sf(const SFError *e) {
    cli_report_error(e->code, e->field, e->line, e->detail);
    return e->code;   /* SF 码 1..6 即退出码 */
}

/* ------------------------------------------------------------------ */
/* 帮助：全部从 g_cmds / OptSpec 生成                                  */
/* ------------------------------------------------------------------ */

static void print_main_help(const char *prog) {
    printf("usage: %s <subcommand> <scene> [options]\n", prog);
    printf("子命令：\n");
    for (int i = 0; i < NCMDS; ++i)
        printf("  %-9s %s\n", g_cmds[i].name, g_cmds[i].summary);
    printf("通用：%s <subcommand> --help 查看该子命令参数表\n", prog);
    printf("旧位置参数：RVEC_LEGACY=1 %s scene.txt out.pgm w h scale\n", prog);
}

static void print_cmd_help(const char *prog, const Command *c) {
    printf("usage: %s %s", prog, c->name);
    for (int i = 0; i < c->nopts; ++i) {
        const OptSpec *o = &c->opts[i];
        if (strcmp(o->name, "scene") == 0) printf(" <scene>");
        else if (o->kind == OPT_FLAG)      printf(" [%s]", o->name);
        else if (o->kind == OPT_INT)
            printf(o->required ? " %s <int>" : " [%s <int>]", o->name);
        else
            printf(o->required ? " %s <path>" : " [%s <path>]", o->name);
    }
    printf("\n%s\n", c->summary);
    printf("参数表：\n");
    for (int i = 0; i < c->nopts; ++i) {
        const OptSpec *o = &c->opts[i];
        const char *req = (o->kind == OPT_FLAG || !o->required) ? "可选" : "必填";
        if (o->kind == OPT_INT)
            printf("  %-9s int  %s [%d..%d]  %s\n",
                   o->name, req, o->min, o->max, o->help);
        else if (o->kind == OPT_FLAG)
            printf("  %-9s flag %s          %s\n", o->name, req, o->help);
        else
            printf("  %-9s path %s          %s\n", o->name, req, o->help);
    }
}

static const Command *find_cmd(const char *name) {
    for (int i = 0; i < NCMDS; ++i)
        if (strcmp(g_cmds[i].name, name) == 0) return &g_cmds[i];
    return NULL;
}

static const OptSpec *find_opt(const Command *c, const char *name) {
    for (int i = 0; i < c->nopts; ++i)
        if (strcmp(c->opts[i].name, name) == 0) return &c->opts[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* 参数解析                                                            */
/* ------------------------------------------------------------------ */

static int parse_int_value(const char *s, int *out) {
    if (!s || !*s) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int parse_args(const Command *c, int argc, char **argv, CliArgs *args) {
    args->scene_path = NULL;
    args->out_path = NULL;
    args->ss = 4;
    args->force = 0;
    args->verbose = 0;
    int seen_scene = 0;

    for (int i = 0; i < argc; ++i) {
        char *tok = argv[i];
        if (strcmp(tok, "--help") == 0 || strcmp(tok, "-h") == 0) {
            print_cmd_help("rvec", c);
            exit(EXIT_OK);
        }
        if (tok[0] == '-' && tok[0] != '\0') {
            const OptSpec *o = find_opt(c, tok);
            if (!o) return fail_usage(tok, "unknown option for this subcommand");
            if (o->kind == OPT_FLAG) {
                if (strcmp(tok, "--force") == 0) args->force = 1;
                if (strcmp(tok, "--verbose") == 0) args->verbose = 1;
                continue;
            }
            if (i + 1 >= argc)
                return fail_usage(o->name, "option requires a value");
            const char *val = argv[++i];
            if (o->kind == OPT_INT) {
                int v;
                if (parse_int_value(val, &v))
                    return fail_field(o->name, "value must be an integer");
                if (v < o->min || v > o->max)
                    return fail_field(o->name, "value out of allowed range");
                if (strcmp(o->name, "--ss") == 0) args->ss = v;
            } else {
                if (strcmp(o->name, "--out") == 0) {
                    if (args->out_path)
                        return fail_usage(o->name, "option given more than once");
                    args->out_path = val;
                }
            }
            continue;
        }
        /* 位置参数：仅允许一个 scene */
        if (find_opt(c, "scene") == NULL)
            return fail_usage("scene", "this subcommand takes no positional args");
        if (seen_scene)
            return fail_usage("scene", "multiple scene files given");
        args->scene_path = tok;
        seen_scene = 1;
    }

    if (!args->scene_path)
        return fail_usage("scene", "missing required positional arg <scene>");
    if (find_opt(c, "--out") && !args->out_path)
        return fail_usage("--out", "missing required option --out <path>");
    return EXIT_OK;
}

/* ------------------------------------------------------------------ */
/* 共享渲染                                                            */
/* ------------------------------------------------------------------ */

int cli_render_buffer(const SceneFile *sf, int ss, unsigned char **buf) {
    *buf = NULL;
    RBox win;
    if (scene_bbox(&sf->scene, &win) != 0) {
        cli_report_error(SF_ERR_MISSING, "poly", 0, "scene has no geometry");
        return SF_ERR_MISSING;
    }
    size_t n = (size_t)sf->width * (size_t)sf->height;
    unsigned char *b = (unsigned char *)malloc(n ? n : 1);
    if (!b) {
        cli_report_error(EXIT_BAD_FIELD, "canvas.w", 0, "buffer allocation failed");
        return EXIT_BAD_FIELD;
    }
    RPlace place = {0.0, 0.0, 0.0, sf->scale};
    int rc = pipeline_render_ss(&sf->scene, place, win,
                                sf->width, sf->height, ss, b);
    if (rc != 0) {
        free(b);
        cli_report_error(EXIT_BAD_FIELD, "canvas.scale", 0, "pipeline rejected scene");
        return EXIT_BAD_FIELD;
    }
    *buf = b;
    return EXIT_OK;
}

static int load_scene(const CliArgs *a, SceneFile *sf, SFError *err) {
    int rc = scene_file_load(a->scene_path, sf, err);
    if (rc != SF_OK) return report_sf(err);
    return EXIT_OK;
}

/* ------------------------------------------------------------------ */
/* 子命令实现                                                          */
/* ------------------------------------------------------------------ */

static int cmd_parse(const CliArgs *a) {
    SceneFile sf; SFError err;
    int rc = load_scene(a, &sf, &err);
    if (rc != EXIT_OK) return rc;
    printf("scene %s version=%d canvas=%dx%d scale=%g polys=%d verts=%d\n",
           a->scene_path, sf.version, sf.width, sf.height, sf.scale,
           sf.scene.npolys, scene_count_verts(&sf.scene));
    return EXIT_OK;
}

static int cmd_diag(const CliArgs *a) {
    SceneFile sf; SFError err;
    int rc = scene_file_load(a->scene_path, &sf, &err);
    struct DiagRow { const char *field; int good; const char *msg; };
    struct DiagRow rows[7];
    int checks = 0, passed = 0;
    #define ROW(cond, fld, txt) do { \
        int _g = (cond); \
        rows[checks] = (struct DiagRow){ (fld), _g, (txt) }; \
        ++checks; if (_g) ++passed; } while (0)

    ROW(rc == SF_OK || err.code != SF_ERR_VERSION, "version",
        "version section present and supported");
    ROW(rc == SF_OK || err.code != SF_ERR_MISSING ||
        strcmp(err.field, "canvas") != 0, "canvas", "canvas section present");
    ROW(rc == SF_OK || err.code != SF_ERR_MISSING ||
        strcmp(err.field, "poly") != 0, "poly", "at least one poly section");
    ROW(rc == SF_OK || err.code != SF_ERR_SYNTAX, "syntax", "all sections well-formed");
    ROW(rc == SF_OK || err.code != SF_ERR_VALUE, "values",
        "all field values in range and finite");
    ROW(rc == SF_OK || err.code != SF_ERR_CAPACITY, "capacity",
        "within poly/vert capacity");
    ROW(rc == SF_OK || err.code != SF_ERR_FILE, "path", "scene file readable");
    #undef ROW

    if (a->verbose) {
        for (int i = 0; i < checks; ++i)
            printf("[%s] field=%s: %s\n",
                   rows[i].good ? "PASS" : "FAIL", rows[i].field, rows[i].msg);
    }
    if (rc != SF_OK) {
        cli_report_error(err.code, err.field, err.line, err.detail);
        printf("diagnose %s: %d/%d checks passed, blocked=yes\n",
               a->scene_path, passed, checks);
        return err.code;
    }
    printf("diagnose %s: %d/%d checks passed, blocked=no\n",
           a->scene_path, passed, checks);
    return EXIT_OK;
}

static int cmd_render(const CliArgs *a) {
    SceneFile sf; SFError err;
    int rc = load_scene(a, &sf, &err);
    if (rc != EXIT_OK) return rc;
    unsigned char *buf = NULL;
    rc = cli_render_buffer(&sf, a->ss, &buf);
    if (rc != EXIT_OK) return rc;
    printf("render %s %dx%d ss=%d polys=%d verts=%d on=%d blocked=no\n",
           a->scene_path, sf.width, sf.height, a->ss,
           sf.scene.npolys, scene_count_verts(&sf.scene),
           export_count_on(buf, sf.width, sf.height));
    free(buf);
    return EXIT_OK;
}

static int cmd_export(const CliArgs *a) {
    SceneFile sf; SFError err;
    int rc = load_scene(a, &sf, &err);
    if (rc != EXIT_OK) return rc;
    if (!a->force) {
        FILE *probe = fopen(a->out_path, "rb");
        if (probe) {
            fclose(probe);
            return fail_field("--out", "output exists; pass --force to overwrite");
        }
    }
    unsigned char *buf = NULL;
    rc = cli_render_buffer(&sf, a->ss, &buf);
    if (rc != EXIT_OK) return rc;
    if (export_pgm(a->out_path, buf, sf.width, sf.height) != 0) {
        free(buf);
        cli_report_error(EXIT_OUTPUT, "--out", 0, "cannot write output file");
        return EXIT_OUTPUT;
    }
    printf("export %s -> %s %dx%d ss=%d on=%d blocked=no\n",
           a->scene_path, a->out_path, sf.width, sf.height, a->ss,
           export_count_on(buf, sf.width, sf.height));
    free(buf);
    return EXIT_OK;
}

/* ------------------------------------------------------------------ */
/* 旧位置参数入口（编译期 RVEC_LEGACY_ONLY 或运行期 RVEC_LEGACY=1）     */
/* ------------------------------------------------------------------ */
#endif /* !RVEC_LEGACY_ONLY */

static int legacy_main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
    RScene s;
    scene_init(&s);
    char tag[16];
    while (fscanf(f, "%15s", tag) == 1) {
        if (tag[0] != 'p') break;
        RPoint pts[64];
        int n = 0;
        while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) {
            ++n;
            int c = fgetc(f);
            if (c == '\n' || c == EOF) break;
            ungetc(c, f);
        }
        if (scene_add_poly(&s, pts, n) != 0) { fclose(f); return 3; }
    }
    fclose(f);
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
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

int main(int argc, char **argv) {
#ifdef RVEC_LEGACY_ONLY
    return legacy_main(argc, argv);
#else
    if (getenv("RVEC_LEGACY") != NULL)
        return legacy_main(argc, argv);

    if (argc < 2) {
        print_main_help("rvec");
        return EXIT_USAGE;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_main_help("rvec");
        return EXIT_OK;
    }
    const Command *c = find_cmd(argv[1]);
    if (!c) {
        /* 未识别的首参不静默回退旧入口，避免错误被吞掉 */
        return fail_usage(argv[1], "unknown subcommand");
    }
    CliArgs args;
    int rc = parse_args(c, argc - 2, argv + 2, &args);
    if (rc != EXIT_OK) return rc;
    return c->run(&args);
#endif
}
