#include "src/gamma.h"
#include "src/pipeline.h"
#include "src/scene.h"
#include "src/export.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 伽马反解演示：四类图（渐变 / 软边圆盘 / 同心环 / 混合场景）。
 *
 * 每类先按已知 gamma 生成“参照”（通过 gold/ref pgm-gamma 独立编码，
 * 打不开 gold 时用本文件内一份独立的 long double 参考编码兜底），
 * 再把参照与线性源图交给 gamma_fit 反解，报告：
 *   解出的 gamma、残差（CDF RMS）、耗时；
 *   用解出的 gamma 重新编码后与参照逐字节比对。
 *
 * 另做三组边角验证：
 *   - gamma 参数为 0 / 负数 / NaN 必须拒绝并指出参数名；
 *   - 全黑、全白图单独报告无法拟合（伽马无定义）；
 *   - 关闭新逻辑（RExportGamma.enabled=0）以及 enabled=1、gamma=1 时的
 *     导出与改动前基线 PGM 逐字节相同。
 */

#define W 256
#define H 192

static unsigned char q255(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 255.0) return 255;
    return (unsigned char)(long)(v + 0.5);
}

/* 独立参考编码（long double、逐像素幂运算；与模块内的 double 查表不同）。 */
static unsigned char ref_encode(int s, double g) {
    if (s <= 0) return 0;
    if (s >= 255) return 255;
    long double x = (long double)s / 255.0L;
    long double y = 255.0L * powl(x, 1.0L / (long double)g);
    if (y <= 0.0L) return 0;
    if (y >= 255.0L) return 255;
    return (unsigned char)(long)(y + 0.5L);
}

/* ---- 四类线性源图 ---- */
static void gen_ramp(unsigned char *b) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            b[y * W + x] = q255(255.0 * x / (W - 1));
}

static void gen_disc(unsigned char *b) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double fx = (x + 0.5) / W * 2.0 - 1.0;
            double fy = (y + 0.5) / H * 2.0 - 1.0;
            double r = sqrt(fx * fx + fy * fy);
            double cov = (0.62 - r) * 28.0 + 0.5;
            if (cov < 0.0) cov = 0.0;
            if (cov > 1.0) cov = 1.0;
            double bg = 0.12 + 0.5 * ((double)(x + y) / (W + H - 2));
            b[y * W + x] = q255(255.0 * (cov + (1.0 - cov) * bg));
        }
}

static void gen_rings(unsigned char *b) {
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double fx = (x + 0.5) / W * 2.0 - 1.0;
            double fy = (y + 0.5) / H * 2.0 - 1.0;
            double r = sqrt(fx * fx + fy * fy);
            double th = atan2(fy, fx);
            b[y * W + x] = q255(255.0 * (0.5 + 0.5 * sin(15.0 * r + 2.0 * th)));
        }
}

static void gen_scene(unsigned char *b) {
    static const double cir[3][3] = {
        {-0.35, -0.20, 0.45}, {0.40, 0.30, 0.30}, {-0.10, 0.50, 0.22}
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double fx = (x + 0.5) / W * 2.0 - 1.0;
            double fy = (y + 0.5) / H * 2.0 - 1.0;
            double v = 0.08 + 0.40 * ((double)x / (W - 1));
            for (int k = 0; k < 3; ++k) {
                double dx = fx - cir[k][0], dy = fy - cir[k][1];
                double d = sqrt(dx * dx + dy * dy);
                double cov = (cir[k][2] - d) * 25.0 + 0.5;
                if (cov < 0.0) cov = 0.0;
                if (cov > 1.0) cov = 1.0;
                v = v + (0.92 - v) * cov;
            }
            if (fx > 0.25) v = v * 0.35 + 0.65 * ((fx - 0.25) / 0.75);
            if (v < 0.0) v = 0.0;
            if (v > 1.0) v = 1.0;
            b[y * W + x] = q255(255.0 * v);
        }
}

/* ---- 参照生成：优先 gold/ref pgm-gamma，缺失时用本文件独立编码 ---- */
static int write_pgm(const char *path, const unsigned char *b) {
    return export_pgm(path, b, W, H);
}

