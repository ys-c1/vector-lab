/* 确定性线段集生成器：产出 master.txt（设计几何，交给 gold/ref 做精确真值）
 * 与 segs.txt（加噪后的输入，交给 double 管线反解容差）。
 * 两文件线段顺序、下标一致；每行 "x0 y0 x1 y1"，6 位小数定点格式。
 * 用法：gen <outdir>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct { double x0, y0, x1, y1; } Seg;

static unsigned long rng = 0x12345678UL;
static double rnd(void) {                 /* [-1, 1] */
    rng = rng * 6364136223846793005UL + 1442695040888963407UL;
    return (double)((rng >> 33) & 0x7fffffffUL) / (double)0x40000000 * 2.0 - 1.0;
}
static double nz(double v) { return v == 0.0 ? 0.0 : v; }

/* 每组的 gold 吸附口径（坐标单位）：设计错位 <= snap 的近失对在真值中保留。
 * 该值必须显著小于该组一成红线，使反解出的容差仍可被 ACCEPT。 */
static double dataset_snap(const char *name) {
    if (!strcmp(name, "A_general"))  return 0.12;
    if (!strcmp(name, "B_grazing"))  return 2.0;
    if (!strcmp(name, "C_endpoint")) return 0.0;    /* 全部严格接触，无需吸附 */
    if (!strcmp(name, "D_snapbig"))  return 0.0;    /* 真值均为严格交叉/点在线上 */
    return 0.0;
}

static void add(Seg *m, Seg *s, int *n, double x0, double y0, double x1, double y1,
                double jitter, int zero_ok) {
    m[*n] = (Seg){x0, y0, x1, y1};
    double j = jitter;
    (void)zero_ok;
    s[*n] = (Seg){
        nz(x0 + rnd() * j), nz(y0 + rnd() * j),
        nz(x1 + rnd() * j), nz(y1 + rnd() * j)};
    ++*n;
}
/* 零长段（点）：主数据精确落在指定点；加噪点沿锚线段方向做小位移，
 * 位移幅度 <= jitter，保证点始终停在锚线段的局部邻域内（gap 小且非恒 0）。
 * 退化身份记入 zflag，加噪后坐标不再相等也仍按零长段统计。 */
static void add_point_anchored(Seg *m, Seg *s, int *n, int *zflag,
                               double x, double y, double ux, double uy,
                               double jitter) {
    m[*n] = (Seg){x, y, x, y};
    double ul = hypot(ux, uy);
    double dx = ul > 0 ? ux / ul : 1.0, dy = ul > 0 ? uy / ul : 0.0;
    double off = rnd() * jitter;
    double px = nz(x + dx * off), py = nz(y + dy * off);
    s[*n] = (Seg){px, py, px, py};
    zflag[*n] = 1;
    ++*n;
}

static void write_set(const char *dir, const char *name, const Seg *m, const Seg *s,
                      int n, int designed_truth, const int *zero_flag) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/%s/master.txt", dir, name);
    FILE *fm = fopen(path, "w");
    snprintf(path, sizeof path, "%s/%s/segs.txt", dir, name);
    FILE *fs = fopen(path, "w");
    snprintf(path, sizeof path, "%s/%s/manifest.txt", dir, name);
    FILE *fz = fopen(path, "w");
    snprintf(path, sizeof path, "%s/%s/snap.txt", dir, name);
    FILE *fp = fopen(path, "w");
    if (!fm || !fs || !fz || !fp) { perror("open"); exit(2); }
    double snap = dataset_snap(name);
    fprintf(fp, "%.6f\n", snap);
    int zc = 0;
    for (int i = 0; i < n; ++i) {
        fprintf(fm, "%.6f %.6f %.6f %.6f\n", m[i].x0, m[i].y0, m[i].x1, m[i].y1);
        fprintf(fs, "%.6f %.6f %.6f %.6f\n", s[i].x0, s[i].y0, s[i].x1, s[i].y1);
        if (zero_flag[i]) { fprintf(fz, "%d\n", i); ++zc; }
    }
    fclose(fm); fclose(fs); fclose(fz); fclose(fp);
    fprintf(stderr, "gen %s: segs=%d designed_truth=%d designed_zero=%d snap=%g\n",
            name, n, designed_truth, zc, snap);
}

/* 相交对：两条等长线段中心重合于 P、角度随机，master 精确相交。 */
static void cross_pair(Seg *m, Seg *s, int *n, double px, double py,
                       double len, double jitter, double angle_lo) {
    double a1 = rnd() * 3.14159265;
    double a2 = a1 + angle_lo + rnd() * (1.5 - angle_lo);
    double c1 = cos(a1), sn1 = sin(a1), c2 = cos(a2), sn2 = sin(a2);
    add(m, s, n, px - len * c1, py - len * sn1, px + len * c1, py + len * sn1, jitter, 0);
    add(m, s, n, px - len * c2, py - len * sn2, px + len * c2, py + len * sn2, jitter, 0);
}

