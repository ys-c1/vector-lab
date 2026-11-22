/*
 * 分配/拷贝计数器：用链接器 --wrap 拦截本工程代码中的
 * malloc/calloc/realloc/aligned_alloc/posix_memalign/free 与
 * memcpy/memmove/mempcpy。变换内核里任何一次分配或整块拷贝都会被计数，
 * 作为“没有第二次拷贝”的运行时证据（perf/tuning.md 第 5 节）。
 *
 * 注意：只拦截本二进制内对这些符号的调用；glibc 内部调用不受影响。
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

unsigned long xb_alloc_calls;     /* 分配类调用次数 */
size_t        xb_alloc_bytes;     /* 分配累计字节 */
unsigned long xb_copy_calls;      /* memcpy/memmove/mempcpy 调用次数 */
size_t        xb_copy_bytes;      /* 拷贝累计字节 */

void *__real_malloc(size_t);
void  __real_free(void *);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_aligned_alloc(size_t, size_t);
int   __real_posix_memalign(void **, size_t, size_t);
void *__real_memcpy(void *, const void *, size_t);
void *__real_memmove(void *, const void *, size_t);
void *__real_mempcpy(void *, const void *, size_t);

void *__wrap_malloc(size_t n) {
    ++xb_alloc_calls; xb_alloc_bytes += n;
    return __real_malloc(n);
}
void __wrap_free(void *p) { __real_free(p); }
void *__wrap_calloc(size_t k, size_t n) {
    ++xb_alloc_calls; xb_alloc_bytes += k * n;
    return __real_calloc(k, n);
}
void *__wrap_realloc(void *p, size_t n) {
    ++xb_alloc_calls; xb_alloc_bytes += n;
    return __real_realloc(p, n);
}
void *__wrap_aligned_alloc(size_t a, size_t n) {
    ++xb_alloc_calls; xb_alloc_bytes += n;
    return __real_aligned_alloc(a, n);
}
int __wrap_posix_memalign(void **m, size_t a, size_t n) {
    ++xb_alloc_calls; xb_alloc_bytes += n;
    return __real_posix_memalign(m, a, n);
}
void *__wrap_memcpy(void *d, const void *s, size_t n) {
    ++xb_copy_calls; xb_copy_bytes += n;
    return __real_memcpy(d, s, n);
}
void *__wrap_memmove(void *d, const void *s, size_t n) {
    ++xb_copy_calls; xb_copy_bytes += n;
    return __real_memmove(d, s, n);
}
void *__wrap_mempcpy(void *d, const void *s, size_t n) {
    ++xb_copy_calls; xb_copy_bytes += n;
    return __real_mempcpy(d, s, n);
}
