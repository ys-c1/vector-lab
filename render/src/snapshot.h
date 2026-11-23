#ifndef RSNAP_H
#define RSNAP_H
#include "scene.h"
#include "pipeline.h"
#include <stdio.h>

/* 二进制快照：当前场景 + 变换(RPlace) + 窗口(RBox) + 输出宽高。
 * 布局全部小端、定长字段，偏移即文件字节偏移（见 RSNAP_OFF_*）。
 *
 * [0..3]   magic "RSNP"
 * [4..7]   version u32
 * [8..11]  w  i32      [12..15] h i32
 * [16..79] place: tx ty rot scale，4 个 f64
 * [48..79] win:   minx miny maxx maxy，4 个 f64
 * [80..83] npolys i32
 * [84..]   每个多边形: n i32，随后 n 个 (x,y) f64
 * 不允许尾随字节。
 */
#define RSNAP_MAGIC "RSNP"
#define RSNAP_VERSION 1u

#define RSNAP_OFF_MAGIC   0
#define RSNAP_OFF_VERSION 4
#define RSNAP_OFF_W       8
#define RSNAP_OFF_H       12
#define RSNAP_OFF_PLACE   16
#define RSNAP_OFF_WIN     48
#define RSNAP_OFF_NPOLYS  80
#define RSNAP_OFF_RECORDS 84
#define RSNAP_HEADER_LEN  RSNAP_OFF_RECORDS

/* 错误码。*err_off 给出拒绝位置的文件字节偏移；不适用时为 -1。 */
enum {
    SNAP_OK = 0,
    SNAP_EIO = -1,        /* 文件读写错误              */
    SNAP_ETRUNC = -2,     /* 字段截断/文件过短         */
    SNAP_EVERSION = -3,   /* 版本号不符                */
    SNAP_EMAGIC = -4,     /* 魔数不符                  */
    SNAP_EINVAL = -5,     /* 字段非法（NaN/越界/退化） */
    SNAP_ETRAIL = -6,     /* 存在尾随字节              */
    SNAP_EDISABLED = -7   /* 快照功能编译关闭          */
};

int snapshot_save(const char *path, const RScene *s, RPlace place,
                  const RBox *win, int w, int h, long *err_off);
int snapshot_load(const char *path, RScene *s, RPlace *place,
                  RBox *win, int *w, int *h, long *err_off);
const char *snapshot_strerror(int code);

/* 快照文件应有字节数（保存前可预知），参数非法时返回 -1。 */
long snapshot_size_bytes(const RScene *s);
#endif
