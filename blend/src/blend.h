#ifndef BLEND_H
#define BLEND_H
/*
 * 混合模式的矩阵化处理与连乘顺序反解。
 *
 * 每个混合层（源色 s，范围 0..1）对底色 c 的作用都写成每通道仿射：
 *     c' = a*c + b
 * 在齐次坐标 (r,g,b,1) 下即 4x4 分块对角矩阵，层链输出 = 各层矩阵连乘。
 *
 * 只有可写成仿射的混合模式能给出矩阵；分段/非线性模式没有矩阵形式，
 * 出现在链中时直接拒绝（错误码 -11，并由调用方报告具体步骤）。
 */

typedef enum {
    BLEND_NORMAL = 0,   /* c' = s                       奇异，不可逆 */
    BLEND_MULTIPLY,     /* c' = c*s                     s=0 时奇异   */
    BLEND_SCREEN,       /* c' = c + s - c*s             s=1 时奇异   */
    BLEND_PLUS,         /* c' = c + s（截断前线性）      恒可逆       */
    BLEND_DIFFERENCE,   /* c' = |c - s|                 分段，无矩阵  */
    BLEND_EXCLUSION,    /* c' = c + s - 2*c*s           s=0.5 时奇异 */
    BLEND_DARKEN,       /* c' = min(c, s)               分段，无矩阵  */
    BLEND_DODGE,        /* c' = c/((1-s)(1-c))          非线性，无矩阵 */
    BLEND_N
} BlendMode;

/* 每个模式的简短名字（矩阵报告用）。 */
const char *blend_name(BlendMode m);

/* 4x4 齐次矩阵，行主序：out = M * (r,g,b,1)^T。 */
typedef struct { double m[4][4]; } BMat;

/* 一层混合：源色 (sr,sg,sb) 下该模式对应的 4x4 矩阵。
 * 返回 0 成功；返回 -11 表示该模式不是仿射的（无矩阵）。 */
int blend_matrix(BlendMode mode, double sr, double sg, double sb, BMat *out);

/* 前向合成：按 order 给定的层序（底->顶）从底色 base 出发展开，
 * 结果写入 out。非仿射模式走原始公式，仍可前向计算。
 * 任何输入越界返回 -1。 */
typedef struct {
    BlendMode mode;
    double r, g, b;      /* 该层源色，0..1 */
} BlendLayer;

int blend_forward(const BlendLayer *layers, const int *order, int n,
                  const double base[3], double out[3]);

/* 反解需要的单步可逆性检查。
 * 返回 0 表示该层（给定源色下）可逆；
 * 返回 -10 表示矩阵奇异（行列式为 0），pivot 给出失效的通道（0/1/2，-1 为无）；
 * 返回 -11 表示该模式根本没有矩阵形式（非线性/分段）。 */
int blend_invertible(const BlendLayer *layer, int *pivot);

/* 色差：CIE76 Delta E（Lab 空间欧氏距离）。 */
double blend_delta_e(const double c1[3], const double c2[3]);

/* 顺序反解结果。 */
#define BLEND_MAX_LAYERS 8
typedef struct {
    int order[BLEND_MAX_LAYERS];  /* 层索引，底->顶 */
    double achieved[3];           /* 按该顺序前向得到的颜色 */
    double delta;                 /* 与目标的色差 */
    long elapsed_ns;              /* 求解耗时 */
    int tried;                    /* 评估过的排列数 */
} BlendSolution;

/* 由目标颜色反解连乘顺序。
 *   n==1 时不走排列搜索，直接返回单层（order[0]=0）并在 *single 置 1。
 *   存在无矩阵/奇异模式时拒绝：返回负值，bad_step 报告出问题的层索引（0基）。
 *   所有排列都超过容差 tol（Delta E）返回 -12。
 *   参数非法返回 -1。 */
int blend_solve(const BlendLayer *layers, int n,
                const double base[3], const double target[3], double tol,
                BlendSolution *sol, int *single, int *bad_step, int *bad_code);

/* 工具：矩阵乘法 c = a * b。 */
BMat bmat_mul(const BMat *a, const BMat *b);
/* 工具：矩阵作用于颜色。 */
void bmat_apply(const BMat *a, const double in[3], double out[3]);
#endif
