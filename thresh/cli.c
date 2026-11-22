/* 灰度阈值反解命令行。
 *
 * 固定模式（旧逻辑，关掉新逻辑时输出与历史逐字节一致）：
 *   thresh fixed <in.pgm> <out.pgm> <threshold 0..255>
 *
 * 反解模式：亮像素占比 = F(t) = #{gray >= t} / N，F 关于 t 单调不增。
 * 反解使 F(t) 落在 [lo,hi] 的阈值；阈值必须落在灰度范围 [0,255]，否则拒绝。
 *   thresh solve <in.pgm> <out.pgm> <lo> <hi>
 *
 * 批量模式（清单每行：name path lo hi，'#' 起始为注释）：
 *   thresh batch <manifest> <outdir>
 *
 * 退出码：0 全部达标；1 用法错；2 读文件失败；3 区间非法；
 *         4 阈值超出灰度范围/目标不可达；5 纯黑或纯白（单独报告）；6 写文件失败。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/thresh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

static const char *status_name(TStatus st) {
    switch (st) {
        case T_OK:      return "OK";
        case T_EMPTY:   return "EMPTY";
        case T_BLACK:   return "BLACK";
        case T_WHITE:   return "WHITE";
        case T_UNREACH: return "UNREACHABLE";
    }
    return "?";
}

/* 处理单张图：计时、反解、报告、落盘。返回进程退出码。 */
static int process_one(const char *name, const char *in, const char *out,
                       double lo, double hi, int verbose) {
    double t0 = now_sec();
    TImage img;
    int rc = thresh_load_pgm(in, &img);
    if (rc != 0) {
        printf("[%s] LOAD-FAIL rc=%d path=%s\n", name, rc, in);
        return 2;
    }
    THist h;
    thresh_hist(&img, &h);

    if (thresh_is_black(&h) || thresh_is_white(&h)) {
        double el = (now_sec() - t0) * 1000.0;
        printf("[%s] %s pixels=%ld ratio=%.6f threshold=- elapsed_ms=%.3f (单独报告：无灰度分布，无法反解)\n",
               name, thresh_is_black(&h) ? "PURE-BLACK" : "PURE-WHITE",
               h.total, thresh_is_black(&h) ? 0.0 : 1.0, el);
        thresh_free(&img);
        return 5;
    }
    if (!(lo >= 0.0 && lo <= 1.0 && hi >= 0.0 && hi <= 1.0 && lo <= hi)) {
        printf("[%s] BAD-RANGE lo=%.6f hi=%.6f\n", name, lo, hi);
        thresh_free(&img);
        return 3;
    }

    int t;
    double ratio;
    TStatus st = thresh_invert(&h, lo, hi, &t, &ratio);
    double el = (now_sec() - t0) * 1000.0;
    if (st != T_OK) {
        /* 目标落在 F(t) 的台阶空隙中：要达标所需阈值不是整数灰度值，
         * 即阈值落在 [0,255] 灰度范围之外（两档灰度之间或区间外），拒绝并说明。 */
        double below, above;
        thresh_flanking_steps(&h, lo, hi, &below, &above);
        char gap[96];
        int glen = 0;
        if (above >= 0.0) glen += snprintf(gap + glen, sizeof(gap) - glen, "above=%.6f ", above);
        if (below >= 0.0) glen += snprintf(gap + glen, sizeof(gap) - glen, "below=%.6f", below);
        if (glen > 0 && gap[glen - 1] == ' ') gap[--glen] = '\0';
        printf("[%s] REJECT %s target=[%.6f,%.6f] nearest_steps={%s} "
               "elapsed_ms=%.3f (阈值超出灰度范围 [0,255]：目标落在占比台阶之间，拒绝)\n",
               name, status_name(st), lo, hi, glen ? gap : "none", el);
        thresh_free(&img);
        return 4;
    }

    TImage bin;
    if (thresh_apply_fixed(&img, t, &bin) != 0) {
        thresh_free(&img);
        return 6;
    }
    int wrc = thresh_save_pgm(out, &bin);
    free(bin.pix);
    if (wrc != 0) {
        printf("[%s] WRITE-FAIL path=%s\n", name, out);
        thresh_free(&img);
        return 6;
    }
    if (verbose) {
        printf("[%s] OK threshold=%d ratio=%.6f target=[%.6f,%.6f] "
               "pixels=%ld elapsed_ms=%.3f out=%s\n",
               name, t, ratio, lo, hi, h.total, el, out);
    } else {
        printf("[%s] OK threshold=%d ratio=%.6f elapsed_ms=%.3f\n", name, t, ratio, el);
    }
    thresh_free(&img);
    return 0;
}

static int cmd_fixed(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: thresh fixed <in.pgm> <out.pgm> <threshold>\n");
        return 1;
    }
    int t = atoi(argv[4]);
    if (t < 0 || t > 255) {
        fprintf(stderr, "threshold out of grayscale range [0,255]: %d\n", t);
        return 4;
    }
    TImage img;
    int rc = thresh_load_pgm(argv[2], &img);
    if (rc != 0) {
        fprintf(stderr, "load failed rc=%d\n", rc);
        return 2;
    }
    TImage bin;
    if (thresh_apply_fixed(&img, t, &bin) != 0) {
        thresh_free(&img);
        return 1;
    }
    if (thresh_save_pgm(argv[3], &bin) != 0) {
        fprintf(stderr, "write failed\n");
        free(bin.pix);
        thresh_free(&img);
        return 6;
    }
    THist h;
    thresh_hist(&bin, &h);
    printf("fixed threshold=%d on=%ld ratio=%.6f out=%s\n",
           t, h.hist[255], (double)h.hist[255] / h.total, argv[3]);
    free(bin.pix);
    thresh_free(&img);
    return 0;
}

static int cmd_solve(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: thresh solve <in.pgm> <out.pgm> <lo> <hi>\n");
        return 1;
    }
    double lo = atof(argv[4]), hi = atof(argv[5]);
    return process_one("img", argv[2], argv[3], lo, hi, 1);
}

static int cmd_batch(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: thresh batch <manifest> <outdir>\n");
        return 1;
    }
    FILE *f = fopen(argv[2], "r");
    if (!f) {
        fprintf(stderr, "manifest open failed: %s\n", argv[2]);
        return 2;
    }
    char line[512];
    int worst = 0;
    while (fgets(line, sizeof(line), f)) {
        char name[128], path[256];
        double lo, hi;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%127s %255s %lf %lf", name, path, &lo, &hi) != 4) {
            printf("BAD-LINE %s", line);
            worst = worst ? worst : 1;
            continue;
        }
        char out[400];
        snprintf(out, sizeof(out), "%s/%s.pgm", argv[3], name);        int rc = process_one(name, path, out, lo, hi, 1);
        if (rc > worst) worst = rc;
    }
    fclose(f);
    return worst;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage:\n"
                "  thresh fixed  <in.pgm> <out.pgm> <threshold 0..255>\n"
                "  thresh solve  <in.pgm> <out.pgm> <lo> <hi>\n"
                "  thresh batch  <manifest> <outdir>\n");
        return 1;
    }
    if (!strcmp(argv[1], "fixed")) return cmd_fixed(argc, argv);
    if (!strcmp(argv[1], "solve")) return cmd_solve(argc, argv);
    if (!strcmp(argv[1], "batch")) return cmd_batch(argc, argv);
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}
