#include "contour.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_CAP 256

const char *ct_strerror(int code) {
    switch (code) {
        case CT_OK:           return "ok";
        case CT_ERR_IO:       return "cannot open input";
        case CT_ERR_FORMAT:   return "bad line format";
        case CT_ERR_TOOFEW:   return "fewer than 3 points in layer";
        case CT_ERR_COLLINEAR:return "all points collinear in layer";
        case CT_ERR_GAP:      return "layer number gap or out of order";
        case CT_ERR_CAP:      return "too many layers";
        case CT_ERR_MEM:      return "memory exceeds 6x input";
        case CT_ERR_MONO:     return "areas not non-increasing";
        default:              return "unknown error";
    }
}

/* 非破坏性取行：把当前行（不含换行）拷进 buf，推进 *pp。
 * 行超长置 *toolong=1（调用方按格式错误拒绝），EOF 返回 NULL。*/
static const char *get_line(char **pp, char *buf, int *toolong) {
    char *p = *pp;
    if (!p || !*p) return NULL;
    char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    if (len && p[len - 1] == '\r') --len;
    *toolong = 0;
    if (len >= LINE_CAP) { len = LINE_CAP - 1; *toolong = 1; }
    memcpy(buf, p, len);
    buf[len] = '\0';
    *pp = nl ? nl + 1 : NULL;
    return buf;
}

static int blank(const char *s) {
    for (; *s; ++s) if (!isspace((unsigned char)*s)) return 0;
    return 1;
}

static int is_header(const char *s) {
    return s[0] == 'L' && isspace((unsigned char)s[1]);
}

/* 计数遍：统计层数与顶点数，同时做全部语法/编号校验，不修改原文。
 * expect 为下一个应出现的层号；*cur 为当前所在层（-1 = 未进层）。*/
static int count_pass(const char *text, int *n_layers, long *n_points,
                      int expect, CTError *err) {
    int layers = 0, cur = -1;
    long points = 0;
    char *p = (char*)text, buf[LINE_CAP];
    int toolong;
    while (get_line(&p, buf, &toolong)) {
        const char *line = buf;
        if (blank(line)) continue;
        if (toolong) { err->code = CT_ERR_FORMAT; err->layer = cur; return -1; }
        if (is_header(line)) {
            int id;
            char tail;
            if (sscanf(line, "L %d %c", &id, &tail) != 1) {
                err->code = CT_ERR_FORMAT; err->layer = cur; return -1;
            }
            if (id != expect) {
                err->code = CT_ERR_GAP; err->layer = expect; return -1;
            }
            if (layers >= CT_MAX_LAYERS) {
                err->code = CT_ERR_CAP; err->layer = expect; return -1;
            }
            cur = id;
            expect = id + 1;
            ++layers;
        } else {
            double x, y;
            char tail;
            if (cur < 0) {
                err->code = CT_ERR_FORMAT; err->layer = -1; return -1;
            }
            if (sscanf(line, "%lf %lf %c", &x, &y, &tail) != 2) {
                err->code = CT_ERR_FORMAT; err->layer = cur; return -1;
            }
            ++points;
        }
    }
    *n_layers = layers;
    *n_points = points;
    return 0;
}

