#define _POSIX_C_SOURCE 200809L

#include "parallel.h"
#include "raster.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errsz, const char *fmt, ...) {
    if (!err || errsz == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

const char *rp_strerror(int code) {
    switch (code) {
    case RP_OK: return "ok";
    case RP_ERR_ARG: return "参数非法";
    case RP_ERR_PLACE: return "缩放为 0";
    case RP_ERR_WIN: return "视窗退化";
    case RP_ERR_SCENE: return "场景变换失败";
    case RP_ERR_THREADS: return "线程数越界";
    case RP_ERR_BLOCK: return "块行数非正";
    case RP_ERR_DIVISIBLE: return "块行数不能整除高度";
    case RP_ERR_ALLOC: return "内存分配失败";
    case RP_ERR_THREAD: return "线程创建或回收失败";
    default: return "未知错误";
    }
}

int rp_validate(const RParallelCfg *cfg, int h, char *err, size_t errsz) {
    if (!cfg) {
        set_err(err, errsz, "并发配置为空");
        return RP_ERR_ARG;
    }
    if (cfg->threads < RP_THREADS_MIN || cfg->threads > RP_THREADS_MAX) {
        set_err(err, errsz, "线程数 %d 越界：允许 %d..%d，已停止",
                cfg->threads, RP_THREADS_MIN, RP_THREADS_MAX);
        return RP_ERR_THREADS;
    }
    if (cfg->block <= 0) {
        set_err(err, errsz, "块行数 %d 必须为正，已停止", cfg->block);
        return RP_ERR_BLOCK;
    }
    if (h <= 0) {
        set_err(err, errsz, "图像高度 %d 必须为正，已停止", h);
        return RP_ERR_ARG;
    }
    if (h % cfg->block != 0) {
        set_err(err, errsz,
                "块行数 %d 不能整除高度 %d（余 %d），无法均分，已停止",
                cfg->block, h, h % cfg->block);
        return RP_ERR_DIVISIBLE;
    }
    return RP_OK;
}

typedef struct {
    const RScene *scene;
    const RBox *box;
    int w, h, ss;
    int block, threads, tid;
    unsigned char *buf;
} RJob;

/* 第 tid 个线程处理块 tid, tid+threads, tid+2*threads, ...；
 * 每个块只写 [y0, y0+block) 这些行，写区间互不重叠。 */
static void *worker(void *arg) {
    RJob *j = (RJob*)arg;
    int nblocks = j->h / j->block;
    for (int b = j->tid; b < nblocks; b += j->threads) {
        int y0 = b * j->block;
        raster_fill_rows(j->scene, j->box, j->w, j->h, j->ss, y0, y0 + j->block, j->buf);
    }
    return NULL;
}

int pipeline_render_parallel(const RScene *s, RPlace place, RBox win, int w, int h,
                             const RParallelCfg *cfg, unsigned char *buf,
                             char *err, size_t errsz) {
    if (err && errsz) err[0] = '\0';
    if (!s || !buf || w <= 0) {
        set_err(err, errsz, "场景、缓冲区或宽度非法，已停止");
        return RP_ERR_ARG;
    }
    int rc = rp_validate(cfg, h, err, errsz);
    if (rc != RP_OK) return rc;
    if (place.scale == 0.0) {
        set_err(err, errsz, "缩放为 0，已停止");
        return RP_ERR_PLACE;
    }
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) {
        set_err(err, errsz, "视窗退化（max 必须大于 min），已停止");
        return RP_ERR_WIN;
    }

    RScene moved;
    rc = pipeline_stage(s, place, &moved);
    if (rc != 0) {
        set_err(err, errsz, "场景变换失败：多边形或顶点数超限，已停止");
        return rc;
    }

    memset(buf, 0, (size_t)w * (size_t)h);
    int nthreads = cfg->threads;
    RJob *jobs = (RJob*)calloc((size_t)nthreads, sizeof *jobs);
    pthread_t *tids = (pthread_t*)calloc((size_t)nthreads, sizeof *tids);
    unsigned char *started = (unsigned char*)calloc((size_t)nthreads, 1);
    if (!jobs || !tids || !started) {
        free(jobs);
        free(tids);
        free(started);
        set_err(err, errsz, "任务数组分配失败，已停止");
        return RP_ERR_ALLOC;
    }

    int made = 0;
    for (int t = 0; t < nthreads; ++t) {
        jobs[t].scene = &moved;
        jobs[t].box = &win;
        jobs[t].w = w;
        jobs[t].h = h;
        jobs[t].ss = 4;
        jobs[t].block = cfg->block;
        jobs[t].threads = nthreads;
        jobs[t].tid = t;
        jobs[t].buf = buf;
        if (pthread_create(&tids[t], NULL, worker, &jobs[t]) != 0) {
            set_err(err, errsz, "线程 %d 创建失败（已创建 %d 个），已停止", t, made);
            break;
        }
        started[t] = 1;
        ++made;
    }

    for (int t = 0; t < nthreads; ++t)
        if (started[t]) pthread_join(tids[t], NULL);

    free(jobs);
    free(tids);
    free(started);

    if (made != nthreads) return RP_ERR_THREAD;
    return RP_OK;
}
