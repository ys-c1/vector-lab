#ifndef STROKE_H
#define STROKE_H
typedef struct { double x, y; } SPoint;
typedef struct { int count; SPoint pts[128]; } SPoly;
int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out);

/* 拐角接法 */
typedef enum { SJOIN_MITER = 0, SJOIN_BEVEL = 1, SJOIN_ROUND = 2 } SJoin;

/* 单个拐角的判定报告 */
typedef struct {
    int vertex;        /* 拐角顶点在输入折线中的下标 */
    double angle;      /* 转角（弧度），0 < angle < pi */
    SJoin join;        /* 选中的接法 */
    double err_miter;  /* 斜接的面积误差 */
    double err_bevel;  /* 斜切的面积误差 */
    double err_round;  /* 圆角的面积误差 */
    double err;        /* 选中接法的面积误差 */
    double seconds;    /* 本次判定耗时（秒） */
} SJoinReport;

/* 描边（可选自适应接法）。
 * adaptive == 0：固定接法，输出与 stroke_polyline 逐字节相同。
 * adaptive != 0：对每个拐角比较斜接、斜切、圆角相对理想扇形的面积误差，
 *   取误差最小者；三种误差完全相同时固定选斜切（顶点最少、计算最简、
 *   结果确定，不依赖浮点偶然性）。
 * err_max：选中接法的面积误差必须小于该值，否则整体失败。
 * round_segs：圆角的弦逼近段数，1..64（为 1 时圆角退化为斜切）。
 * reports：可选，长度至少 n-2，逐拐角记录判定结果。
 * 返回：0 成功；-1 参数非法；-2 零长线段；-3 拐角为 0 度（方向未改变，
 *       无角可接）；-4 拐角为 180 度（线段折返，接法未定义）；
 *       -5 选中接法的面积误差不小于 err_max；-6 输出容量不足。
 */
int stroke_polyline_ex(const SPoint *pts, int n, double width, int adaptive,
                       double err_max, int round_segs,
                       SJoinReport *reports, SPoly *out);
const char *stroke_join_name(SJoin j);
#endif
