#ifndef CLIP_H
#define CLIP_H
#define CLIP_MAX 256
typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[CLIP_MAX]; } CPoly;
/* a*x + b*y + c = 0, (a,b) 为指向环内的单位法向 */
typedef struct { double a, b, c; } CLine;

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);
double clip_area(const CPoly *p);

/* 收缩返回码 */
#define CLIP_SHRINK_OK     0
#define CLIP_SHRINK_EARG  -1  /* 参数非法（含退化零长边） */
#define CLIP_SHRINK_ENEG  -2  /* 偏移量为负 */
#define CLIP_SHRINK_SELFX -3  /* 收缩环穿出偏移核或自交，err_edge 指出边号 */

/* 逐边内偏移线：lines[i] 对应边 i (pts[i] -> pts[i+1])，沿内法向平移 dist。
   返回 0 成功；-1 参数非法；-2 dist < 0。 */
int clip_offset_lines(const CPoly *in, double dist, CLine *lines, int max_lines);

/* 两段式收缩：先按每条边偏移 dist，再求相邻偏移线交点拼回环。
   dist == 0 时原样复制（结果与未开偏移的当前版本一致）。
   成功返回 0；dist < 0 返回 CLIP_SHRINK_ENEG；
   收缩环穿出偏移核或自交返回 CLIP_SHRINK_SELFX，
   且 err_edge（非 NULL 时）给出涉事边号。 */
int clip_shrink(const CPoly *in, double dist, CPoly *out, int *err_edge);
#endif