static int gold_gamma(const char *gold, const char *in, const char *out, double g) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s pgm-gamma %s %s %.17g", gold, in, out, g);
    int rc = system(cmd);
    return rc == 0 ? 0 : -1;
}

static int make_reference(const char *gold, const unsigned char *src,
                          const char *lin_path, const char *ref_path,
                          double g, int *used_gold) {
    if (gold && gold_gamma(gold, lin_path, ref_path, g) == 0) {
        FILE *f = fopen(ref_path, "rb");
        if (f) { fclose(f); *used_gold = 1; return 0; }
    }
    *used_gold = 0;
    unsigned char *ref = (unsigned char *)malloc((size_t)W * H);
    if (!ref) return -1;
    for (int i = 0; i < W * H; ++i) ref[i] = ref_encode(src[i], g);
    int rc = write_pgm(ref_path, ref);
    free(ref);
    return rc;
}

/* 独立残差：按像素单遍统计直方图，再用 long double 累计 CDF；
 * 编码用本文件的 long double 参考编码，与模块的 double 查表独立。 */
static double ref_residual(const unsigned char *src, const unsigned char *ref,
                           double g) {
    long n = (long)W * H;
    static long hs[256], hr[256], mapped[256];
    memset(hs, 0, sizeof hs);
    memset(hr, 0, sizeof hr);
    memset(mapped, 0, sizeof mapped);
    for (long i = 0; i < n; ++i) {
        hs[src[i]]++;
        hr[ref[i]]++;
    }
    for (int v = 0; v < 256; ++v)
        if (hs[v]) mapped[ref_encode(v, g)] += hs[v];
    long long am = 0, ar = 0;
    long double sum2 = 0.0L;
    for (int v = 0; v < 256; ++v) {
        am += mapped[v];
        ar += hr[v];
        long double d = (long double)am / n - (long double)ar / n;
        sum2 += d * d;
    }
    return (double)sqrtl(sum2 / 256.0L);
}

