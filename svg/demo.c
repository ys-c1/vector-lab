#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* 把路径的第一个子路径（首个闭合段链）按 gold/ref.c 的输入格式导出：
 * 每行 "x y"，不重复输出闭合点。供 ref pixels/area 消费。 */
static int dump_polygon(const VPath *p, const char *out) {
    FILE *f = fopen(out, "w");
    if (!f) {
        printf("error: cannot open %s\n", out);
        return 1;
    }
    for (int i = 0; i < p->count; ++i) {
        if (i > 0) {
            VPoint prev = p->segs[i - 1].b;
            /* 段不连续 => 进入下一个子路径，只取第一个。 */
            if (prev.x != p->segs[i].a.x || prev.y != p->segs[i].a.y) break;
        }
        fprintf(f, "%.17g %.17g\n", p->segs[i].a.x, p->segs[i].a.y);
    }
    fclose(f);
    return 0;
}

static int render(const char *d, int w, int h, const char *out) {
    /* 当场停并指名参数：宽或高为零/负。 */
    if (w == 0) {
        printf("error: width must be positive, got 0 (param: width)\n");
        return 2;
    }
    if (w < 0) {
        printf("error: width must be positive, got %d (param: width)\n", w);
        return 2;
    }
    if (h == 0) {
        printf("error: height must be positive, got 0 (param: height)\n");
        return 2;
    }
    if (h < 0) {
        printf("error: height must be positive, got %d (param: height)\n", h);
        return 2;
    }

    VPath p;
    int rc = svg_parse(d, &p);
    if (rc != 0) {
        printf("error: svg_parse failed (%d)\n", rc);
        return 1;
    }

    /* 内存上限：图本身 3 倍。这里只分配 1 倍（w*h 字节），
     * 不建超采样中间缓冲——64 个子点在寄存器里直接累计。
     * RENDER_CAP 是可渲染图像的硬上限，防止 overcommit 下 malloc 成功、
     * 触页时被 OOM 杀掉，保证超尺寸是带信息的干净失败。 */
    enum { RENDER_CAP = 1u << 30 }; /* 1 GiB */
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > RENDER_CAP) {
        printf("error: image %zu bytes exceeds render cap %d bytes "
               "(param: width/height)\n", pixels, RENDER_CAP);
        return 2;
    }
    unsigned char *buf = (unsigned char *)malloc(pixels);
    if (!buf) {
        printf("error: out of memory allocating %zu bytes\n", pixels);
        return 3;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = svg_fill(&p, w, h, buf);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (rc != 0) {
        printf("error: svg_fill failed (%d)\n", rc);
        free(buf);
        return 1;
    }
    long ms = (t1.tv_sec - t0.tv_sec) * 1000L
            + (t1.tv_nsec - t0.tv_nsec) / 1000000L;

    if (svg_save_pgm(out, buf, w, h) != 0) {
        printf("error: cannot write %s\n", out);
        free(buf);
        return 1;
    }

    struct stat st;
    long bytes = stat(out, &st) == 0 ? (long)st.st_size : -1;
    /* 同一张图记一笔：字节数、像素数、毫秒。 */
    printf("render %s bytes %ld pixels %zu ms %ld\n", out, bytes, pixels, ms);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        /* 缺省演示：嵌套矩形路径，50x50。 */
        const char *d =
            "M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z";
        return render(d, 50, 50, "demo.pgm");
    }
    if (argc >= 2 && !strcmp(argv[1], "render")) {
        if (argc < 6) {
            printf("usage: %s render <svg-path-d> <width> <height> <out.pgm>\n",
                   argv[0]);
            return 1;
        }
        return render(argv[2], atoi(argv[3]), atoi(argv[4]), argv[5]);
    }
    if (argc >= 2 && !strcmp(argv[1], "poly")) {
        if (argc < 4) {
            printf("usage: %s poly <svg-path-d> <out.txt>\n", argv[0]);
            return 1;
        }
        VPath p;
        if (svg_parse(argv[2], &p) != 0 || p.count == 0) {
            printf("error: invalid or empty svg path\n");
            return 1;
        }
        return dump_polygon(&p, argv[3]);
    }
    printf("usage: %s [render <d> <w> <h> <out.pgm> | poly <d> <out.txt>]\n",
           argv[0]);
    return 1;
}
