#include "winsplit.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 独立实现的线对分类，只用于校验文件里的拆分声明（与 winnorm 不共享代码）。 */
static int cross_at(WPoint a, WPoint b, WPoint c, WPoint d, WPoint *at) {
    double rx = b.x - a.x, ry = b.y - a.y;
    double sx = d.x - c.x, sy = d.y - c.y;
    double den = rx * sy - ry * sx;
    if (den == 0.0) return 0;
    double qx = c.x - a.x, qy = c.y - a.y;
    double t = (qx * sy - qy * sx) / den;
    double u = (qx * ry - qy * rx) / den;
    if (t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0) return 0;
    at->x = a.x + rx * t;
    at->y = a.y + ry * t;
    return 1;
}

/* 按拆分声明把窗口在交点处切开。每条声明独立执行：在当前子环里找
 * 包含这两条输入边的那个环，把它切成两段。 */
static int apply_split(WSWindow *w, int ea, int eb, char *err, size_t errsz,
                       int line) {
    if (ea < 0 || eb < 0 || ea >= w->in_n || eb >= w->in_n) {
        snprintf(err, errsz, "第 %d 行（窗口 %d）：拆分边号越界（边号 %d %d，顶点数 %d）",
                 line, w->id, ea, eb, w->in_n);
        return -1;
    }
    if (ea == eb || (eb == ea + 1) || (ea == 0 && eb == w->in_n - 1)) {
        snprintf(err, errsz, "第 %d 行（窗口 %d）：拆分边号 %d 与 %d 相邻或相同，不可能真穿过",
                 line, w->id, ea, eb);
        return -1;
    }
    WPoint a = w->in[ea], b = w->in[(ea + 1) % w->in_n];
    WPoint c = w->in[eb], d = w->in[(eb + 1) % w->in_n];
    WPoint q;
    if (!cross_at(a, b, c, d, &q)) {
        snprintf(err, errsz,
                 "第 %d 行（窗口 %d）：边 %d 与边 %d 不真穿过（不相交/贴边/共线），不能作为拆分点",
                 line, w->id, ea, eb);
        return -1;
    }

    /* 在子环里找同时含这两条输入边的环（按坐标匹配输入边端点）。 */
    for (int r = 0; r < w->rings; ++r) {
        int n = w->ring[r].n;
        int ia = -1, ib = -1;
        for (int i = 0; i < n; ++i) {
            WPoint p = w->ring[r].pts[i], s = w->ring[r].pts[(i + 1) % n];
            if (p.x == a.x && p.y == a.y && s.x == b.x && s.y == b.y) ia = i;
            if (p.x == c.x && p.y == c.y && s.x == d.x && s.y == d.y) ib = i;
        }
        if (ia < 0 || ib < 0) continue;
        WPoint A[WN_MAX_VERTS], B[WN_MAX_VERTS];
        int na = 0, nb = 0;
        A[na++] = q;
        for (int k = (ia + 1) % n;; k = (k + 1) % n) {
            if (na >= WN_MAX_VERTS) { snprintf(err, errsz, "拆分后子环顶点超限"); return -1; }
            A[na++] = w->ring[r].pts[k];
            if (k == ib) break;
        }
        B[nb++] = q;
        for (int k = (ib + 1) % n;; k = (k + 1) % n) {
            if (nb >= WN_MAX_VERTS) { snprintf(err, errsz, "拆分后子环顶点超限"); return -1; }
            B[nb++] = w->ring[r].pts[k];
            if (k == ia) break;
        }
        if (na < 3 || nb < 3) {
            snprintf(err, errsz, "第 %d 行（窗口 %d）：拆分后出现不足三点的子环",
                     line, w->id);
            return -1;
        }
        if (w->rings + 1 >= WN_MAX_RINGS) {
            snprintf(err, errsz, "第 %d 行（窗口 %d）：拆分后子环数超过 %d",
                     line, w->id, WN_MAX_RINGS);
            return -1;
        }
        w->ring[r].n = na;
        memcpy(w->ring[r].pts, A, sizeof(WPoint) * (size_t)na);
        w->ring[w->rings].n = nb;
        memcpy(w->ring[w->rings].pts, B, sizeof(WPoint) * (size_t)nb);
        ++w->rings;
        return 0;
    }
    snprintf(err, errsz,
             "第 %d 行（窗口 %d）：边 %d 与边 %d 不在同一个子环里（拆分声明顺序不对）",
             line, w->id, ea, eb);
    return -1;
}

