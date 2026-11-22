#ifndef DASH_H
#define DASH_H
typedef struct { double x, y; } DPoint;
typedef struct { int count; DPoint pts[128]; } DPoly;

/* 反解结果: 让虚线终点正好落在实线段上 */
typedef struct {
    double period;     /* 解出的周期 */
    double on;         /* 微调后的实线段长 */
    double off;        /* 微调后的间隔长 */
    int    cycles;     /* 完整周期数 */
    double end_phase;  /* 终点在末周期内的相位 */
    double end_pos;    /* 终点在实线段内的相对位置 [0,1] */
    double end_dev;    /* 终点距实线段的偏差(落在实线内为 0) */
    int    degenerate; /* 退化(零长)段数, 单独统计 */
    double elapsed_us; /* 求解耗时, 微秒 */
} DashFit;

int dash_polyline(const DPoint *pts, int n, double on, double off, DPoly *out);

/* 反解模式长度: 总长对周期取模, 解出让终点落在实线段中点的周期,
 * 两段长度按比例微调。周期为零返回 -3, 总长不足一个周期返回 -4。 */
int dash_solve_period(double total, double on, double off, DashFit *fit);

/* fit_enabled=0: 走原手调周期路径, 行为与 dash_polyline 逐字节一致。
 * fit_enabled=1: 先反解周期再切分; 退化段跳过并计入 fit->degenerate。 */
int dash_polyline_fit(const DPoint *pts, int n, double on, double off,
                      int fit_enabled, DashFit *fit, DPoly *out);
#endif
