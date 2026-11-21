#ifndef PICK_H
#define PICK_H
typedef struct { double x, y; } PPoint;
typedef struct { PPoint p0, p1; } PSeg;
typedef struct { double minx, miny, maxx, maxy; } PBox;
typedef struct { int seg; double t; double u; PPoint at; } PHit;
typedef struct { int boxes_tested; int segs_tested; } PStats;
typedef struct { const PSeg *segs; PBox *boxes; int n; } PIndex;

enum {
    PICK_OK = 0,
    PICK_ERR_ARG = -1,   /* 空指针或数量非法 */
    PICK_ERR_DIR = -2,   /* 方向向量为零 */
    PICK_ERR_DEGEN = -3, /* 段退化，err_seg 给出段号 */
    PICK_ERR_MEM = -4
};

/* 建索引：预计算每段包围盒并校验退化段；segs 须活得比 idx 久 */
int pick_index_build(const PSeg *segs, int n, PIndex *idx, int *err_seg);
void pick_index_free(PIndex *idx);

/* 最近命中。cull!=0 先包围盒剔除再精确求解；无命中时 out->seg == -1。
   命中距离为射线参数 t（d 为单位向量时即欧氏距离）。 */
int pick_ray_nearest(const PIndex *idx, PPoint o, PPoint d, int cull, PHit *out, PStats *stats);

/* 全部命中，按段号升序写入 hits（至多 cap 条），返回命中数或负错误码 */
int pick_ray_all(const PIndex *idx, PPoint o, PPoint d, int cull, PHit *hits, int cap, PStats *stats);

/* 命中表导出为 CSV（seg,t,u,x,y） */
int pick_hits_export(const PHit *hits, int n, const char *path);
#endif