int wsplit_load(const char *path, WFileSet *out, char *err, size_t errsz) {
    if (err && errsz) err[0] = '\0';
    if (!path || !out) {
        if (err) snprintf(err, errsz, "拒绝：空指针");
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) snprintf(err, errsz, "拒绝：无法打开 %s", path);
        return -1;
    }
    out->count = 0;
    char line[4096];
    int lineno = 0;
    WSWindow *cur = NULL;
    /* 拆分声明先攒着，等窗口顶点读全再统一应用 */
    struct { int win; int a, b, line; } pend[WN_MAX_CROSS * 32];
    int npend = 0;

    while (fgets(line, sizeof line, f)) {
        ++lineno;
        char *p = line;
        while (*p && isspace((unsigned char)*p)) ++p;
        if (*p == '#' || *p == '\0') continue;
        if (*p == 'W') {
            int id;
            char extra;
            if (sscanf(p + 1, "%d %c", &id, &extra) != 1) {
                if (err) snprintf(err, errsz, "第 %d 行：窗口头格式错误（需要 W <编号>）", lineno);
                fclose(f);
                return -1;
            }
            if (out->count >= (int)(sizeof out->win / sizeof out->win[0])) {
                if (err) snprintf(err, errsz, "第 %d 行：窗口数超过上限", lineno);
                fclose(f);
                return -1;
            }
            for (int k = 0; k < out->count; ++k)
                if (out->win[k].id == id) {
                    if (err) snprintf(err, errsz, "第 %d 行：窗口号 %d 重复", lineno, id);
                    fclose(f);
                    return -1;
                }
            if (cur && cur->in_n < 3) {
                if (err) snprintf(err, errsz, "窗口 %d 顶点不足三个（实际 %d 个）",
                                  cur->id, cur->in_n);
                fclose(f);
                return -1;
            }
            cur = &out->win[out->count++];
            memset(cur, 0, sizeof *cur);
            cur->id = id;
            continue;
        }
        if (*p == 'S') {
            int id, a, b;
            char extra;
            if (sscanf(p + 1, "%d %d %d %c", &id, &a, &b, &extra) != 3) {
                if (err) snprintf(err, errsz,
                                  "第 %d 行：拆分声明格式错误（需要 S <窗口号> <边a> <边b>）",
                                  lineno);
                fclose(f);
                return -1;
            }
            if (npend >= (int)(sizeof pend / sizeof pend[0])) {
                if (err) snprintf(err, errsz, "第 %d 行：拆分声明过多", lineno);
                fclose(f);
                return -1;
            }
            pend[npend].win = id;
            pend[npend].a = a;
            pend[npend].b = b;
            pend[npend].line = lineno;
            ++npend;
            continue;
        }
        if (*p == 'w' || *p == 's') {
            if (err) snprintf(err, errsz, "第 %d 行：命令必须大写（W/S）", lineno);
            fclose(f);
            return -1;
        }
        /* 顶点行 */
        double x, y;
        char extra;
        int got = sscanf(p, "%lf %lf %c", &x, &y, &extra);
        if (got != 2) {
            if (err) snprintf(err, errsz, "第 %d 行：顶点格式错误（需要 x y 两列）", lineno);
            fclose(f);
            return -1;
        }
        if (!cur) {
            if (err) snprintf(err, errsz, "第 %d 行：顶点出现在任何窗口头之前", lineno);
            fclose(f);
            return -1;
        }
        if (cur->in_n >= WN_MAX_VERTS) {
            if (err) snprintf(err, errsz, "窗口 %d 顶点超过上限 %d", cur->id, WN_MAX_VERTS);
            fclose(f);
            return -1;
        }
        cur->in[cur->in_n].x = x;
        cur->in[cur->in_n].y = y;
        ++cur->in_n;
    }
    fclose(f);
    if (cur && cur->in_n < 3) {
        if (err) snprintf(err, errsz, "窗口 %d 顶点不足三个（实际 %d 个）", cur->id, cur->in_n);
        return -1;
    }
    if (out->count == 0) {
        if (err) snprintf(err, errsz, "拒绝：文件里没有任何窗口（需要 W <编号> 开头）");
        return -1;
    }

    /* 初始化子环 = 输入整环，再逐条应用拆分声明 */
    for (int k = 0; k < out->count; ++k) {
        out->win[k].rings = 1;
        out->win[k].ring[0].n = out->win[k].in_n;
        memcpy(out->win[k].ring[0].pts, out->win[k].in,
               sizeof(WPoint) * (size_t)out->win[k].in_n);
    }
    for (int k = 0; k < npend; ++k) {
        int wi = -1;
        for (int j = 0; j < out->count; ++j)
            if (out->win[j].id == pend[k].win) { wi = j; break; }
        if (wi < 0) {
            if (err) snprintf(err, errsz, "第 %d 行：拆分声明指向不存在的窗口号 %d",
                              pend[k].line, pend[k].win);
            return -1;
        }
        if (apply_split(&out->win[wi], pend[k].a, pend[k].b, err, errsz, pend[k].line))
            return -1;
        out->win[wi].split_a[out->win[wi].nsplit] = pend[k].a;
        out->win[wi].split_b[out->win[wi].nsplit] = pend[k].b;
        ++out->win[wi].nsplit;
    }

    for (int k = 0; k < out->count; ++k) {
        WSWindow *w = &out->win[k];
        for (int r = 0; r < w->rings; ++r) {
            double s = winnorm_signed_area(w->ring[r].pts, w->ring[r].n);
            w->raw_signed[r] = s;
            w->raw_abs[r] = s < 0.0 ? -s : s;
        }
    }
    return 0;
}

double wsplit_total_abs(const WSWindow *w) {
    if (!w) return 0.0;
    double t = 0.0;
    for (int r = 0; r < w->rings; ++r) t += w->raw_abs[r];
    return t;
}

double wsplit_norm_total_abs(const WNorm *nrm) {
    if (!nrm) return 0.0;
    double t = 0.0;
    for (int r = 0; r < nrm->rings; ++r) t += nrm->abs_area[r];
    return t;
}
