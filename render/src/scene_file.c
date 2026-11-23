#include "scene_file.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SF_POLY_CAP 32
#define SF_VERT_CAP 64

const char *sf_code_name(int code) {
    switch (code) {
    case SF_OK:           return "OK";
    case SF_ERR_FILE:     return "E_FILE";
    case SF_ERR_VERSION:  return "E_VERSION";
    case SF_ERR_SYNTAX:   return "E_SYNTAX";
    case SF_ERR_MISSING:  return "E_MISSING";
    case SF_ERR_VALUE:    return "E_VALUE";
    case SF_ERR_CAPACITY: return "E_CAPACITY";
    default:              return "E_UNKNOWN";
    }
}

void scene_file_init(SceneFile *sf) {
    if (!sf) return;
    sf->version = 0;
    sf->width = sf->height = 0;
    sf->scale = 1.0;
    sf->has_canvas = 0;
    sf->has_version = 0;
    scene_init(&sf->scene);
}

static void fail(SFError *err, int code, const char *field, int line, const char *detail) {
    if (!err) return;
    err->code = code;
    err->field = field;
    err->line = line;
    err->detail = detail;
}

/* 字段名来自行缓冲（如未知关键字）时，拷贝进错误结构避免悬垂指针。 */
static void fail_dyn(SFError *err, int code, const char *field, int line, const char *detail) {
    if (!err) return;
    size_t n = strlen(field);
    if (n >= sizeof err->fbuf) n = sizeof err->fbuf - 1;
    memcpy(err->fbuf, field, n);
    err->fbuf[n] = '\0';
    fail(err, code, err->fbuf, line, detail);
}

static int parse_int_strict(const char *tok, int *out) {
    if (!tok || !*tok) return -1;
    char *end = NULL;
    long v = strtol(tok, &end, 10);
    if (!end || *end != '\0') return -1;
    *out = (int)v;
    return 0;
}

static int parse_double_strict(const char *tok, double *out) {
    if (!tok || !*tok) return -1;
    char *end = NULL;
    double v = strtod(tok, &end);
    if (!end || *end != '\0') return -1;
    if (!isfinite(v)) return -1;
    *out = v;
    return 0;
}

/* 按空白切分一行，最多 max 个 token；返回 token 数。 */
static int tokenize(char *line, char **toks, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        toks[n++] = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        if (*p) *p++ = '\0';
    }
    while (*p) {
        if (!isspace((unsigned char)*p)) {
            /* 多余 token：塞回最后一个槽位之外，由调用方按 -1 处理 */
            return -(n + 1);
        }
        ++p;
    }
    return n;
}

/* 读一行；超长行返回 SF_ERR_SYNTAX。返回值同 fgets（NULL 表示 EOF/错误）。 */
static char *read_line(char *buf, size_t cap, FILE *f, int *longline) {
    char *r = fgets(buf, (int)cap, f);
    *longline = 0;
    if (r) {
        size_t len = strlen(buf);
        if (len == cap - 1 && buf[len - 1] != '\n' && !feof(f))
            *longline = 1;
    }
    return r;
}

