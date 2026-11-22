#ifndef PALETTE_H
#define PALETTE_H

/* 聚类调色板。
 *
 * 旧逻辑（手挑调色板）：固定 8 色色表 + 最近邻量化。开关关闭时只走这条路径，
 *                     输出与冻结基线逐字节一致。
 * 新逻辑（聚类定色）：把像素当 RGB 点，k-means 迭代中心使组内平方误差最小，
 *                     中心取整后再做整数中心精修，得到每组的代表色。
 */

#define PAL_MAX_GROUPS 32       /* k 的硬上限（超过即拒绝） */
#define PAL_MAX_REPS 8          /* 手挑调色板的色数 */

typedef struct { int w, h; unsigned char *pix; } PImage;   /* RGB8，行优先 */

typedef struct { unsigned char r, g, b; } Rgb;

typedef struct { int n; Rgb v[PAL_MAX_REPS]; } Palette;

typedef struct {
    int k;                      /* 实际非空组数 */
    Rgb rep[PAL_MAX_GROUPS];    /* 代表色，按 RGB 字典序升序 */
    long count[PAL_MAX_GROUPS]; /* 每组像素数 */
    double gsse[PAL_MAX_GROUPS];/* 每组平方误差总和 */
    double gvar[PAL_MAX_GROUPS];/* 每组方差 = gsse / count（count>0） */
    double sse;                 /* 组内平方误差总和（RGB 欧氏距离平方和，未归一） */
    double var;                 /* 组内方差 = sse / 像素数 */
    int iters;                  /* k-means 迭代轮数 */
} ClusterResult;

typedef enum {
    P_OK = 0,           /* 聚类成功，组数在 [1, PAL_MAX_GROUPS] */
    P_EMPTY = -1,       /* 空图：无像素，组数为零，拒绝 */
    P_K_INVALID = -2,   /* k <= 0：组数为零，拒绝 */
    P_K_TOO_BIG = -3,   /* k > PAL_MAX_GROUPS：超过上限，拒绝 */
    P_SINGLE_COLOR = -4,/* 全图只有一种颜色：单独报告，不迭代 */
    P_IMG_BAD = -5,     /* 图像指针/尺寸非法 */
} PStatus;

/* 手挑固定调色板（旧逻辑），8 色。 */
const Palette *palette_hand(void);

/* 图像到给定整数调色板的最近邻方差（均值）。 */
double palette_var_against(const PImage *img, int n, const Rgb *pal);

/* 旧方案基线：手挑调色板下的最近邻方差。 */
double palette_legacy_var(const PImage *img);

/* k-means 聚类。返回 PStatus。
 * 成功填好 out。单色图返回 P_SINGLE_COLOR，out->k=1、rep[0] 为该颜色、
 * iters=0，供调用方单独报告（不做迭代）。
 * 迭代确定：maximin 初始化；分配平局取中心下标小者；质心平局按 RGB 字典序；
 * 收敛判据为分配不再变化；轮数有上限。 */
PStatus palette_kmeans(const PImage *img, int k, ClusterResult *out);

/* 用调色板量化整幅图（最近邻），out 为新分配缓冲（调用方 free / palette_free）。 */
int palette_apply(const PImage *img, int n, const Rgb *pal, PImage *out);

/* PGM P5（灰度，扩成 RGB）或 PPM P6 读取；只接受 maxval=255。 */
int palette_load(const char *path, PImage *out);
/* 写 PPM P6。 */
int palette_save(const char *path, const PImage *img);

void palette_free(PImage *img);

/* 图像是否只有一种颜色（>0 像素且所有像素 RGB 相同）。 */
int palette_is_single_color(const PImage *img);

#endif