/* ---- 边角：拒绝与无法拟合 ---- */
static int check_rejections(void) {
    int fails = 0;
    const char *bad[3] = {"0", "-1.5", "nan"};
    double vals[3] = {0.0, -1.5, NAN};
    for (int i = 0; i < 3; ++i) {
        char field[GAM_FIELD_MAX] = "";
        int rc = gamma_validate(vals[i], field, sizeof field);
        int ok = (rc == GAM_E_GAMMA && !strcmp(field, "gamma"));
        printf("reject gamma=%s -> rc=%d field=%s %s\n",
               bad[i], rc, field[0] ? field : "-", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }
    unsigned char px[4] = {0, 128, 255, 7}, out[4];
    char field[GAM_FIELD_MAX] = "";
    int rc = gamma_apply(px, 2, 2, -2.0, out, field, sizeof field);
    int ok = (rc == GAM_E_GAMMA && !strcmp(field, "gamma"));
    printf("apply gamma=-2 -> rc=%d field=%s %s\n", rc, field,
           ok ? "OK" : "FAIL");
    if (!ok) ++fails;

    /* 全黑 / 全白：无法拟合。 */
    size_t hb[256] = {0}, hw[256] = {0}, hr[256] = {0}, hn[256] = {0};
    hb[0] = 1000;
    hw[255] = 1000;
    hr[100] = 1000;
    hn[40] = 500;
    hn[200] = 500;
    GammaFit f;
    rc = gamma_fit_hist(hb, 1000, hr, 1000, &f);
    ok = (rc == GAM_E_FLAT && f.flat && f.flat_level == 0 &&
          !strcmp(f.err_field, "src"));
    printf("flat all-black -> rc=%d level=%d field=%s %s\n",
           rc, f.flat_level, f.err_field, ok ? "OK" : "FAIL");
    if (!ok) ++fails;
    rc = gamma_fit_hist(hw, 1000, hr, 1000, &f);
    ok = (rc == GAM_E_FLAT && f.flat && f.flat_level == 255 &&
          !strcmp(f.err_field, "src"));
    printf("flat all-white -> rc=%d level=%d field=%s %s\n",
           rc, f.flat_level, f.err_field, ok ? "OK" : "FAIL");
    if (!ok) ++fails;
    rc = gamma_fit_hist(hn, 1000, hb, 1000, &f);
    ok = (rc == GAM_E_FLAT && f.flat && f.flat_level == 0 &&
          !strcmp(f.err_field, "ref"));
    printf("flat reference -> rc=%d level=%d field=%s %s\n",
           rc, f.flat_level, f.err_field, ok ? "OK" : "FAIL");
    if (!ok) ++fails;
    return fails;
}

/* ---- 线性导出回归：与改动前基线 PGM 逐字节比较 ---- */
static int check_linear_baseline(const char *base_dir) {
    static const char *cases[3][3] = {
        {"scene.txt",   "legacy_a.pgm", "64 48 1.0"},
        {"scene_b.txt", "legacy_b.pgm", "97 61 1.3"},
        {"scene_c.txt", "legacy_c.pgm", "80 80 0.6"},
    };
    int fails = 0;
    for (int i = 0; i < 3; ++i) {
        char sp[512], bp[512];
        snprintf(sp, sizeof sp, "%s/%s", base_dir, cases[i][0]);
        snprintf(bp, sizeof bp, "%s/%s", base_dir, cases[i][1]);
        int w = 0, h = 0;
        double sc = 0.0;
        sscanf(cases[i][2], "%d %d %lf", &w, &h, &sc);

        unsigned char *want = NULL;
        int bw = 0, bh = 0;
        char field[GAM_FIELD_MAX] = "";
        if (gamma_load_pgm(bp, &want, &bw, &bh, field, sizeof field) != 0) {
            printf("linear baseline %s: missing (%s) FAIL\n", bp, field);
            ++fails;
            continue;
        }
        RScene s;
        scene_init(&s);
        FILE *f = fopen(sp, "r");
        if (!f) {
            printf("linear baseline %s: missing scene FAIL\n", sp);
            free(want); ++fails; continue;
        }
        char tag[16];
        while (fscanf(f, "%15s", tag) == 1) {
            if (tag[0] != 'p') break;
            RPoint pts[64];
            int n = 0;
            while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) {
                ++n;
                int c = fgetc(f);
                if (c == '\n' || c == EOF) break;
                ungetc(c, f);
            }
            if (scene_add_poly(&s, pts, n) != 0) { s.npolys = -1; break; }
        }
        fclose(f);
        RBox win;
        unsigned char *got = (unsigned char *)malloc((size_t)w * h);
        if (s.npolys < 0 || !got || scene_bbox(&s, &win) != 0 ||
            bw != w || bh != h) {
            printf("linear baseline case %d: setup FAIL\n", i);
            free(want); free(got); ++fails; continue;
        }
        RPlace place = {0.0, 0.0, 0.0, sc};
        RExportGamma off = {0, 1.0};
        int rc1 = pipeline_render_gamma(&s, place, win, w, h, got, &off);
        int same_off = (rc1 == 0) && memcmp(got, want, (size_t)w * h) == 0;
        RExportGamma g1 = {1, 1.0}; /* 显式 gamma=1，同样必须不变 */
        int rc2 = pipeline_render_gamma(&s, place, win, w, h, got, &g1);
        int same_g1 = (rc2 == 0) && memcmp(got, want, (size_t)w * h) == 0;
        RExportGamma bad = {1, 0.0}; /* gamma=0 拒绝，不改缓冲 */
        int rc3 = pipeline_render_gamma(&s, place, win, w, h, got, &bad);
        printf("linear baseline %-13s off=%s gamma1=%s gamma0_rc=%d %s\n",
               cases[i][1], same_off ? "identical" : "DIFF",
               same_g1 ? "identical" : "DIFF", rc3,
               (same_off && same_g1 && rc3 == -10) ? "OK" : "FAIL");
        if (!(same_off && same_g1 && rc3 == -10)) ++fails;
        free(want);
        free(got);
    }
    return fails;
}