int scene_file_load(const char *path, SceneFile *sf, SFError *err) {
    if (!path || !sf) {
        fail(err, SF_ERR_FILE, "path", 0, "null argument");
        return SF_ERR_FILE;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        fail(err, SF_ERR_FILE, "path", 0, "cannot open scene file");
        return SF_ERR_FILE;
    }
    scene_file_init(sf);

    char line[SF_LINE_MAX];
    char *toks[SF_VERT_CAP * 2 + 4];
    int lineno = 0, in_poly = 0, cur_verts = 0;
    RPoint cur_pts[SF_VERT_CAP];
    int rc = SF_OK;
    int longline = 0;

    while (read_line(line, sizeof line, f, &longline)) {
        ++lineno;
        if (longline) {
            fail(err, SF_ERR_SYNTAX, "line", lineno, "line exceeds maximum length");
            rc = SF_ERR_SYNTAX;
            break;
        }
        int ntok = tokenize(line, toks, (int)(sizeof toks / sizeof toks[0]));
        if (ntok < 0) {
            fail(err, SF_ERR_SYNTAX, "line", lineno, "too many tokens on line");
            rc = SF_ERR_SYNTAX;
            break;
        }
        if (ntok == 0) continue;                 /* 空行 */
        if (toks[0][0] == '#') continue;         /* 注释行 */
        const char *kw = toks[0];

        if (in_poly) {
            if (strcmp(kw, "end") == 0) {
                if (ntok != 1) {
                    fail(err, SF_ERR_SYNTAX, "poly.end", lineno, "end takes no fields");
                    rc = SF_ERR_SYNTAX; break;
                }
                if (cur_verts < 3) {
                    fail(err, SF_ERR_VALUE, "poly.verts", lineno,
                         "poly needs at least 3 verts");
                    rc = SF_ERR_VALUE; break;
                }
                if (sf->scene.npolys >= SF_POLY_CAP) {
                    fail(err, SF_ERR_CAPACITY, "poly", lineno,
                         "scene holds at most 32 polys");
                    rc = SF_ERR_CAPACITY; break;
                }
                if (scene_add_poly(&sf->scene, cur_pts, cur_verts) != 0) {
                    fail(err, SF_ERR_CAPACITY, "poly", lineno, "poly rejected");
                    rc = SF_ERR_CAPACITY; break;
                }
                in_poly = 0;
                cur_verts = 0;
                continue;
            }
            /* 多边形体内：x y 顶点对 */
            if (ntok != 2) {
                fail(err, SF_ERR_SYNTAX, "poly.vert", lineno,
                     "poly body expects 'x y' or end");
                rc = SF_ERR_SYNTAX; break;
            }
            double x, y;
            if (parse_double_strict(toks[0], &x) ||
                parse_double_strict(toks[1], &y)) {
                fail(err, SF_ERR_VALUE, "poly.vert", lineno,
                     "coord must be a finite number");
                rc = SF_ERR_VALUE; break;
            }
            if (cur_verts >= SF_VERT_CAP) {
                fail(err, SF_ERR_CAPACITY, "poly.verts", lineno,
                     "poly holds at most 64 verts");
                rc = SF_ERR_CAPACITY; break;
            }
            cur_pts[cur_verts].x = x;
            cur_pts[cur_verts].y = y;
            ++cur_verts;
            continue;
        }

        if (strcmp(kw, "version") == 0) {
            if (ntok != 2) {
                fail(err, SF_ERR_SYNTAX, "version", lineno,
                     "version line expects 'version <int>'");
                rc = SF_ERR_SYNTAX; break;
            }
            if (sf->has_version) {
                fail(err, SF_ERR_VERSION, "version", lineno, "duplicate version");
                rc = SF_ERR_VERSION; break;
            }
            int v;
            if (parse_int_strict(toks[1], &v)) {
                fail(err, SF_ERR_VERSION, "version", lineno,
                     "version must be an integer");
                rc = SF_ERR_VERSION; break;
            }
            if (v != 1) {
                fail(err, SF_ERR_VERSION, "version", lineno, "unsupported version");
                rc = SF_ERR_VERSION; break;
            }
            sf->version = v;
            sf->has_version = 1;
        } else if (strcmp(kw, "canvas") == 0) {
            if (ntok != 4) {
                fail(err, SF_ERR_SYNTAX, "canvas", lineno,
                     "canvas expects 'canvas w h scale'");
                rc = SF_ERR_SYNTAX; break;
            }
            if (sf->has_canvas) {
                fail(err, SF_ERR_SYNTAX, "canvas", lineno, "duplicate canvas section");
                rc = SF_ERR_SYNTAX; break;
            }
            int w, h;
            double sc;
            if (parse_int_strict(toks[1], &w) || parse_int_strict(toks[2], &h)) {
                fail(err, SF_ERR_VALUE, "canvas.w", lineno,
                     "width/height must be integers");
                rc = SF_ERR_VALUE; break;
            }
            if (parse_double_strict(toks[3], &sc)) {
                fail(err, SF_ERR_VALUE, "canvas.scale", lineno,
                     "scale must be a finite number");
                rc = SF_ERR_VALUE; break;
            }
            if (w < 1 || w > SF_DIM_MAX || h < 1 || h > SF_DIM_MAX) {
                fail(err, SF_ERR_VALUE, "canvas.w", lineno,
                     "dimensions must be in 1..4096");
                rc = SF_ERR_VALUE; break;
            }
            if (sc == 0.0) {
                fail(err, SF_ERR_VALUE, "canvas.scale", lineno, "scale must be nonzero");
                rc = SF_ERR_VALUE; break;
            }
            sf->width = w;
            sf->height = h;
            sf->scale = sc;
            sf->has_canvas = 1;
        } else if (strcmp(kw, "poly") == 0) {
            if (ntok != 1) {
                fail(err, SF_ERR_SYNTAX, "poly", lineno, "poly takes no inline fields");
                rc = SF_ERR_SYNTAX; break;
            }
            in_poly = 1;
            cur_verts = 0;
        } else {
            fail_dyn(err, SF_ERR_SYNTAX, kw, lineno, "unknown section or keyword");
            rc = SF_ERR_SYNTAX; break;
        }
    }

    if (rc == SF_OK && ferror(f)) {
        fail(err, SF_ERR_FILE, "path", lineno, "read error");
        rc = SF_ERR_FILE;
    }
    fclose(f);
    if (rc != SF_OK) return rc;

    if (in_poly) {
        fail(err, SF_ERR_SYNTAX, "poly.end", lineno, "poly missing closing end");
        return SF_ERR_SYNTAX;
    }
    if (!sf->has_version) {
        fail(err, SF_ERR_VERSION, "version", lineno, "missing required version section");
        return SF_ERR_VERSION;
    }
    if (!sf->has_canvas) {
        fail(err, SF_ERR_MISSING, "canvas", lineno, "missing required canvas section");
        return SF_ERR_MISSING;
    }
    if (sf->scene.npolys == 0) {
        fail(err, SF_ERR_MISSING, "poly", lineno, "at least one poly section is required");
        return SF_ERR_MISSING;
    }
    if (err) err->code = SF_OK;
    return SF_OK;
}

int scene_file_load_legacy(const char *path, RScene *out, SFError *err) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fail(err, SF_ERR_FILE, "path", 0, "cannot open scene file");
        return SF_ERR_FILE;
    }
    scene_init(out);
    char tag[16];
    int rc = SF_OK;
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
        if (scene_add_poly(out, pts, n) != 0) {
            fail(err, SF_ERR_CAPACITY, "poly", 0, "poly rejected");
            rc = SF_ERR_CAPACITY;
            break;
        }
    }
    fclose(f);
    return rc;
}
