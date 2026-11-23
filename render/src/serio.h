#ifndef RSERIO_H
#define RSERIO_H
#include "scene.h"

/*
 * 场景序列化（字节序无关）。
 *
 * 线格式 v1（所有多字节字段均按字节显式书写，读端按同样规则拼回，
 * 不依赖宿主机字节序、结构体填充或 double 内存布局）：
 *
 *   偏移  长度  字段            编码
 *   0     4     magic           'R','S','L','E'（小端载荷）/ 'R','S','B','E'（大端载荷）
 *   4     2     version         uint16，当前 = 1
 *   8     4     npolys          uint32，0..32
 *   每个多边形：
 *   +0    4     poly[k].n       uint32，3..64
 *   +4    8*n   poly[k].pts     每点两个 IEEE-754 double：x 后 y，各 8 字节
 *
 * 写出永远使用小端（RSLE）；读入同时接受 RSLE/RSBE，按 magic 指示的
 * 字节序解码。double 以 uint64 逐字节搬运，位模式原样保留。
 *
 * 字段计数：magic、version、npolys 各 1 个标量字段；每个多边形 1 个 n
 * 字段加 2*n 个坐标字段（x/y）。
 */

#define SERIO_VERSION 1

/* 错误码：拒读时 SerioErr.field 给出具体字段名 */
#define SERIO_OK         0
#define SERIO_E_ARG     (-1)  /* 参数非法 */
#define SERIO_E_IO      (-2)  /* 文件打开/读写失败 */
#define SERIO_E_MAGIC   (-3)  /* magic 不对，field="magic" */
#define SERIO_E_VERSION (-4)  /* 版本不对，field="version" */
#define SERIO_E_LENGTH  (-5)  /* 长度不符（计数越界/载荷与声明不一致/多余字节） */
#define SERIO_E_MISSING (-6)  /* 漏字段（声明了但文件提前结束） */

typedef struct {
    int  code;          /* SERIO_OK 或 SERIO_E_* */
    char field[64];     /* 出错字段名，如 "poly[0].pts[2].x" */
    char reason[96];    /* 人类可读原因 */
} SerioErr;

/* 模式开关：1=显式按字节序序列化（默认，新路径）；0=内存直拷（老路径） */
void serio_set_mode(int explicit_bytes);
int  serio_get_mode(void);
/* 从环境变量 RENDER_SERIO 初始化：raw/0 走老直拷路径，其余走新路径 */
void serio_init(void);

/* 场景标量字段数（见文件头注释）；入参非法返回 -1 */
int  serio_field_count(const RScene *s);

int  scene_save(const RScene *s, const char *path);
int  scene_load(const char *path, RScene *out);
int  scene_load_ex(const char *path, RScene *out, SerioErr *err);

const char *serio_strerror(int code);
#endif
