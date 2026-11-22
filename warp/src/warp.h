#ifndef WARP_H
#define WARP_H

#include <stdio.h>

/*
 * 变形修正：弧长反参数化的不动点迭代。
 *
 * 场景：三次贝塞尔 P(t) 在参数化下被拉伸/压缩（t 均匀时弧长并不均匀）。
 * 给定目标弧长 s，反求参数 t 使 S(t)=s，即求非线性变形映射 S 的逆。
 * 解析逆不存在，用不动点迭代逐次逼近。
 *
 * 记 v(u)=|P'(u)|（速率），L=S(1) 为曲线总弧长。新路径的迭代式：
 *
 *     t_{k+1} = clamp( t_k + omega(t_k) * (s - S(t_k)) / L , 0, 1 )
 *
 * 自适应下松弛因子（vmin/vmax 由全参数域扫描得到）：
 *
 *     omega(u) = min(omega_cap, 0.9 * L / v(u))
 *     omega_cap = min(0.9, 1.8 * L / vmax)
 *
 * 收敛条件（压缩映射）：g(u)=u+omega(u)*(s-S(u))/L，
 *     |g'(u)| = |1 - omega(u)*v(u)/L|。
 *   - 低速区 omega=0.9L/v，代入得 |g'|=0.1；
 *   - 高速区 omega=omega_cap<=1.8L/vmax，代入得 |g'|<=0.8。
 *   故全局 q=max|g'|<1，Banach 定理保证不动点唯一、任意初值线性收敛，
 *   且 |t*-t_k| <= q^k/(1-q)*|t_1-t_0|。实测四条曲线 q<=0.60、
 *   64 轮上限内全部达标，演示按相邻步长比实测 q 并打印。
 *   用户给定固定 omega（cfg->omega>0）时合法区间为 (0,2)，
 *   越界返回 WARP_E_TOL、字段 "omega"；取接近 2 的值即可制造振荡专项。
 *
 * 停止准则（满足任一即停）：
 *   - 弧长残差 |s-S(t_k)| <= tol_len（物理量，主判据）；
 *   - 参数步长 |t_{k+1}-t_k| <= tol_t（次判据，防残差平台空转）。
 * 到 max_iter 仍未达标 => WARP_NO_CONVERGE，结果拒绝（不返回点）。
 *
 * 振荡：连续出现 sign(d_k)=-sign(d_{k-1})（修正方向反转）且
 * |d_k| >= eta*|d_{k-1}|（幅度不衰减，eta 默认 0.8），判 WARP_OSCILLATING，
 * 单独上报振荡周期（2 周期=两点来回）与首个触发步，不与普通失败混淆。
 * omega 接近或越过压缩边界 2L/v 即可构造该情形，用于专项验证。
 *
 * 关闭新逻辑（flags 不含 WARP_F_FIXPOINT）时走旧路径：固定 8 次迭代、
 * 按端弦 L0 归一化、omega 恒为 1，次数写死、不做任何停止判断；
 * 这是改动前的现状行为，任何调用方拿到的点与旧代码逐位相同
 * （make -C warp compat 同时比对 t、点坐标与 PGM 字节）。
 */

typedef struct { double x, y; } WPoint;

#define WARP_LEGACY_ITERS 8    /* 现状：固定修正 8 次，无停止判据 */
#define WARP_DEFAULT_MAX   64
#define WARP_FIELD_MAX     32

/* 求解状态 */
enum {
    WARP_OK = 0,
    WARP_NO_CONVERGE = 1,    /* 达 max_iter 残差仍超标，拒绝 */
    WARP_OSCILLATING = 2,    /* 检测到振荡，单独上报 */
    WARP_E_ARG = -1,         /* 空指针/越界；field = "c"/"out"/... */
    WARP_E_CTRL = -2,        /* 控制点非有限；field = "c[k].x"/"c[k].y" */
    WARP_E_TARGET = -3,      /* 目标弧长越界；field = "s" */
    WARP_E_TOL = -4,         /* tol/max_iter/omega 非法；field 见报告 */
    WARP_E_DEGEN = -5,       /* 曲线退化为零长；field = "c" */
    WARP_E_CAP = -6,         /* 等分点容量不足 */
    WARP_E_IO = -7           /* 文件写出失败 */
};

#define WARP_F_FIXPOINT 1u    /* 打开不动点收敛逻辑；缺省只跑 legacy 8 次 */

typedef struct {
    double tol_len;     /* 弧长残差上限（主），<=0 用默认 1e-10 */
    double tol_t;       /* 参数步长上限（次），<=0 用默认 1e-12 */
    int    max_iter;    /* 迭代上限，<=0 用 WARP_DEFAULT_MAX，不得超过 4096 */
    double omega;       /* <=0：自适应 min(cap,0.9L/v)；>0：固定因子，
                         * 合法 (0,2)，越界 WARP_E_TOL，字段 "omega" */
    double osc_eta;     /* 振荡幅度比阈值，<=0 用默认 0.8 */
} WarpCfg;

typedef struct {
    int    status;      /* WARP_OK / WARP_NO_CONVERGE / WARP_OSCILLATING / 负错误码 */
    int    iters;       /* 实际执行的迭代次数 */
    double t;           /* 末轮参数（拒绝时保留末值但 point 无效） */
    double residual;    /* 末轮弧长残差 |s-S(t)|，物理单位 */
    double step;        /* 末轮参数步长 |t_k-t_{k-1}| */
    double rate;        /* 实测收敛因子 q = step_k/step_{k-1}（>=2 轮才有） */
    int    osc_period;  /* 振荡时的周期（2=两点来回），否则 0 */
    int    osc_step;    /* 首次触发振荡判定的迭代步，否则 0 */
    char   field[WARP_FIELD_MAX]; /* 出错字段名，OK 时为 "" */
} WarpReport;

/* 自适应 Simpson 求从 0 到 t 的累计弧长 S(t)；eps 为积分容差。 */
double warp_arclen(const WPoint c[4], double t);

/* 旧路径：固定 WARP_LEGACY_ITERS 次修正，无停止判据、无报告。 */
int warp_correct_legacy(const WPoint c[4], double target_s, double t0,
                        double *out_t);

/* 新路径：不动点收敛。flags=0 时内部直接等价调用 legacy（报告 iters=8）。
 * rep 可为 NULL。target_s 以 warp_arclen(c,1) 为总长，越界 WARP_E_TARGET。 */
int warp_correct(const WPoint c[4], double target_s, double t0,
                 unsigned flags, const WarpCfg *cfg,
                 WPoint *point, WarpReport *rep);

/* 按累计弧长 n 等分取点（含两端）。flags/cfg 透传给 warp_correct；
 * 任一点不收敛即整体返回该状态，已算出的点仍写入 out 供诊断。 */
int warp_equally_spaced(const WPoint c[4], int n, unsigned flags,
                        const WarpCfg *cfg, WPoint *out, WarpReport *reps);

/* PGM 导出：把等分点按 x/y 坐标栅格化（w*h，覆盖给定包围盒）。 */
int warp_save_pgm(const char *path, const WPoint *pts, int n,
                  double minx, double miny, double maxx, double maxy,
                  int w, int h);

/* 单次求解报告导出 CSV（供逐次数值核对）。 */
int warp_save_report_csv(const char *path, const WarpReport *r, int n);

#endif
