/* 采样相位反解与台阶验证工具。
 *
 * 背景：旧光栅化 raster_fill 是"任一子采样命中即整像素置 255"的二值逻辑。
 * 对半平面斜边，是否命中由"最容易命中的那个子采样"决定，等价于把采样点
 * 放在像素内 (0.125, 0.875)（ss=4），而不是居中 (0.5, 0.5)；且输出只有
 * 0/255，斜边因此出现一格一格的硬台阶。
 *
 * 本工具：
 *  1) 对四个角度（15/30/45/60°）的斜边分别渲染旧逻辑图；
 *  2) 由斜边相邻像素的亮度差列方程：每列（陡坡每行）的边缘跳变给出
 *     E_i = k·x_i + b + δ，对全部跳变最小二乘，反解像素内采样偏移 δ；
 *     四个角度联立 δ = k·φx − φy 解出有效采样相位 (φx, φy)；
 *  3) 相位偏离居中超过半格（0.5px）则拒绝校正并说明；否则应用校正
 *     （新逻辑 = 居中相位 + 覆盖率平均 raster_fill_cov）；
 *  4) 统计校正前后台阶数：沿主轴每列（行）内相邻像素亮度差 >= 250
 *     （近乎全幅的硬跳变）即一个台阶，要求每个角度下降 >= 30%；
 *  5) 全水平/全垂直的边方程退化（k=0 只约束 φy，垂直只约束 φx），
 *     单独用边缘位置扫描求解、单独报告；
 *  6) 校验新逻辑关闭（mode 0）时与旧管线逐字节一致。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/raster.h"
#include "src/pipeline.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 192
#define H 192
#define CX 96.0
#define CY 96.31          /* 故意非整数，避免边缘恰与像素格对齐 */
#define STEP_TH 250       /* 相邻像素亮度差 >= 250 记为一个硬台阶 */
#define REDUCE_NEED 0.30  /* 台阶数至少下降三成 */
#define PI 3.14159265358979323846

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* 半平面场景：过 (cx,cy)、角度 deg 的直线，填充法向 (-sin,cos) 一侧 */
static void edge_scene(RScene *s, double cx, double cy, double deg) {
    double th = deg * PI / 180.0;
    double dx = cos(th), dy = sin(th);
    double nx = -dy, ny = dx;
    double T = 1000.0;
    RPoint q[4] = {
        {cx + T*dx + T*nx, cy + T*dy + T*ny},
        {cx - T*dx + T*nx, cy - T*dy + T*ny},
        {cx - T*dx,        cy - T*dy},
        {cx + T*dx,        cy + T*dy},
    };
    scene_init(s);
    scene_add_poly(s, q, 4);
}

/* 列 x 的边缘位置：该列"未覆盖行数"的连续估计（由相邻像素亮度差累计）。
 * 二值图减 0.5：跳变只说明边界落在该像素格内，取格中点为无偏估计。 */
static double epos_col(const unsigned char *img, int x, int binary) {
    double s = 0;
    for (int y = 0; y < H; ++y) s += 1.0 - img[y * W + x] / 255.0;
    return binary ? s - 0.5 : s;
}

/* 行 y 的边缘位置（填充在左侧）：命中列数即边界 x 的连续估计 */
static double epos_row(const unsigned char *img, int y, int binary) {
    double s = 0;
    for (int x = 0; x < W; ++x) s += img[y * W + x] / 255.0;
    return binary ? s - 0.5 : s;
}

/* 沿主轴列方程并求解：每个跳变给出 E_i = a·(t_i+toff) + c（a、icpt 为真实直线），
 * 返回残差均值（即采样相位在该方向的偏移），fit_a 输出拟合斜率供核对。
 * toff：二值图取 0（epos 的 −0.5 已把跳变格换算成格中点）；
 *       覆盖率图取 0.5（Σ(1−cov) 是整列积分，等于像素中心处的边界位置）。 */
