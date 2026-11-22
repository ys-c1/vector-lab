#ifndef HULL_H
#define HULL_H
typedef struct { double x, y; } HPoint;
typedef struct { int count; HPoint pts[256]; } HPoly;

/* 凸层分解（onion peeling）结果：
 * layers[0..nlayers-1] 为逐层剥下的凸包，0 是最外层；
 * rem[0..nrem-1] 为剥完后不足以再构成凸包的剩余点。 */
#define HULL_MAX_LAYERS 64
typedef struct {
    int nlayers;
    int nrem;
    HPoly layers[HULL_MAX_LAYERS];
    HPoint rem[256];
} LResult;

typedef struct { double minx, miny, maxx, maxy; } HBox;

int hull_convex(const HPoint *pts, int n, HPoly *out);
int hull_min_rect(const HPoint *pts, int n, double *cx, double *cy, double *w, double *h, double *angle);

/* 凸层分解。返回 0 成功；-1 参数错误；-2 层数超过 HULL_MAX_LAYERS。 */
int hull_layers(const HPoint *pts, int n, LResult *res);

/* 多边形面积（shoelace）。 */
double hull_poly_area(const HPoly *p);

/* 反解层号：返回首个使“本层面积 / 最外层面积 < target”的层下标 k（0 起），
 * 即已剥层数（层号，1 起）为 k+1。
 * 返回 -1 参数错误；-3 全部层剥完仍未达标（层号上限为总层数）。 */
int hull_solve_layer(const LResult *res, double target);

/* 把 0..klayer 各层轮廓画进灰度 PGM 缓冲：
 * 像素中心落在第几层内就取该层灰度，层 i 灰度 = max(16, 255 - 16*i)，
 * 所有层外为 0。margin 为包围盒外扩相对比例（如 0.02）。 */
int hull_layers_pgm(const LResult *res, int klayer, int w, int h,
                    double margin, unsigned char *buf);

/* 点集包围盒。 */
int hull_box(const HPoint *pts, int n, double margin, HBox *box);
#endif
