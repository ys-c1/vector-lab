#ifndef LAMP_H
#define LAMP_H

/*
 * 沿折线布置路灯。
 *
 * 等距模式（equidistant != 0）：先按折线段累计长度建立弧长索引，
 * 再按目标点距 spacing 反查每个灯位的参数位置，保证相邻灯沿曲线
 * 的弧长间距一致，不会随折线转折忽密忽疏。
 *
 * 逐像素模式（equidistant == 0）：沿用旧做法，沿折线每 0.5 个
 * 单位步进一个灯位，输出与历史逐像素结果完全相同。
 */

typedef struct { double x, y; } LPoint;
typedef struct { int count; LPoint pts[1024]; } LLamps;

#define LAMP_MAX_N 256   /* 支持的折线顶点上限（128 段曲线 = 129 点）*/

/* 错误码（均为负值），字符串可经 lamp_strerror 取出，含出错参数名 */
enum {
    LAMP_OK = 0,
    LAMP_E_ARG = -1,       /* 空指针 / 点数不足等通用参数错误        */
    LAMP_E_SPACING = -2,   /* spacing 为负（两模式）或等距模式为零，
                            * 参数名 spacing                          */
    LAMP_E_ZERO_LEN = -3,  /* 折线总长为零，参数名 pts（全部重合）   */
    LAMP_E_CAPACITY = -4   /* 灯位超过 LLamps 容量（1024）             */
};

/*
 * 布置路灯。
 *   pts/n       折线顶点
 *   spacing     目标点距：等距模式下为相邻灯的弧长间距；
 *               逐像素模式下不参与计算（步进固定 0.5），仍会校验非负
 *   equidistant 非零走弧长反查，零走逐像素步进
 * 返回 LAMP_OK 或上方错误码。
 */
int lamp_place(const LPoint *pts, int n, double spacing,
               int equidistant, LLamps *out);

const char *lamp_strerror(int code);

/* 导出点距表：每盏灯的序号、弧长位置、与上一盏的实际点距。
 * path 为 NULL 或 "-" 时写到 stdout。返回 0 成功，负值为错误码。 */
int lamp_export_csv(const LLamps *lamps, const LPoint *pts, int n,
                    const char *path);

#endif
