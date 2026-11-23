#include "backend.h"
#include "raster.h"
#include "tile.h"

int raster_backend_valid(int backend) {
    return (backend >= RASTER_BACKEND_MIN && backend <= RASTER_BACKEND_MAX) ? 0 : -1;
}

int raster_render(int backend, const RScene *s, const RBox *box,
                  int w, int h, int ss, unsigned char *buf) {
    /* 选择器为 0 或越界：停机，不触碰 buf。 */
    if (raster_backend_valid(backend) != 0) return -2;
    if (backend == RASTER_BACKEND_SCANLINE)
        return raster_fill(s, box, w, h, ss, buf);
    if (backend == RASTER_BACKEND_TILE)
        return raster_fill_tiled(s, box, w, h, ss, buf);
    return -2;
}
