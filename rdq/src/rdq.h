#ifndef RDQ_H
#define RDQ_H
#include <stddef.h>

/* 目标设备：4 位灰度（16 级），见 RQ_DEV_BITS。 */

#define RQ_DEV_BITS 4
#define RQ_DEV_LEVELS (1 << RQ_DEV_BITS)

typedef struct {
    int w, h;
    int block;      /* 块边长，像素 */
    int dmin, dmax; /* 每块候选位深，1..RQ_DEV_BITS */
} RQParams;

typedef struct {
    int w, h;
    int block;
    int bw, bh;          /* 块列数/行数 */
    int dmin, dmax;
    int lossless;        /* 1: 纯色，只存一个 8 位灰阶 */
    unsigned char *bits; /* bw*bh，每块位深 */
    unsigned char *codes;/* bw*bh*block*block，每块调色板索引 */
    unsigned char *pal;  /* bw*bh*RQ_DEV_LEVELS，每块调色板 */
    int nbytes;          /* 序列化后字节数 */
} RQImage;

typedef struct {
    double psnr;         /* dB；无损时为 INFINITY */
    long sse;
    int nbytes;
    double lambda;
} RQStats;

int rq_validate(const RQParams *p);

/* 编码，budget 为序列化后字节数上限。失败返回负值并把 *badparam 指向参数名：
 * -1 参数/内存，-2 预算（budget<=0、过小或分配后仍超）。 */
int rq_encode(const unsigned char *img, const RQParams *p, long budget,
              RQImage *out, RQStats *st, const char **badparam);

int rq_decode(const RQImage *q, unsigned char *out);
int rq_serialize(const RQImage *q, unsigned char *buf, size_t cap);
int rq_deserialize(const unsigned char *buf, size_t n, RQImage *q);

void rq_free(RQImage *q);
#endif
