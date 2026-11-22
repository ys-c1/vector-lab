#ifndef CLIP_H
#define CLIP_H

/* 裁剪缓冲容量。逐步裁剪会累积顶点（凸多边形 ∩ 凸窗口最多 n+wn 个），
 * 旧实现的 64 点固定缓冲会静默丢点，这里显式给出上限并在超限时报错。 */
#define CLIP_MAX_VERTS 1024

typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[CLIP_MAX_VERTS]; } CPoly;

/* 用凸窗口裁剪多边形（Sutherland–Hodgman）。
 * 窗口方向自动归一化：顺时针窗口按逆时针处理。
 * 返回：0 成功；-1 参数非法（含顶点不足三个）；-2 窗口退化
 *       （零面积或相邻顶点重合）；-3 顶点数超过 CLIP_MAX_VERTS。 */
int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);

/* 有符号面积（逆时针为正）。调用方需要面积大小时取 fabs。 */
double clip_area(const CPoly *p);

#endif
