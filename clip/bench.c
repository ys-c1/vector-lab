#define _POSIX_C_SOURCE 199309L
#include "clip.h"
#include "shapes.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define ITERS 20000

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double abs_area(const CPoly *p) {
    double a = clip_area(p);
    return a < 0 ? -a : a;
}

static void run_case(const char *tag, const CPoint *win, int wn,
                     double hand_area, double hand_half,
                     const char *csv_path) {
    CPoint cover[4], half[4];
    int cn = shape_cover(cover);
    int hn = shape_left_half(half);

    CPoly pieces[CLIP_MAX_PIECES];
    int ep = -1;
    int np = clip_decompose(win, wn, pieces, CLIP_MAX_PIECES, &ep);
    if (np < 0) {
        char msg[160];
        printf("[%s] decompose failed: %s\n", tag,
               clip_strerror(np, ep, msg, sizeof msg));
        return;
    }
    double piece_union = 0;
    for (int k = 0; k < np; ++k) piece_union += abs_area(&pieces[k]);

    /* 按凸块手算并集核对：直接对每块做全覆盖裁剪，求和应等于块并集/手算面积 */
    CPoly outs[CLIP_MAX_PIECES];
    int nouts = 0, npieces = 0;
    double ua = 0;

    double t0 = now_sec();
    for (int it = 0; it < ITERS; ++it) {
        int rc = clip_concave(cover, cn, win, wn, 1, outs, CLIP_MAX_PIECES,
                              &nouts, &ua, &npieces, &ep);
        if (rc != 0) { printf("[%s] clip failed rc=%d\n", tag, rc); return; }
    }
    double t_split = now_sec() - t0;

    double ua_off = 0;
    CPoly offs[CLIP_MAX_PIECES];
    t0 = now_sec();
    for (int it = 0; it < ITERS; ++it) {
        int rc = clip_concave(cover, cn, win, wn, 0, offs, CLIP_MAX_PIECES,
                              &nouts, &ua_off, &npieces, &ep);
        if (rc != 0) { printf("[%s] clip-off failed rc=%d\n", tag, rc); return; }
    }
    double t_off = now_sec() - t0;

    int hnouts = 0; double hua = 0; int hnp = 0;
    clip_concave(half, hn, win, wn, 1, outs, CLIP_MAX_PIECES,
                 &hnouts, &hua, &hnp, &ep);

    FILE *fp = fopen(csv_path, "w");
    long csv_bytes = fp ? clip_pieces_csv(pieces, np, fp) : -1;
    if (fp) fclose(fp);

    printf("=== %s (%d vertices) ===\n", tag, wn);
    printf("pieces: %d\n", np);
    printf("window area   hand=%.6f  piece-union=%.9f  clip-union=%.9f\n",
           hand_area, piece_union, ua);
    printf("area error vs hand: abs=%.3e  rel=%.3e\n",
           fabs(ua - hand_area), fabs(ua - hand_area) / hand_area);
    printf("piece-union vs hand: abs=%.3e  rel=%.3e\n",
           fabs(piece_union - hand_area), fabs(piece_union - hand_area) / hand_area);
    printf("left-half clip: hand=%.6f got=%.9f (non-empty pieces %d)\n",
           hand_half, hua, hnouts);
    printf("split=OFF (legacy path) area=%.6f  <-- demonstrates the old defect\n", ua_off);
    printf("time/call: split=ON %.2f ns   split=OFF %.2f ns   (x%d iters)\n",
           t_split / ITERS * 1e9, t_off / ITERS * 1e9, ITERS);
    printf("piece table -> %s (%ld bytes)\n\n", csv_path, csv_bytes);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build";
    char path[512];

    CPoint uwin[CLIP_MAX_VERTS];
    int un = shape_u_window(uwin);
    snprintf(path, sizeof path, "%s/pieces_u8.csv", dir);
    run_case("U-notch 8 verts", uwin, un, 72.0, 36.0, path);

    CPoint cwin[CLIP_MAX_VERTS];
    int cn = shape_comb_window(cwin);
    snprintf(path, sizeof path, "%s/pieces_comb64.csv", dir);
    /* 左半(x<=5)手算：5*10 - 7 个完整凹口(0.6*5) - 1 个切半凹口(0.2*5)
       = 50 - 21 - 1 = 28 */
    run_case("comb 64 verts", cwin, cn, 55.0, 28.0, path);
    return 0;
}