int main(int argc, char **argv) {
    const char *gold = argc > 1 ? argv[1] : "../gold/build/ref";
    const char *tmp = argc > 2 ? argv[2] : "gamma_work";
    const char *base = argc > 3 ? argv[3] : "baseline";

    struct { const char *name; void (*gen)(unsigned char *); double g; }
    cls[4] = {
        {"ramp", gen_ramp, 0.45},
        {"disc", gen_disc, 1.25},
        {"rings", gen_rings, 1.80},
        {"scene", gen_scene, 2.20},
    };

    unsigned char *src = (unsigned char *)malloc((size_t)W * H);
    unsigned char *ref = (unsigned char *)malloc((size_t)W * H);
    unsigned char *chk = (unsigned char *)malloc((size_t)W * H);
    if (!src || !ref || !chk) return 2;

    char lin_path[512], ref_path[512];
    snprintf(lin_path, sizeof lin_path, "%s/linear.pgm", tmp);
    snprintf(ref_path, sizeof ref_path, "%s/reference.pgm", tmp);

    int fails = 0, used_gold_any = 0;
    printf("四类图：%dx%d，参照由 %s 生成；拟合按 CDF RMS 残差，阈值 1e-3\n",
           W, H, "gold/ref pgm-gamma");
    printf("%-6s %8s %10s %12s %10s %7s %6s %8s %s\n",
           "类", "真值", "解出", "相对误差", "残差", "耗时ms", "求值", "平台宽%", "字节");
    for (int i = 0; i < 4; ++i) {
        cls[i].gen(src);
        if (write_pgm(lin_path, src) != 0) {
            fprintf(stderr, "write %s failed\n", lin_path);
            return 2;
        }
        int used_gold = 0;
        if (make_reference(gold, src, lin_path, ref_path, cls[i].g, &used_gold) != 0) {
            fprintf(stderr, "make reference failed\n");
            return 2;
        }
        used_gold_any |= used_gold;
        int rw, rh;
        unsigned char *rf = NULL;
        char field[GAM_FIELD_MAX] = "";
        if (gamma_load_pgm(ref_path, &rf, &rw, &rh, field, sizeof field) != 0 ||
            rw != W || rh != H) {
            fprintf(stderr, "load reference failed (%s)\n", field);
            return 2;
        }
        memcpy(ref, rf, (size_t)W * H);
        free(rf);

        GammaFit fit;
        int rc = GAM_OK;
        double best_ms = 1e18;
        for (int t = 0; t < 5; ++t) {
            rc = gamma_fit(src, ref, W, H, &fit);
            if (fit.ms < best_ms) best_ms = fit.ms;
        }
        if (rc != GAM_OK) {
            printf("%-6s FIT FAILED rc=%d\n", cls[i].name, rc);
            ++fails;
            continue;
        }
        double rms = ref_residual(src, ref, fit.gamma);
        long double rel = ((long double)fit.gamma - cls[i].g) / cls[i].g * 100.0L;
        int same = 0;
        if (gamma_apply(src, W, H, fit.gamma, chk, field, sizeof field) == GAM_OK)
            same = memcmp(chk, ref, (size_t)W * H) == 0;
        double width = (fit.hi - fit.lo) / fit.gamma * 100.0;
        printf("%-6s %8.3f %10.6f %+11.5Lf%% %10.2e %7.2f %6d %8.4f %s\n",
               cls[i].name, cls[i].g, fit.gamma, rel, fit.residual, best_ms,
               fit.evals, width, same ? "identical" : "DIFF");
        if (fit.residual > 1e-3 || rms > 1e-3 || !same) {
            printf("  FAIL residual=%.3e ref_rms=%.3e bytes=%d\n",
                   fit.residual, rms, same);
            ++fails;
        }
    }
    if (!used_gold_any)
        printf("note: gold/ref 不可用，参照改用演示内独立 long double 编码\n");

    printf("\n[拒绝 gamma<=0 / NaN]\n");
    fails += check_rejections();

    printf("\n[关闭新逻辑：线性导出与改动前基线逐字节比较]\n");
    fails += check_linear_baseline(base);

    printf("\n%s\n", fails ? "FAIL" : "PASS");
    free(src); free(ref); free(chk);
    return fails ? 1 : 0;
}
