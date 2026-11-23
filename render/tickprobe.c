/* 先证明 shim 真的把钟移动了，再让渲染命令被信任。 */
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

int main(void) {
    time_t t = time(NULL);
    struct timeval tv;
    struct timespec ts;
    struct tm tmv;
    gettimeofday(&tv, NULL);
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&t, &tmv);
    printf("time=%lld\n", (long long)t);
    printf("gettimeofday=%lld.%06ld\n", (long long)tv.tv_sec, (long)tv.tv_usec);
    printf("clock_gettime=%lld.%09ld\n", (long long)ts.tv_sec, ts.tv_nsec);
    printf("localtime_hour=%02d\n", tmv.tm_hour);
    return 0;
}
