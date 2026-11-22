#include "fit.h"
#include "export.h"
#include "raster.h"
#include <stdlib.h>

/* 渲染一个方形尺寸，返回点亮像素数；失败返回 -1。buf 由调用方提供。 */
static int render_on(const RScene *s, RPlace place, RBox win,
                     int size, unsigned char *buf) {
    if (pipeline_render(s, place, win, size, size, buf) != 0) return -1;
    return export_count_on(buf, size, size);
}

/* 可辨认：至少 1 个点亮像素，且点亮占比不低于满幅参考的
 * FIT_RECOG_DENSITY_RATIO 倍（用整数乘法比较，避免浮点误差）。 */
static int recognizable(int size, int on, int cap, int ref_on) {
    if (on <= 0 || ref_on <= 0) return 0;
    /* on / (size*size) >= ratio * ref_on / (cap*cap) */
    long long lhs = (long long)on * (long long)cap * (long long)cap;
    long long rhs = (long long)(FIT_RECOG_DENSITY_RATIO * 100.0 + 0.5)
                  * (long long)ref_on * (long long)size * (long long)size;
    return lhs * 100LL >= rhs ? 1 : 0;
}

const char *fit_status_str(int status) {
    switch (status) {
    case FIT_OK:                 return "ok";
    case FIT_BAD_ARG:            return "bad-argument";
    case FIT_BUDGET_BELOW_HEADER:return "budget-below-header";
    case FIT_RENDER_FAIL:        return "render-failed";
    case FIT_CAP_TOO_SMALL:      return "cap-too-small";
    case FIT_NO_FIT:             return "no-size-fits-budget";
    case FIT_NOT_RECOGNIZABLE:   return "not-recognizable";
    default:                     return "unknown";
    }
}

int fit_solve(const RScene *s, RPlace place, RBox win, int cap,
              long long budget, RFitResult *out) {
    if (!out) return FIT_BAD_ARG;
    RFitResult r = {0};
    r.budget = budget;
    r.cap = cap;
    r.header_min = export_pgm_header_len(1, 1);
    if (r.header_min < 0) r.header_min = 0;

    if (!s || cap <= 0 || budget <= 0) { r.status = FIT_BAD_ARG; *out = r; return r.status; }
    if (cap > FIT_MAX_CAP) cap = FIT_MAX_CAP;
    r.cap = cap;

    /* 预算连最小头部都放不下：直接报出，不渲染。 */
    if (budget < (long long)r.header_min) {
        r.status = FIT_BUDGET_BELOW_HEADER;
        r.bytes = r.header_min;
        *out = r;
        return r.status;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)cap * (size_t)cap);
    if (!buf) { r.status = FIT_RENDER_FAIL; *out = r; return r.status; }

    int ref_on = render_on(s, place, win, cap, buf);
    if (ref_on < 0) { free(buf); r.status = FIT_RENDER_FAIL; *out = r; return r.status; }
    r.ref_on = ref_on;
    if (ref_on == 0) { free(buf); r.status = FIT_CAP_TOO_SMALL; *out = r; return r.status; }

    /* 预算连 1x1（头部 + 1 像素）都放不下：没有任何带像素尺寸可行。 */
    long long min_file = export_pgm_bytes(1, 1);
    if (budget < min_file) {
        r.status = FIT_NO_FIT;
        r.bytes = min_file;
        free(buf);
        *out = r;
        return r.status;
    }

    /* 二分：最大的 size 使 export_pgm_bytes(size,size) <= budget，且不超过 cap。 */
    int lo = 1, hi = cap, best = 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        long long b = export_pgm_bytes(mid, mid);
        if (b < 0) { hi = mid - 1; continue; }
        if (b <= budget) { best = mid; lo = mid + 1; }
        else hi = mid - 1;
    }

    int on = render_on(s, place, win, best, buf);
    if (on < 0) { free(buf); r.status = FIT_RENDER_FAIL; *out = r; return r.status; }

    r.size = best;
    r.bytes = export_pgm_bytes(best, best);
    r.on = on;
    r.recognizable = recognizable(best, on, cap, ref_on);

    /* 从 1 向上找仍可辨认的最小边长（找到的第一个即下限）。 */
    r.min_size = 0;
    for (int z = 1; z <= cap; ++z) {
        int oz = render_on(s, place, win, z, buf);
        if (oz < 0) { free(buf); r.status = FIT_RENDER_FAIL; *out = r; return r.status; }
        if (recognizable(z, oz, cap, ref_on)) { r.min_size = z; break; }
    }

    if (!r.recognizable) {
        r.status = FIT_NOT_RECOGNIZABLE;
        r.size = 0;
    } else {
        r.status = FIT_OK;
    }
    free(buf);
    *out = r;
    return r.status;
}
