#include "dash.h"
#include <stdio.h>
#include <string.h>

static void report(const char *name, const DPoint *p, int n, double on, double off) {
    DPoly out;
    DashFit f;
    int rc = dash_polyline_fit(p, n, on, off, 1, &f, &out);
    if (rc != 0) {
        const char *why = rc == -3 ? "周期为零" :
                          rc == -4 ? "总长不足一个周期" : "参数非法";
        printf("%s: 拒绝 rc=%d %s 退化段=%d\n", name, rc, why, f.degenerate);
        return;
    }
    printf("%s: 解出周期=%.6f 手调周期=%.6f 实线=%.6f 间隔=%.6f 周期数=%d "
           "端点相位=%.6f 落在实线%.1f%%处 偏差=%.6f 退化段=%d 耗时=%.2fus 点数=%d\n",
           name, f.period, on + off, f.on, f.off, f.cycles,
           f.end_phase, 100.0 * f.end_pos, f.end_dev, f.degenerate,
           f.elapsed_us, out.count);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--no-fit") == 0) {
        /* 关掉新逻辑: 原手调周期路径, 输出与旧版逐字节一致 */
        DPoint p[3] = {{0,0},{60,0},{60,40}};
        DPoly out;
        if (dash_polyline_fit(p, 3, 8.0, 6.0, 0, NULL, &out) != 0) return 1;
        printf("dash points %d\n", out.count);
        return 0;
    }
    DPoint g2[3] = {{0,0},{60,0},{60,40}};
    DPoint g4[5] = {{0,0},{30,0},{30,20},{50,20},{50,50}};
    DPoint gc[5] = {{0,0},{40,0},{40,40},{0,40},{0,0}};
    DPoint g0[5] = {{0,0},{0,0},{50,0},{50,0},{50,40}};
    report("两段  ", g2, 3, 8.0, 6.0);
    report("四段  ", g4, 5, 8.0, 6.0);
    report("闭合环", gc, 5, 8.0, 6.0);
    report("零长  ", g0, 5, 8.0, 6.0);
    DPoint zz[2] = {{3,3},{3,3}};
    report("全零长", zz, 2, 8.0, 6.0);
    DPoint tiny[2] = {{0,0},{5,0}};
    report("短于周期", tiny, 2, 8.0, 6.0);
    DashFit f;
    int rc = dash_solve_period(100.0, 0.0, 0.0, &f);
    printf("零周期: 拒绝 rc=%d 周期为零\n", rc);
    return 0;
}
