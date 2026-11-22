#ifndef SPICK_H
#define SPICK_H

#include <stdio.h>

/*
 * 屏幕空间容差拾取（screen-space tolerance picking）。
 *
 * 现状（旧路径）：用逆矩阵把屏幕点击点 s 回到形状空间得 back，再用一个
 * 形状空间标量容差找段。容差是形状单位，旋转/缩放后它在屏幕上的有效大小
 * 随之变化（缩小后容差变“大”，放大后变“小”）。
 *
 * 新路径：像素容差 tol_px 按逆变换 J = M^{-1} 的雅可比换算成形状容差
 *      tol_shape = tol_px * sigma_max(J)
 * 再在形状空间判 dist(back, seg) <= tol_shape。这是一种全局（不分段）
 * 的雅可比换算；各处打印的 tol_shape 即此值。
 *
 * 约定（与 svg/src/svgpath.h、xform/src/xform.h 一致）：
 *   形状空间 (u,v) --M--> 屏幕空间 (x,y)
 *     x = a*u + c*v + e
 *     y = b*u + d*v + f
 *   前向线性部分 L = [[a,c],[b,d]]，det = a*d - b*c。
 *   逆矩阵 J = L^{-1} = (1/det)*[[d,-c],[-b,a]]。
 *   拾取点 s 在屏幕空间；back = J*(s-(e,f)) 是形状空间查询点。
 *
 * 雅可比：
 *     |J|_F^2   = (d^2 + c^2 + b^2 + a^2)/det^2
 *     det(J)    = 1/det
 *     sigma_max = sqrt((|J|_F^2 + sqrt(|J|_F^4 - 4*det(J)^2))/2)
 *     sigma_min = sqrt((|J|_F^2 - sqrt(|J|_F^4 - 4*det(J)^2))/2)
 *     kappa     = sigma_max/sigma_min（各向异性；相似变换下为 1）
 *   旋转 + 等比缩放 k 时 J = (1/k)R(-theta)，两个奇异值都是 1/|k|，
 *   tol_shape = tol_px/|k|，屏幕上的有效容差严格等于 tol_px。
 *
 * 为什么取 sigma_max：
 *   记 q 为形状空间最近点，d = |q-back|，r = |M(q)-s|。由 L 的奇异值夹逼
 *     sigma_min(L)*d <= r <= sigma_max(L)*d,
 *   且 1/sigma_min(L) = sigma_max(J)、1/sigma_max(L) = sigma_min(J)：
 *     r <= tol_px              => d <= tol_px*sigma_max(J) = tol_shape
 *                               （屏幕上 tol_px 内的点一定命中：不漏判）
 *     d > tol_shape            => r > tol_px
 *                               （形状距离超限一定在屏幕 tol_px 外：不误判
 *                                 屏幕内的点）
 *   反过来，新路径可能命中的最大屏幕距离是
 *     r <= sigma_max(L)*tol_shape = tol_px*kappa,
 *   即各向异性会带来“屏幕 tol_px 之外、tol_px*kappa 之内”的保守误收
 *   （换来的好处是不漏判）。相似变换下 kappa=1，该带消失。
 *
 * 与逐像素点击参考逐项比对与误差上界：
 *   参考实现：枚举点击点 s 容差圆内每个整数像素中心 p（|p-s| <= tol_px），
 *   对每个 p 把它映回形状空间并测试各段；段的屏幕像穿过该像素（p 到像的
 *   距离 <= sqrt(2)/2，即像素中心到像素角）即视为该像素点中该段，任一点中
 *   即参考命中。参考不使用雅可比、奇异值或逆矩阵的显式求逆以外的任何
 *   本模块逻辑。
 *   令 h = sqrt(2)/2，对任一段：
 *     r <= tol_px - h  => 参考必然命中；
 *     r >  tol_px + h  => 参考必然不命中。
 *
 *   误差上界（按参考实现的实际判定，逐步推导）：
 *     (a) 新命中 且 参考未命中：新命中给出 r <= tol_px*kappa；参考未命中
 *         给出 r > tol_px - h。故不一致段必满足
 *             tol_px - h < r <= tol_px * kappa.
 *     (b) 新未命中 且 参考命中：新未命中给出 r > tol_px（相似变换下
 *         严格；一般仿射下 r > tol_px/kappa），参考命中给出 r <= tol_px + h。
 *         故不一致段必满足
 *             tol_px/kappa < r <= tol_px + h.
 *   两种情形都蕴含“r 落在 (tol_px - h, tol_px*kappa + h] 内”，所以
 *   SpPick.band_n = 统计 r 落在该带内的段数即为不一致数上界：
 *     |新路径命中率 - 参考命中率| <= band_n / nsegs.
 *   相似变换（kappa=1）下该带退化为 (tol_px - h, tol_px + h]，
 *   |不一致|/nsegs 通常远小于 2h/nsegs。演示里同时打印实测不一致数，
 *   并断言 实测 <= band_n。
 *
 * 关闭（flags 不含 SP_F_SCREEN_TOL）时走旧路径：tol 直接作为形状空间标量，
 * 命中集合与旧行为逐位一致；sp_pick_legacy 与 sp_pick(...,0) 是同一条代码。
 *
 * 矩阵奇异 / 雅可比退化：返回 SP_E_SINGULAR，err_field 指出字段。
 *   - 输入系数非有限                    -> "a"/"b"/"c"/"d"/"e"/"f"
 *   - 行列式近零或非有限                -> "det"
 *   - 逆矩阵元素溢出                    -> "det"
 *   - 奇异值退化（sigma_min 为零/非有限）-> "sigma_min"
 *   - 中间量 |J|_F^2 非有限             -> "fro2"
 *   拾取点非有限 -> SP_E_SEG，field="scr"；容差非法 -> SP_E_TOL，field="tol"。
 */