int contour_load(const char *path, CTSet *set, CTError *err) {
    if (err) { err->code = CT_OK; err->layer = -1; }
    if (!path || !set) return CT_ERR_FORMAT;
    memset(set, 0, sizeof(*set));

    FILE *f = fopen(path, "rb");
    if (!f) { if (err) err->code = CT_ERR_IO; return CT_ERR_IO; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); if (err) err->code = CT_ERR_IO; return CT_ERR_IO; }
    long size = ftell(f);
    if (size < 0) { fclose(f); if (err) err->code = CT_ERR_IO; return CT_ERR_IO; }
    rewind(f);

    char *text = (char*)malloc((size_t)size + 1);
    if (!text) { fclose(f); if (err) err->code = CT_ERR_MEM; return CT_ERR_MEM; }
    long got = (long)fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = '\0';
    set->text = text;
    set->input_bytes = size;
    set->peak_bytes = got + 1;

    int n_layers = 0;
    long n_points = 0;
    if (count_pass(text, &n_layers, &n_points, 0, err) != 0) {
        int code = err ? err->code : CT_ERR_FORMAT;
        contour_free(set);
        return code;
    }
    if (n_layers == 0) {
        if (err) { err->code = CT_ERR_FORMAT; err->layer = -1; }
        contour_free(set);
        return CT_ERR_FORMAT;
    }

    /* 硬性内存红线：顶点池 16 字节/点，连同文件缓冲不得超过输入 6 倍。
     * 点数来自计数遍的精确值，池按实际顶点数一次性分配。*/
    long peak = set->peak_bytes + (long)(n_points * (long)sizeof(CTPoint));
    if (peak > 6 * size) {
        if (err) err->code = CT_ERR_MEM;
        contour_free(set);
        return CT_ERR_MEM;
    }

    CTPoint *pool = NULL;
    if (n_points > 0) {
        pool = (CTPoint*)malloc((size_t)n_points * sizeof(CTPoint));
        if (!pool) { if (err) err->code = CT_ERR_MEM; contour_free(set); return CT_ERR_MEM; }
    }
    set->pool = pool;
    set->pool_n = (size_t)n_points;
    set->peak_bytes = peak;

    /* 填充遍：编号在计数遍已校验，这里只按序落池，原文未被破坏。*/
    int li = -1;
    long off = 0;
    int layer_count = 0;
    char *q = text, buf[LINE_CAP];
    int toolong;
    while (get_line(&q, buf, &toolong)) {
        const char *line = buf;
        if (blank(line)) continue;
        if (is_header(line)) {
            if (li >= 0) set->cs[li].count = layer_count;
            ++li;
            layer_count = 0;
            set->cs[li].layer = li;
            set->cs[li].count = 0;
            set->cs[li].pts = pool + off;
        } else {
            double x, y;
            if (sscanf(line, "%lf %lf", &x, &y) != 2) {
                if (err) { err->code = CT_ERR_FORMAT; err->layer = li; }
                contour_free(set);
                return CT_ERR_FORMAT;
            }
            pool[off].x = x;
            pool[off].y = y;
            ++off;
            ++layer_count;
        }
    }
    if (li >= 0) set->cs[li].count = layer_count;
    set->layers = li + 1;
    return CT_OK;
}

int contour_validate(const CTSet *set, CTError *err) {
    if (err) { err->code = CT_OK; err->layer = -1; }
    if (!set) { if (err) err->code = CT_ERR_FORMAT; return CT_ERR_FORMAT; }
    for (int l = 0; l < set->layers; ++l) {
        const CTContour *c = &set->cs[l];
        if (c->count < 3) {
            if (err) { err->code = CT_ERR_TOOFEW; err->layer = c->layer; }
            return CT_ERR_TOOFEW;
        }
        double maxx = c->pts[0].x, minx = c->pts[0].x;
        double maxy = c->pts[0].y, miny = c->pts[0].y;
        for (int i = 1; i < c->count; ++i) {
            double x = c->pts[i].x, y = c->pts[i].y;
            if (x > maxx) maxx = x;
            if (x < minx) minx = x;
            if (y > maxy) maxy = y;
            if (y < miny) miny = y;
        }
        double span = maxx - minx;
        if (maxy - miny > span) span = maxy - miny;
        double eps = span > 0.0 ? span * 1e-12 : 0.0;
        const CTPoint *o = &c->pts[0];
        int collinear = 1;
        for (int i = 1; i + 1 < c->count; ++i) {
            double cr = (c->pts[i].x - o->x) * (c->pts[i+1].y - o->y)
                      - (c->pts[i].y - o->y) * (c->pts[i+1].x - o->x);
            if (fabs(cr) > eps) { collinear = 0; break; }
        }
        if (collinear) {
            if (err) { err->code = CT_ERR_COLLINEAR; err->layer = c->layer; }
            return CT_ERR_COLLINEAR;
        }
    }
    return CT_OK;
}

double contour_area(const CTPoint *pts, int n) {
    if (!pts || n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const CTPoint *a = &pts[i], *b = &pts[(i + 1) % n];
        s += a->x * b->y - b->x * a->y;
    }
    return fabs(s) * 0.5;
}

int contour_check_mono(const CTSet *set, const double *areas, CTError *err) {
    if (err) { err->code = CT_OK; err->layer = -1; }
    if (!set || !areas) { if (err) err->code = CT_ERR_FORMAT; return CT_ERR_FORMAT; }
    double scale = 0.0;
    for (int l = 0; l < set->layers; ++l)
        if (fabs(areas[l]) > scale) scale = fabs(areas[l]);
    double eps = scale > 0.0 ? scale * 1e-9 : 0.0;
    for (int l = 1; l < set->layers; ++l) {
        if (areas[l] > areas[l-1] + eps) {
            if (err) { err->code = CT_ERR_MONO; err->layer = set->cs[l].layer; }
            return CT_ERR_MONO;
        }
    }
    return CT_OK;
}

void contour_free(CTSet *set) {
    if (!set) return;
    free(set->text);
    free(set->pool);
    set->text = NULL;
    set->pool = NULL;
    set->layers = 0;
    set->pool_n = 0;
}
