#ifndef CLIP_H
#define CLIP_H
#include <stdio.h>

#define CLIP_MAX_VERTS 64
#define CLIP_MAX_PIECES 64

typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[CLIP_MAX_VERTS]; } CPoly;

/* 错误码：< 0 表示失败 */
#define CLIP_E_ARG         -1 /* 参数空指针/顶点不足(<3)/容量为 0 */
#define CLIP_E_PIECE_VERTS -2 /* 某号凸块顶点不足或退化，块号见 err_piece */
#define CLIP_E_NO_PIECES   -3 /* 凸拆分得到 0 个块（多边形自交等） */
#define CLIP_E_CAP         -4 /* 输出数组容量不足 */

/*
 * 凸窗口 Sutherland-Hodgman 裁剪。
 * poly 与 win 均按逆时针给出；窗口为凹多边形时结果不正确，
 * 凹窗口请用 clip_concave。
 */
int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);

/*
 * 凹窗口凸拆分：耳切三角化后做 Hertel-Mehlhorn 凸块合并。
 * 成功返回块数(>=1)；失败返回负错误码，err_piece 给出问题块号（无块号时为 -1）。
 */
int clip_decompose(const CPoint *win, int wn, CPoly *pieces, int cap, int *err_piece);

/*
 * 凹窗口裁剪：先拆块，再逐块凸裁剪，最后按面积求和合并
 * （凸块互不重叠，并集面积 = 各块裁剪面积之和）。
 * split=0 时跳过拆分，直接走 clip_convex，输出与现状逐顶点相同。
 * 成功返回 0；*nouts 为非空裁剪结果块数，*union_area 为并集面积，
 * *npieces 为窗口拆出的凸块总数。
 */
int clip_concave(const CPoint *poly, int n, const CPoint *win, int wn,
                 int split, CPoly *outs, int cap, int *nouts,
                 double *union_area, int *npieces, int *err_piece);

/* 块表导出 CSV：piece,vertex,x,y,piece_area；返回写入字节数，失败返回负值 */
long clip_pieces_csv(const CPoly *pieces, int npieces, FILE *fp);

/* 错误码转文字（含块号） */
const char *clip_strerror(int code, int err_piece, char *buf, int buflen);

double clip_area(const CPoly *p);
#endif