static double fit_residual(const unsigned char *img, double a, double icpt, double toff,
                           int walk_rows, int binary, int *neq, double *fit_a) {
    double st = 0, se = 0, stt = 0, ste = 0, sd = 0;
    int n = 0;
    for (int t = 2; t < (walk_rows ? H : W) - 2; ++t) {
        double e = walk_rows ? epos_row(img, t, binary) : epos_col(img, t, binary);
        double lim = walk_rows ? W : H;
        if (e < 2.0 || e > lim - 3.0) continue;   /* 跳变不在画面内，跳过 */
        st += t; se += e; stt += (double)t * t; ste += t * e;
        sd += e - (a * (t + toff) + icpt);
        ++n;
    }
    if (neq) *neq = n;
    if (fit_a) {
        double det = (double)n * stt - st * st;
        *fit_a = det != 0.0 ? ((double)n * ste - st * se) / det : 0.0;
    }
    return n ? sd / n : 0.0;
}

/* 轴对齐边的探针：渲染边缘位于 c+o 的图，返回中列（中行）的边缘位置估计 */
static double probe(const RBox *box, double cx, double cy, double deg, int vertical,
                    double o, unsigned char *img) {
    RScene s;
    if (vertical) edge_scene(&s, cx + o, cy, deg);
    else          edge_scene(&s, cx, cy + o, deg);
    raster_fill(&s, box, W, H, 4, img);
    return vertical ? epos_row(img, H / 2, 1) : epos_col(img, W / 2, 1);
}

/* 轴对齐边的相位求解：单幅图所有列（行）给出同一方程，退化；
 * 扫描边缘位置 o，每幅图给出约束 est = ceil/floor(c+o−φ) − 0.5，
 * 跳变点 o* 满足 c+o*−φ = q（整数）。两段 16 等分扫描把 o* 夹到 1/256 格，
 * 解出 φ = c + o* − q，误差 ≤ 1/512 格。 */
static double solve_phase_axis(const RBox *box, double cx, double cy, double deg,
                               int vertical, unsigned char *img, int *ok) {
    double lo = 0.0, hi = 1.0, q = 0.0;
    int found = 0;
    for (int stage = 0; stage < 2; ++stage) {
        double step = (hi - lo) / 16.0;
        double prev = probe(box, cx, cy, deg, vertical, lo, img);
        found = 0;
        for (int m = 1; m <= 16; ++m) {
            double o = lo + step * m;
            double e = probe(box, cx, cy, deg, vertical, o, img);
            if (e != prev) {           /* 跳变落在 (o-step, o] */
                hi = o; lo = o - step;
                q = e - 0.5;           /* 跳变后 est = q + 0.5 */
                found = 1;
                break;
            }
            prev = e;
        }
        if (!found) break;
    }
    if (ok) *ok = found;
    double o_mid = (lo + hi) / 2.0;
    return (vertical ? cx : cy) + o_mid - q;
}

/* 台阶数：主轴每列（行）内，边缘两侧相邻像素亮度差 >= STEP_TH 的列（行）数 */
static int count_steps(const unsigned char *img, int walk_rows) {
    int steps = 0;
    if (!walk_rows) {
        for (int x = 0; x < W; ++x)
            for (int y = 0; y + 1 < H; ++y)
                if (abs(img[y * W + x] - img[(y + 1) * W + x]) >= STEP_TH) { ++steps; break; }
    } else {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x + 1 < W; ++x)
                if (abs(img[y * W + x] - img[y * W + x + 1]) >= STEP_TH) { ++steps; break; }
    }
    return steps;
}