/* 平行近失对：两条等长、同方向、投影重叠的平行线段，法向精确间隔 gap。
 * master 精确判不交，段间最小距离恰为 gap（端点噪声后在 gap 量级）。
 * 这是反解容差的可控来源：gap 小于 snap 即真值对，大于 snap 即假候选。 */
static void parallel_pair(Seg *m, Seg *s, int *n, double px, double py,
                          double len, double jitter, double gap) {
    double a = rnd() * 3.14159265;
    double c = cos(a), sn = sin(a);
    double nx = -sn, ny = c;
    add(m, s, n, px - len * c, py - len * sn, px + len * c, py + len * sn, jitter, 0);
    double bx = px + gap * nx, by = py + gap * ny;
    m[*n] = (Seg){bx - len * c, by - len * sn, bx + len * c, by + len * sn};
    double j = jitter;
    s[*n] = (Seg){
        nz(m[*n].x0 + rnd() * j), nz(m[*n].y0 + rnd() * j),
        nz(m[*n].x1 + rnd() * j), nz(m[*n].y1 + rnd() * j)};
    ++*n;
}

/* T 接：第一条段的端点 P 落在第二条长段内部。主段半长 30（远大于噪声），
 * 噪声后 P 仍在主段邻域内，gap 保持在抖动量级。 */
static void t_pair(Seg *m, Seg *s, int *n, double px, double py, double jitter) {
    double a = rnd() * 3.14159265;
    double L = 8 + rnd() * 4;
    add(m, s, n, px, py, px + L * cos(a), py + L * sin(a), jitter, 0);
    double b = a + 0.6 + rnd() * 1.2;
    add(m, s, n, px - 30 * cos(b), py - 30 * sin(b),
        px + 30 * cos(b), py + 30 * sin(b), jitter, 0);
}

/* 真共线重叠对：两条水平段共享同一支撑线 y=py（角度恒 0，法向即 y 轴，
 * 端点噪声的法向分量不被长杠杆放大），x 区间重叠。master 精确共线，gold 判重叠，
 * segs 的 gap 保持在端点抖动量级。 */
static void collinear_pair(Seg *m, Seg *s, int *n, double px, double py, double jitter) {
    add(m, s, n, px - 10, py, px + 12, py, jitter, 0);
    add(m, s, n, px - 4, py, px + 16, py, jitter, 0);
}

/* 端点相碰对 */
static void touch_pair(Seg *m, Seg *s, int *n, double px, double py, double jitter) {
    double a1 = rnd() * 3.14159, a2 = rnd() * 3.14159;
    double L = 14 + rnd() * 6, M = 14 + rnd() * 6;
    add(m, s, n, px, py, px + L * cos(a1), py + L * sin(a1), jitter, 0);
    add(m, s, n, px, py, px - M * cos(a2), py - M * sin(a2), jitter, 0);
}

