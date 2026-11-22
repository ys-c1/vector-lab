#include "pipeline.h"
#include "raster.h"
#include <math.h>
#include <time.h>

#define RPIPE_PI 3.14159265358979323846
#define RPIPE_2PI (2.0 * RPIPE_PI)

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    return raster_fill(&moved, &win, w, h, 4, buf);
}

/* 单个角点: 由目标窗口坐标 u 闭式反解 rot, 再做一次牛顿修正。
 * 放置模型 move(): u = scale * R(rot) * c + t 。 */
static RCorner solve_one(RPoint c, RPoint u, RPlace q, double dxw, double dyw) {
    clock_t t0 = clock();
    RCorner r = {0.0, 0.0, 0, 0.0};
    double qx = (u.x - q.tx) / q.scale, qy = (u.y - q.ty) / q.scale;
    double theta;
    if (c.x == 0.0 && c.y == 0.0) {
        theta = 0.0; /* 角点压在原点, 旋转不可观测, 跳过一次牛顿修正 */
    } else {
        theta = atan2(qy, qx) - atan2(c.y, c.x);
        /* 一次牛顿迭代: 残差 res = scale*R(theta)c + t - u,
         * 雅可比 J = scale*R(theta)*(-c.y, c.x), 步长 -(J.res)/(J.J) */
        double co = cos(theta), si = sin(theta);
        double rx = (c.x * co - c.y * si) * q.scale + q.tx - u.x;
        double ry = (c.x * si + c.y * co) * q.scale + q.ty - u.y;
        double jx = (-c.x * si - c.y * co) * q.scale;
        double jy = (c.x * co - c.y * si) * q.scale;
        double jj = jx * jx + jy * jy;
        if (jj > 0.0) theta -= (jx * rx + jy * ry) / jj;
    }
    /* 归一化到 (-pi, pi], 记录抹掉的整圈数 */
    r.turns = (long)floor(theta / RPIPE_2PI + 0.5);
    r.angle = theta - (double)r.turns * RPIPE_2PI;
    double co = cos(r.angle), si = sin(r.angle);
    double lx = (c.x * co - c.y * si) * q.scale + q.tx;
    double ly = (c.x * si + c.y * co) * q.scale + q.ty;
    double ex = (lx - u.x) / dxw, ey = (ly - u.y) / dyw; /* 换算到像素 */
    r.dev = sqrt(ex * ex + ey * ey);
    r.usec = (double)(clock() - t0) * 1e6 / (double)CLOCKS_PER_SEC;
    return r;
}

int pipeline_solve_rot(const RScene *s, RPlace place, RBox win, int w, int h,
                       const RPoint target_px[4], RCorner out[4]) {
    if (!s || !target_px || !out || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;                 /* 参数 place.scale */
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3; /* 参数 win */
    RBox bb;
    if (scene_bbox(s, &bb) != 0) return -1;
    RPoint corner[4] = {
        {bb.minx, bb.miny}, {bb.maxx, bb.miny},
        {bb.maxx, bb.maxy}, {bb.minx, bb.maxy},
    };
    double dxw = (win.maxx - win.minx) / w;
    double dyw = (win.maxy - win.miny) / h;
    for (int k = 0; k < 4; ++k) {
        RPoint u; /* 目标像素中心映射到窗口坐标 */
        u.x = win.minx + (target_px[k].x + 0.5) * dxw;
        u.y = win.miny + (target_px[k].y + 0.5) * dyw;
        out[k] = solve_one(corner[k], u, place, dxw, dyw);
        if (out[k].dev > 1.0) return -4;
    }
    return 0;
}