typedef struct { double a, b, c, d, e, f; } SpForm;   /* 与 XForm 同布局 */
typedef struct { double x, y; } SpPoint;
typedef struct { SpPoint p0, p1; } SpSeg;

#define SP_MAX_SEGS  4096
#define SP_FIELD_MAX 32
#define SP_MAX_MISMATCH 4096

/* 返回码；失败时 err_field 给出出错字段名（可为 NULL）。 */
enum {
    SP_OK = 0,
    SP_E_ARG = -1,      /* 空指针 / n 越界；field = "segs"/"n"/"out"/"m" */
    SP_E_SEG = -2,      /* 点坐标非有限；field = "segs"/"scr" */
    SP_E_TOL = -3,      /* tol 为负或非有限；field = "tol" */
    SP_E_FLAGS = -4,    /* 未知标志位；field = "flags" */
    SP_E_SINGULAR = -5, /* 矩阵奇异 / 雅可比退化；field 见文件头 */
    SP_E_CAP = -6       /* 命中数超过 SP_MAX_SEGS（保留） */
};

#define SP_F_SCREEN_TOL 1u  /* 打开屏幕空间容差（雅可比换算） */

/* 容差模式（记录在结果里，便于表格分栏） */
enum {
    SP_TOL_MODE_LEGACY = 0,   /* 旧路径：tol 直接当形状空间标量 */
    SP_TOL_MODE_JACOBIAN = 1  /* 屏幕容差按雅可比换算成形状容差 */
};

/* 雅可比审计信息 */
typedef struct {
    double det;        /* 前向矩阵行列式 a*d - b*c */
    double j00, j01;   /* J = M^{-1} 的线性部分 */
    double j10, j11;
    double fro2;       /* |J|_F^2 */
    double sigma_max;  /* J 的最大奇异值（形状/屏幕） */
    double sigma_min;  /* J 的最小奇异值 */
    double kappa;      /* sigma_max/sigma_min（各向异性比例） */
    int singular;      /* 1 = 奇异/退化 */
    const char *field; /* 退化时出错字段；否则空串 */
} SpJac;

