#ifndef RMETA_H
#define RMETA_H
#include <stdio.h>

/* 固定元数据：字段名、顺序、类型均不可变。
 * width/height  画幅（正整数）
 * vertices      场景顶点总数（非负整数）
 * sequence      生成序号（正整数，持久化、严格递增、不得回绕）
 * elapsed_ms    本次渲染耗时（非负整数，毫秒）
 */
typedef struct {
    int width;
    int height;
    int vertices;
    long long sequence;
    long long elapsed_ms;
} RMeta;

#define RMETA_E_VALUE (-1)   /* 字段取值非法（类型/范围） */
#define RMETA_E_IO    (-2)   /* 文件读写失败 */
#define RMETA_E_PARSE (-3)   /* 元数据文件解析失败 */
#define RMETA_E_ORDER (-4)   /* 序号缺失/重复/回退/回绕 */

/* 从序号文件读入状态；文件不存在时 last、peak 均置 0 并返回 0。
 * last 为最近一次发放的序号，peak 为历史高水位，二者均持久化。 */
int meta_seq_load(const char *path, long long *last, long long *peak);

/* 校验并分配下一个序号：必须为 last+1，且不得低于/越过历史峰值，不得回绕。
 * 成功后把新状态写回序号文件（原子替换），并在 *next 返回。 */
int meta_seq_next(const char *path, long long last, long long peak, long long *next);

/* 字段完整性 + 类型/范围校验；任一不合法返回 RMETA_E_VALUE。 */
int meta_validate(const RMeta *m);

/* 按固定字段顺序与格式写元数据文件（先写临时文件再原子替换）。
 * 写入前强制再过一次校验，非法直接拒绝、不落盘。 */
int meta_write(const char *path, const RMeta *m);

/* 读回并校验元数据文件：字段缺失/顺序错/类型错/取值非法一律拒绝。 */
int meta_read(const char *path, RMeta *m);

/* 毫秒级单调时钟；两次调用相减得耗时，失败返回 -1。 */
long long meta_now_ms(void);
#endif