#define MAXN 300

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <outdir>\n", argv[0]); return 1; }
    Seg m[MAXN], s[MAXN];
    int zflag[MAXN] = {0};
    int n = 0, truth = 0;

    /* A: 一般场景。段半长 8（一成红线 ~1.6，实测最短 ~1.8）。
     * 25 相交对（master 精确相交，提供严格真值基线）
     * + 25 平行近失真值对（法向缝隙 0.08，gold snap=0.12 保留，反解容差 ~0.1，ACCEPT）
     * 另在右侧放 25 平行近失非真值对（缝隙 0.40，snap 外，容差过大就误吸）
     * + 100 隔离填充段。 */
    for (int k = 0; k < 25; ++k) {
        double px = 20.0 * (k % 5), py = 20.0 * (k / 5);
        cross_pair(m, s, &n, px, py, 8.0, 0.02, 0.25);
        truth += 1;
    }
    for (int k = 0; k < 25; ++k) {
        double px = 120.0 + 20.0 * (k % 5), py = 20.0 * (k / 5);
        parallel_pair(m, s, &n, px, py, 8.0, 0.02, 0.08);
        truth += 1;
    }
    for (int k = 0; k < 25; ++k) {
        double px = 240.0 + 20.0 * (k % 5), py = 20.0 * (k / 5);
        parallel_pair(m, s, &n, px, py, 8.0, 0.02, 0.40);
    }
    for (int k = 0; k < 100; ++k) {
        double px = 5000.0 + 5.0 * (k % 10), py = 5000.0 + 5.0 * (k / 10);
        add(m, s, &n, px, py, px + 1.5, py + 1.2, 0.02, 0);
    }
    write_set(argv[1], "A_general", m, s, n, truth, zflag);

    /* B: 大坐标近掠。坐标 ~1e6，段半长 80（一成红线 ~16），抖动 0.4。
     * 30 相交对（严格真值）+ 30 平行近失真值对（缝隙 1.2，snap=2 保留，
     * 反解容差 ~1，ACCEPT）；右侧 30 平行近失非真值对（缝隙 8，snap 外）。
     * 组中心间距 500，杜绝跨组吸附。 */
    n = 0; truth = 0;
    memset(zflag, 0, sizeof zflag);
    for (int k = 0; k < 30; ++k) {
        double px = 1e6 + 500.0 * (k % 6), py = 1e6 + 500.0 * (k / 6);
        cross_pair(m, s, &n, px, py, 80.0, 0.4, 0.01);
        truth += 1;
    }
    for (int k = 0; k < 30; ++k) {
        double px = 1e6 + 4000.0 + 500.0 * (k % 6), py = 1e6 + 500.0 * (k / 6);
        parallel_pair(m, s, &n, px, py, 80.0, 0.4, 1.2);
        truth += 1;
    }
    for (int k = 0; k < 30; ++k) {
        double px = 1e6 + 8000.0 + 500.0 * (k % 6), py = 1e6 + 500.0 * (k / 6);
        parallel_pair(m, s, &n, px, py, 80.0, 0.4, 8.0);
    }
    write_set(argv[1], "B_grazing", m, s, n, truth, zflag);

    /* C: 端点/共线/T 接 + 6 个零长点；全部小抖动 2e-4，退化身份走 manifest。
     * 期望小容差即可零漏判，容差远低于一成红线 -> ACCEPT，退化统计单独可见。 */
    n = 0; truth = 0;
    memset(zflag, 0, sizeof zflag);
    for (int k = 0; k < 15; ++k) {
        double px = 40.0 * (k % 5), py = 40.0 * (k / 5);
        t_pair(m, s, &n, px + 10, py + 10, 2e-4); truth += 1;
    }
    for (int k = 0; k < 10; ++k) {
        double px = 300.0 + 40.0 * (k % 5), py = 40.0 * (k / 5);
        collinear_pair(m, s, &n, px, py, 2e-4); truth += 1;
    }
    for (int k = 0; k < 5; ++k) {
        double px = 600.0 + 40.0 * k, py = 600.0;
        touch_pair(m, s, &n, px, py, 2e-4); truth += 1;
    }
    /* 6 个零长点：各自落在专属区域的独立水平锚段内部，与其它几何隔离。
     * 点沿锚段方向做 2e-4 局部抖动，master 精确点在线上（gold 判交），
     * 退化身份由 manifest 保留，segs 的 gap 小而不恒为 0。 */
    for (int k = 0; k < 6; ++k) {
        double px = 5000.0 + 60.0 * k, py = 5000.0;
        add(m, s, &n, px - 15, py, px + 15, py, 2e-4, 1);
        add_point_anchored(m, s, &n, zflag, px, py, 30.0, 0.0, 2e-4);
        truth += 1;
    }
    for (int k = 0; k < 8; ++k)
        add(m, s, &n, 8000.0 + 8 * k, 8000.0, 8000.0 + 8 * k + 2, 8001.5, 2e-4, 0);
    write_set(argv[1], "C_endpoint", m, s, n, truth, zflag);

    /* D: 短段大噪声。段长 3~5（一成红线 ~0.3~0.5），端点抖动 0.7。
     * 14 相交对 + 14 平行近失真值对（缝隙 0.6，噪声把 gap 撑过一成线 -> REJECT）
     * + 4 零长点对（manifest 标记）。 */
    n = 0; truth = 0;
    memset(zflag, 0, sizeof zflag);
    for (int k = 0; k < 14; ++k) {
        double px = 30.0 * (k % 7), py = 30.0 * (k / 7);
        double len = 3.0 + fabs(rnd()) * 2.0;
        cross_pair(m, s, &n, px, py, len, 0.7, 0.3);
        truth += 1;
    }
    for (int k = 0; k < 14; ++k) {
        double px = 300.0 + 30.0 * (k % 7), py = 30.0 * (k / 7);
        double len = 3.0 + fabs(rnd()) * 2.0;
        parallel_pair(m, s, &n, px, py, len, 0.7, 0.6);
        truth += 1;
    }
    for (int k = 0; k < 4; ++k) {
        double px = 900.0 + 20 * k, py = 900.0;
        add(m, s, &n, px - 2, py, px + 2, py, 0.7, 1);
        add_point_anchored(m, s, &n, zflag, px, py, 4.0, 0.0, 0.5);
        truth += 1;
    }
    write_set(argv[1], "D_snapbig", m, s, n, truth, zflag);
    return 0;
}
