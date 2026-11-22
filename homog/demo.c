#define _POSIX_C_SOURCE 199309L
#include "homog.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const HPoint SRC[4] = {{0,0},{1,0},{1,1},{0,1}};

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void matrix_range(const Homog *h, double *lo, double *hi, double *amax) {
    const double e[8] = {
        h->h11, h->h12, h->h13, h->h21, h->h22, h->h23, h->h31, h->h32
    };
    *lo = e[0]; *hi = e[0]; *amax = fabs(e[0]);
    for (int i = 1; i < 8; ++i) {
        if (e[i] < *lo) *lo = e[i];
        if (e[i] > *hi) *hi = e[i];
        if (fabs(e[i]) > *amax) *amax = fabs(e[i]);
    }
}

static void report(const char *name, const HPoint dst[4]) {
    Homog h;
    HResidual r;
    enum { NITER = 100 };
    double t0 = now_us();
    int rc = -99;
    for (int k = 0; k < NITER; ++k)
        rc = homog_solve(SRC, dst, &h, &r);
    double t1 = now_us();
    if (rc != 0) {
        int bp[2];
        homog_check(dst, 4, bp);
        printf("%-12s REJECTED rc=%d bad_pair=(%d,%d)  time=%7.2f us/call\n",
               name, rc, bp[0], bp[1], (t1 - t0) / NITER);
        return;
    }
    double lo, hi, amax;
    matrix_range(&h, &lo, &hi, &amax);
    printf("%-12s rms=%9.3e max=%9.3e px  time=%7.2f us/call  "
           "H range=[% .4f, % .4f] max|H|=%g\n",
           name, r.rms, r.maxerr, (t1 - t0) / NITER, lo, hi, amax);
    printf("  per-corner px: %.3e %.3e %.3e %.3e\n",
           r.perr[0], r.perr[1], r.perr[2], r.perr[3]);
    printf("  H = [% .5f % .5f % .5f; % .5f % .5f % .5f; % .5f % .5f 1]\n",
           h.h11, h.h12, h.h13, h.h21, h.h22, h.h23, h.h31, h.h32);
}

/* ---- 贴图反解：给定 dest 四边形，把 64x64 棋盘格贴进 200x200 画布 ---- */
#define TW 64
#define TH 64
#define CW 200
#define CH 200

static void make_texture(unsigned char tex[TH][TW]) {
    for (int y = 0; y < TH; ++y)
        for (int x = 0; x < TW; ++x) {
            int cell = (x / 8 + y / 8) & 1;
            int border = (x < 2 || y < 2 || x >= TW - 2 || y >= TH - 2);
            tex[y][x] = (unsigned char)(border ? 255 : cell ? 200 : 60);
        }
}

static void warp(const Homog *h, unsigned char out[CH][CW]) {
    static unsigned char tex[TH][TW];
    make_texture(tex);
    Homog inv;
    memset(out, 0, CW * CH);
    if (homog_invert(h, &inv) != 0) return;
    for (int y = 0; y < CH; ++y)
        for (int x = 0; x < CW; ++x) {
            double w = inv.h31 * x + inv.h32 * y + 1.0;
            double u = (inv.h11 * x + inv.h12 * y + inv.h13) / w;
            double v = (inv.h21 * x + inv.h22 * y + inv.h23) / w;
            int tx = (int)floor(u * TW), ty = (int)floor(v * TH);
            if (tx >= 0 && tx < TW && ty >= 0 && ty < TH) out[y][x] = tex[ty][tx];
        }
}

static int save_pgm(const char *path, const unsigned char *buf) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return -1; }
    fprintf(f, "P5\n%d %d\n255\n", CW, CH);
    fwrite(buf, 1, (size_t)CW * CH, f);
    fclose(f);
    return 0;
}

int main(void) {
    /* 四组规定场景：正方形 / 梯形 / 极端斜置 / 退化共线 */
    HPoint sq[4]   = {{40,40},{160,40},{160,160},{40,160}};
    HPoint trap[4] = {{60,30},{170,30},{150,170},{50,170}};  /* 上底 110、下底 100，非平行四边形 */
    HPoint skew[4] = {{10,10},{190,40},{40,190},{-30,150}};
    HPoint line4[4]= {{0,0},{50,50.0000000001},{100,100},{150,149.9999999999}};
    HPoint dup[4]  = {{40,40},{40,40},{160,160},{40,160}};

    printf("== four-point homography (perspective ON) ==\n");
    report("square", sq);
    report("trapezoid", trap);
    report("extreme", skew);
    report("collinear", line4);
    report("coincident", dup);

    /* 实际贴图对比（真梯形）：
     * 仿射只是最小二乘近似（四角残差 2.5px，故 homog_solve 在仿射模式
     * 会以 -9 拒绝），这里直接取 homog_solve_affine 的最优近似做可视化；
     * 透视精确贴合四角。 */
    unsigned char buf1[CH][CW], buf2[CH][CW];
    Homog h;
    if (homog_solve_affine(SRC, trap, &h, 0) == 0) {
        warp(&h, buf1);
        save_pgm("warp_trap_affine.pgm", &buf1[0][0]);
    }
    homog_set_perspective(1);
    if (homog_solve(SRC, trap, &h, 0) == 0) {
        warp(&h, buf2);
        save_pgm("warp_trap_persp.pgm", &buf2[0][0]);
    }
    /* 旧仿射路径逐字节稳定性：同一正方形输入连跑两次，必须完全相同。
     * 同时验证开关关闭后 xform 风格的 6 参数映射结果确定可复现。 */
    unsigned char buf3[CH][CW], buf4[CH][CW];
    homog_set_perspective(0);
    if (homog_solve(SRC, sq, &h, 0) == 0) {
        warp(&h, buf3);
        Homog h2;
        homog_solve(SRC, sq, &h2, 0);
        warp(&h2, buf4);
        save_pgm("warp_sq_affine.pgm", &buf3[0][0]);
    }
    printf("affine rerun byte-identical: %s\n",
           memcmp(buf3, buf4, sizeof(buf3)) == 0 ? "yes" : "NO");
    printf("trap affine vs perspective differ: %s (perspective corrects skew)\n",
           memcmp(buf1, buf2, sizeof(buf1)) != 0 ? "yes" : "no");
    return 0;
}