int main(void) {
    RBox box = {0, 0, W, H};
    static unsigned char old_img[H * W], new_img[H * W];
    const double angles[4] = {15.0, 30.0, 45.0, 60.0};
    double k[4], b[4], delta[4], delta2[4], t_old[4], t_new[4], fitk[4];
    int s0[4], s1[4], neq[4], rows[4];
    int fails = 0;

    printf("采样相位反解与台阶验证\n");
    printf("画面 %dx%d；旧逻辑=二值点采样(ss=4)，新逻辑=覆盖率(ss=8)+相位校正\n\n", W, H);

    /* 第一步：旧逻辑渲染 + 反解每角度相位偏移 + 数旧台阶 */
    for (int i = 0; i < 4; ++i) {
        k[i] = tan(angles[i] * PI / 180.0);
        b[i] = CY - k[i] * CX;
        rows[i] = fabs(k[i]) > 1.0;
        RScene s;
        edge_scene(&s, CX, CY, angles[i]);
        double t0 = now_ms();
        raster_fill(&s, &box, W, H, 4, old_img);
        delta[i] = rows[i]
            ? -k[i] * fit_residual(old_img, 1.0 / k[i], -b[i] / k[i], 0.0, 1, 1, &neq[i], &fitk[i])
            :          fit_residual(old_img, k[i], b[i], 0.0, 0, 1, &neq[i], &fitk[i]);
        if (rows[i]) fitk[i] = 1.0 / fitk[i];   /* 走行参数化的斜率换算回 k */
        s0[i] = count_steps(old_img, rows[i]);
        t_old[i] = now_ms() - t0;
    }

    /* 第二步：四角度联立 δ = k·φx − φy，解有效采样相位 */
    double sk = 0, skk = 0, skd = 0, sd = 0;
    for (int i = 0; i < 4; ++i) {
        sk += k[i]; skk += k[i] * k[i]; skd += k[i] * delta[i]; sd += delta[i];
    }
    /* 方程组: [skk -sk][phx]   [skd]
     *         [sk  -4 ][phy] = [sd ]  (∂/∂φy 式乘 -1 后) */
    double phx, phy;
    {
        double a11 = skk, a12 = -sk, a21 = sk, a22 = -4.0;
        double r1 = skd, r2 = sd;
        double d = a11 * a22 - a12 * a21;
        phx = (r1 * a22 - a12 * r2) / d;
        phy = (a11 * r2 - r1 * a21) / d;
    }
    double devx = phx - 0.5, devy = phy - 0.5;
    int accept = fabs(devx) <= 0.5 && fabs(devy) <= 0.5;

    /* 第三步：应用校正（或拒绝），渲染新逻辑图，验证台阶与残余相位 */
    for (int i = 0; i < 4; ++i) {
        RScene s;
        edge_scene(&s, CX, CY, angles[i]);
        double t0 = now_ms();
        if (accept) {
            raster_fill_cov(&s, &box, W, H, 8, 0.5, 0.5, new_img);
            delta2[i] = rows[i]
                ? -k[i] * fit_residual(new_img, 1.0 / k[i], -b[i] / k[i], 0.5, 1, 0, 0, 0)
                :          fit_residual(new_img, k[i], b[i], 0.5, 0, 0, 0, 0);
            s1[i] = count_steps(new_img, rows[i]);
        } else {
            s1[i] = s0[i];
            delta2[i] = delta[i];
        }
        t_new[i] = now_ms() - t0;
    }

    /* 报告：四个角度 */
    printf("== 四个角度（斜边） ==\n");
    printf("%-6s %-8s %-6s %-9s %-10s %-11s %-8s %-14s %s\n",
           "角度", "斜率k", "方程数", "拟合k", "解出δ(px)", "台阶(旧→新)", "下降", "耗时ms(旧/新)", "结论");
    for (int i = 0; i < 4; ++i) {
        double drop = s0[i] ? 1.0 - (double)s1[i] / s0[i] : 0.0;
        int pass = accept && drop >= REDUCE_NEED;
        if (!pass) ++fails;
        printf("%-6.1f %-8.4f %-6d %-9.4f %+10.3f %4d→%-6d %7.1f%% %6.2f/%-7.2f %s\n",
               angles[i], k[i], neq[i], fitk[i], delta[i], s0[i], s1[i],
               drop * 100.0, t_old[i], t_new[i],
               !accept ? "REJECT(未校正)" : pass ? "PASS" : "FAIL(<30%)");
    }

    /* 报告：全局相位与校正决策 */
    printf("\n== 全局相位（四角度联立 δ = k·φx − φy） ==\n");
    printf("旧逻辑有效采样相位 φ = (%.3f, %.3f)，居中应为 (0.500, 0.500)\n", phx, phy);
    printf("偏离居中 (%+.3f, %+.3f) px，半格阈值 0.5 px → %s\n", devx, devy,
           accept ? "接受校正" : "拒绝校正");
    if (accept) {
        printf("校正量 (%+.3f, %+.3f)，新逻辑采样相位 = (0.500, 0.500)，并启用覆盖率平均\n",
               0.5 - phx, 0.5 - phy);
        printf("校正后残余δ（应≈0）：");
        for (int i = 0; i < 4; ++i) printf("%.0f°=%+.3f ", angles[i], delta2[i]);
        printf("\n");
    } else {
        printf("拒绝原因：解出的采样偏移超出半格，单点采样模型与数据不符，"
               "不应用校正，台阶数保持旧值。\n");
    }

    /* 全水平 / 全垂直：方程退化，扫描定位跳变点，单独报告 */
    printf("\n== 水平 / 垂直边（方程退化，单独处理） ==\n");
    printf("单幅图中所有列（行）给出同一方程，无法联立；改用边缘位置扫描，\n");
    printf("两段 16 等分把跳变点夹到 1/256 格后解出相位。\n");
    {
        int ok;
        double phy_h = solve_phase_axis(&box, CX, CY, 0.0, 0, old_img, &ok);
        printf("水平边：k=0，方程 δ = −φy 只约束 φy。解出 φy = %.3f（居中 0.500）%s；\n",
               phy_h, ok ? "" : "（跳变点未定位，结果无效）");
        printf("  边缘平直无台阶结构，不参与台阶考核。\n");
        double phx_v = solve_phase_axis(&box, CX, CY, 90.0, 1, old_img, &ok);
        printf("垂直边：方程 δ = −φx 只约束 φx。解出 φx = %.3f（居中 0.500）%s；\n",
               phx_v, ok ? "" : "（跳变点未定位，结果无效）");
        printf("  边缘平直无台阶结构，不参与台阶考核。\n");
    }

    /* 字节一致性：新逻辑关闭时必须与旧管线逐字节一致 */
    printf("\n== 字节一致性（新逻辑关闭时） ==\n");
    {
        RScene s2;
        RPoint sq[4] = {{2, 3}, {40, 8}, {35, 44}, {6, 38}};
        scene_init(&s2);
        scene_add_poly(&s2, sq, 4);
        RBox w2 = {0, 0, 48, 48};
        RPlace pl = {1.5, -2.0, 0.3, 1.2};
        unsigned char ref[48 * 48], off[48 * 48], on[48 * 48];
        pipeline_render(&s2, pl, w2, 48, 48, ref);
        pipeline_render2(&s2, pl, w2, 48, 48, 0, 0.5, 0.5, off);
        pipeline_render2(&s2, pl, w2, 48, 48, 1, 0.5, 0.5, on);
        int same = memcmp(ref, off, sizeof ref) == 0;
        int changed = memcmp(ref, on, sizeof ref) != 0;
        printf("mode=0（新逻辑关闭）与 pipeline_render：%s（%zu 字节）\n",
               same ? "逐字节一致" : "不一致！", sizeof ref);
        printf("mode=1（新逻辑开启）输出不同：%s（覆盖率灰度生效）\n",
               changed ? "是" : "否（异常）");
        if (!same || !changed) ++fails;
    }

    printf("\n总结：%s\n", fails ? "存在未达标项" : "四个角度台阶数均下降超三成，相位校正已应用，旧路径逐字节不变");
    return fails ? 1 : 0;
}