/* 每段一行的结果（用于与参考逐项比对） */
typedef struct {
    int seg;           /* 段号 */
    double dist;       /* 形状空间：查询点到段距离 */
    double tol_shape;  /* 本行使用的形状空间容差 */
    double u;          /* 段参数 [0,1] */
    SpPoint at;        /* 形状空间最近点 */
    SpPoint scr;       /* 最近点经 M 映射到屏幕 */
    double dist_px;    /* 屏幕空间：拾取点到最近点屏幕像的距离 */
    double r_px;       /* 屏幕空间：拾取点到段屏幕像的真实距离 */
    int hit;           /* 1 = 命中 */
    int reason;        /* SP_R_* */
} SpRow;

enum {
    SP_R_NONE = 0,
    SP_R_INSIDE = 1,   /* 形状距离 <= 换算容差 */
    SP_R_OUTSIDE = 2   /* 形状距离 > 换算容差 */
};

typedef struct {
    SpJac jac;
    int mode;            /* SP_TOL_MODE_* */
    int nsegs;           /* 参与测试的段数 */
    double tol_in;       /* 输入容差：屏幕模式为像素，旧路径为形状单位 */
    double tol_shape;    /* 实际使用的形状空间容差 */
    int count;           /* 命中数 */
    SpRow rows[SP_MAX_SEGS];
    /* 与逐像素参考可能不一致的段号列表及个数（band_n 即命中率误差上界） */
    int band_n;
    int band_seg[SP_MAX_MISMATCH];
} SpPick;

/*
 * 计算逆矩阵与雅可比。
 *   m         前向矩阵 M（形状->屏幕）
 *   out       结果；可为 NULL（仅校验）
 *   err_field 出错字段名（可为 NULL，容量至少 SP_FIELD_MAX）
 * 返回 SP_OK 或 SP_E_*；奇异/退化返回 SP_E_SINGULAR 并填 out->singular=1。
 */
int sp_jacobian(const SpForm *m, SpJac *out, char err_field[SP_FIELD_MAX]);

/*
 * 屏幕空间容差拾取。
 *   segs      形状空间段
 *   n         段数 1..SP_MAX_SEGS
 *   m         前向矩阵 M（形状->屏幕），须可逆
 *   scr       屏幕空间拾取点 s
 *   tol       容差；置 SP_F_SCREEN_TOL 时为像素（>= 0），否则为形状单位
 *   flags     0 = 旧路径；SP_F_SCREEN_TOL = 逐段按雅可比换算屏幕容差
 *   out       结果；可为 NULL（仅做校验与雅可比计算）
 *   err_field 出错字段名
 *
 * 旧路径命中判据：形状距离 <= tol。
 * 屏幕模式命中判据：形状距离 <= tol*sigma_max(J)（逐段雅可比版见设计说明）。
 */
int sp_pick(const SpSeg *segs, int n, const SpForm *m, SpPoint scr,
            double tol, unsigned flags, SpPick *out,
            char err_field[SP_FIELD_MAX]);

/* 旧路径便捷入口；与 sp_pick(..., flags=0) 逐位一致。 */
int sp_pick_legacy(const SpSeg *segs, int n, const SpForm *m, SpPoint scr,
                   double tol, SpPick *out, char err_field[SP_FIELD_MAX]);

/* 形状点 -> 屏幕点 */
SpPoint sp_map(const SpForm *m, SpPoint p);

/* 屏幕点按 M^{-1} 映回形状空间；奇异返回 SP_E_SINGULAR（field="det"）。 */
int sp_unmap(const SpForm *m, SpPoint scr, SpPoint *out,
             char err_field[SP_FIELD_MAX]);

/* 导出逐段结果 CSV；only_hits=0 导全部测试段，1 只导命中段。 */
int sp_export_rows(const SpPick *p, FILE *fp, int only_hits);

const char *sp_strerror(int code);
const char *sp_reason_name(int reason);

#endif
