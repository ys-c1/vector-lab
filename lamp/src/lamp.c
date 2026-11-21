#include "lamp.h"
#include <math.h>
#include <stdio.h>

#define LAMP_CAP 1024
#define STEP 0.5          /* 逐像素模式的历史步进，改了会破坏旧输出 */
#define EPS 1e-12

const char *lamp_strerror(int code) {
    switch (code) {
    case LAMP_OK:         return "ok";
    case LAMP_E_ARG:      return "invalid argument: pts/out is null or n < 2";
    case LAMP_E_SPACING:  return "invalid argument: spacing must be >= 0 and > 0 in equidistant mode (parameter 'spacing')";
    case LAMP_E_ZERO_LEN: return "invalid argument: pts polyline total length is zero (all points coincide)";
    case LAMP_E_CAPACITY: return "capacity exceeded: more than 1024 lamp positions";
    default:              return "unknown error";
    }
}

static double seg_len(LPoint a, LPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

/* 按累计弧长 s 在折线上反查点。调用方保证 0 <= s <= total。
 * 注意 s 恰好等于顶点累计值时必须停在该顶点所在段（f=1），
 * 不能滑到下一段，否则转角后的灯会全钉在转角点上。*/
static LPoint point_at_s(const LPoint *pts, const double *cum, int n, double s) {
    int i = 0;
    while (i + 2 < n && cum[i + 1] <= s - EPS) ++i;
    double span = cum[i + 1] - cum[i];
    double f = span > EPS ? (s - cum[i]) / span : 0.0;
    if (f > 1.0) f = 1.0;
    LPoint r;
    r.x = pts[i].x + (pts[i + 1].x - pts[i].x) * f;
    r.y = pts[i].y + (pts[i + 1].y - pts[i].y) * f;
    return r;
}

int lamp_place(const LPoint *pts, int n, double spacing,
               int equidistant, LLamps *out) {
    if (!pts || !out || n < 2 || n > LAMP_MAX_N) return LAMP_E_ARG;
    /* 点距为负（含 NaN）一律拒绝，指出参数 spacing */
    if (isnan(spacing) || spacing < 0.0) return LAMP_E_SPACING;
    /* 等距模式下点距为零无法构造序列；逐像素模式不使用 spacing，
     * 传零保持与旧逐像素实现一致的行为 */
    if (equidistant && spacing == 0.0) return LAMP_E_SPACING;
    out->count = 0;

    /* 总长为零：所有顶点重合，两种模式都拒绝并指出参数 pts */
    double total = 0.0;
    for (int i = 0; i + 1 < n; ++i)
        total += seg_len(pts[i], pts[i + 1]);
    if (total <= EPS) return LAMP_E_ZERO_LEN;

    if (equidistant) {
        /* 第一段：按折线段累计长度建索引 */
        double cum[LAMP_MAX_N];
        cum[0] = 0.0;
        for (int i = 1; i < n; ++i)
            cum[i] = cum[i - 1] + seg_len(pts[i - 1], pts[i]);

        /* 第二段：按目标点距反查参数位置，s = spacing, 2*spacing, ...
         * spacing 大于总长时一盏都放不下，正常返回空结果 */
        int k = 0;
        for (double s = spacing; s < total - EPS && k < LAMP_CAP; s += spacing)
            out->pts[k++] = point_at_s(pts, cum, n, s);
        if (k >= LAMP_CAP) return LAMP_E_CAPACITY;
        out->count = k;
        return LAMP_OK;
    }

    /* 逐像素模式：与旧实现逐位相同，每 STEP 单位一盏灯。
     * 总长已在上方校验过非零；段内零长（中间重合点）沿用旧错误码 */
    int k = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i + 1].x - pts[i].x;
        double dy = pts[i + 1].y - pts[i].y;
        double len = sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return LAMP_E_ZERO_LEN;
        for (double s = 0.0; s < len; s += STEP) {
            if (k >= LAMP_CAP) return LAMP_E_CAPACITY;
            out->pts[k].x = pts[i].x + dx * (s / len);
            out->pts[k].y = pts[i].y + dy * (s / len);
            ++k;
        }
    }
    out->count = k;
    return LAMP_OK;
}

int lamp_export_csv(const LLamps *lamps, const LPoint *pts, int n,
                    const char *path) {
    if (!lamps || !pts || n < 2 || n > LAMP_MAX_N) return LAMP_E_ARG;
    FILE *fp = stdout;
    if (path && *path && !(path[0] == '-' && path[1] == '\0')) {
        fp = fopen(path, "w");
        if (!fp) return LAMP_E_ARG;
    }
    fprintf(fp, "index,arc_s,gap,x,y\n");

    double prev_s = 0.0;
    for (int k = 0; k < lamps->count; ++k) {
        /* 由灯位坐标反推弧长：落在段内按比例还原；
         * 顶点(t≈0)归到上一段末，避免在转角处弧长回退 */
        LPoint q = lamps->pts[k];
        double s = 0.0;
        double acc = 0.0;
        for (int i = 0; i + 1 < n; ++i) {
            double dx = pts[i + 1].x - pts[i].x;
            double dy = pts[i + 1].y - pts[i].y;
            double len = sqrt(dx * dx + dy * dy);
            if (len < EPS) continue;
            double t = ((q.x - pts[i].x) * dx + (q.y - pts[i].y) * dy)
                       / (len * len);
            if (t < 0) t = 0; else if (t > 1) t = 1;
            double rx = q.x - (pts[i].x + dx * t);
            double ry = q.y - (pts[i].y + dy * t);
            int on = (rx * rx + ry * ry) < 1e-12;
            if (on && (t > 1e-9 || i == 0)) { s = acc + t * len; break; }
            acc += len;
        }
        fprintf(fp, "%d,%.6f,%.6f,%.6f,%.6f\n",
                k, s, s - prev_s, q.x, q.y);
        prev_s = s;
    }
    if (fp != stdout) fclose(fp);
    return LAMP_OK;
}
