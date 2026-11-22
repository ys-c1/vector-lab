#ifndef RMETRIC_H
#define RMETRIC_H
#include <stddef.h>

/* 两帧之间的平均绝对差（0..255），作为跳变度。 */
double metric_jump(const unsigned char *a, const unsigned char *b, size_t n);

/* 一串相邻帧的最大跳变度，用来衡量缩放序列里最刺眼的那一步。 */
double metric_sequence_max_jump(const unsigned char *const *frames, size_t n, int count);

/* 同一串帧的平均跳变度。 */
double metric_sequence_mean_jump(const unsigned char *const *frames, size_t n, int count);

#endif
