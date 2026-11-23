/* faketick.c — LD_PRELOAD shim. 把所有“墙钟”入口钉在 FAKETICK_EPOCH
 * 指定的瞬间，用于证明渲染产物不依赖当前时间。随机源不动（本任务
 * 关心的是时间）。除 time/gettimeofday 外，还覆盖 clock_gettime、
 * clock、times 以及 localtime/gmtime 的转写，确保 TZ 也走真实路径。 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>

static long fake_epoch(void) {
    const char *e = getenv("FAKETICK_EPOCH");
    return e ? atol(e) : 0L;
}

time_t time(time_t *t) {
    time_t v = (time_t)fake_epoch();
    if (t) *t = v;
    return v;
}

int gettimeofday(struct timeval *tv, void *tz) {
    /* glibc 标注 tv 非空；真实调用方也都传非空指针。 */
    (void)tz;
    tv->tv_sec = fake_epoch();
    tv->tv_usec = 0;
    return 0;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    (void)clk_id;
    tp->tv_sec = fake_epoch();
    tp->tv_nsec = 0;
    return 0;
}

clock_t clock(void) { return (clock_t)0; }

clock_t times(struct tms *buf) {
    if (buf) memset(buf, 0, sizeof(*buf));
    return (clock_t)0;
}

struct tm *localtime(const time_t *t) {
    static struct tm *(*real_fn)(const time_t *);
    time_t forced = t ? *t : (time_t)fake_epoch();
    if (!real_fn) real_fn = dlsym(RTLD_NEXT, "localtime");
    return real_fn(&forced);
}

struct tm *localtime_r(const time_t *t, struct tm *result) {
    static struct tm *(*real_fn)(const time_t *, struct tm *);
    time_t forced = t ? *t : (time_t)fake_epoch();
    if (!real_fn) real_fn = dlsym(RTLD_NEXT, "localtime_r");
    return real_fn(&forced, result);
}

struct tm *gmtime(const time_t *t) {
    static struct tm *(*real_fn)(const time_t *);
    time_t forced = t ? *t : (time_t)fake_epoch();
    if (!real_fn) real_fn = dlsym(RTLD_NEXT, "gmtime");
    return real_fn(&forced);
}

struct tm *gmtime_r(const time_t *t, struct tm *result) {
    static struct tm *(*real_fn)(const time_t *, struct tm *);
    time_t forced = t ? *t : (time_t)fake_epoch();
    if (!real_fn) real_fn = dlsym(RTLD_NEXT, "gmtime_r");
    return real_fn(&forced, result);
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    static size_t (*real_fn)(char *, size_t, const char *, const struct tm *);
    if (!real_fn) real_fn = dlsym(RTLD_NEXT, "strftime");
    return real_fn(s, max, fmt, tm);
}
