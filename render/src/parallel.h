#ifndef RPARALLEL_H
#define RPARALLEL_H

#include "scene.h"
#include "pipeline.h"
#include <stddef.h>

/* 分块并发渲染的配置。
 * threads: 工作线程数，允许 1..4。
 * block:   每个任务块覆盖的行数，必须能整除图像高度。
 * 每个块只写自己那些行的像素，块与块之间不重叠，因此输出与串行逐字节一致。 */
typedef struct {
    int threads;
    int block;
} RParallelCfg;

#define RP_THREADS_MIN 1
#define RP_THREADS_MAX 4

/* 返回码：负数为错误，0 为成功。 */
enum {
    RP_OK = 0,
    RP_ERR_ARG = -1,        /* 空指针 / 宽高非法 */
    RP_ERR_PLACE = -2,      /* 缩放为 0 */
    RP_ERR_WIN = -3,        /* 视窗退化 */
    RP_ERR_SCENE = -4,      /* 变换后多边形数量或顶点数超限 */
    RP_ERR_THREADS = -5,    /* 线程数越界 */
    RP_ERR_BLOCK = -6,      /* 块行数非正 */
    RP_ERR_DIVISIBLE = -7,  /* 块行数不能整除高度 */
    RP_ERR_ALLOC = -8,      /* 任务数组分配失败 */
    RP_ERR_THREAD = -9      /* 线程创建/回收失败 */
};

/* 校验配置与高度 h 的组合。失败时把原因写入 err（可传 NULL），返回对应错误码。 */
int rp_validate(const RParallelCfg *cfg, int h, char *err, size_t errsz);

/* 并发渲染。语义与 pipeline_render 相同，但把行区间切块后分给 cfg->threads 个线程。
 * 任一配置非法时立即返回错误、不写 buf，并通过 err 说明原因。 */
int pipeline_render_parallel(const RScene *s, RPlace place, RBox win, int w, int h,
                             const RParallelCfg *cfg, unsigned char *buf,
                             char *err, size_t errsz);

const char *rp_strerror(int code);

#endif
