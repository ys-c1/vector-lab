#ifndef RFPCONF_H
#define RFPCONF_H
#include <stdio.h>

/* 浮点确定性运行配置。
 * fp_deterministic: 总开关。off 时回落当前（基线）行为，
 *                   on 时禁用乘加合并并按固定顺序求值。
 * fma:              乘加（FMA）合并开关，仅在 on 模式有意义，必须为 off。
 * eval_order:       求值顺序，on 模式必须为 fixed，off 模式必须为 default。
 */
typedef struct {
    int fp_deterministic; /* 0=off(基线行为) 1=on(确定性) */
    int fma;              /* 0=off 禁用合并 1=on */
    int eval_order;       /* 0=default 1=fixed */
} RFPConf;

/* 解析 key=value 配置。
 * 成功返回 0；出错返回非 0 并向 err 写入以配置名为中心的报错。
 * 规则：三个键必须全部出现；未知键、重复键、空值、非法值一律拒绝。
 */
int fpconf_parse(const char *path, RFPConf *out, char *err, size_t errsz);

/* 输出配置项快照（稳定文本，供报告与产物旁证）。 */
void fpconf_snapshot(const RFPConf *c, FILE *out);

/* 与快照相同的单行文本，写入 buf（不含换行）。 */
void fpconf_snapshot_str(const RFPConf *c, char *buf, size_t bufsz);
#endif
