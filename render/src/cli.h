#ifndef RCLI_H
#define RCLI_H
#include "scene_file.h"

/* 子命令行（非场景文件）退出码。场景文件错误沿用 SF_* 1..6。 */
enum {
    EXIT_OK             = 0,
    EXIT_SCENE_BASE     = 1,   /* SF 错误码 1..6 直接作为退出码 */
    EXIT_USAGE          = 7,   /* E_USAGE：参数结构错误 */
    EXIT_BAD_FIELD      = 8,   /* E_FIELD：选项值非法/越界 */
    EXIT_OUTPUT         = 9    /* E_OUTPUT：导出文件写失败 */
};

/* 一次子命令解析的结果。 */
typedef struct {
    const char *scene_path;   /* 位置参数：场景文件 */
    const char *out_path;     /* --out */
    int         ss;           /* --ss，默认 4 */
    int         force;        /* --force */
    int         verbose;      /* --verbose */
} CliArgs;

/* 渲染共享：载入已校验场景并分配缓冲渲染。失败时 *buf 保持 NULL。 */
int cli_render_buffer(const SceneFile *sf, int ss, unsigned char **buf);
/* 统一错误打印：error <code> field=<f> line=<n> blocked=yes: detail */
void cli_report_error(int code, const char *field, int line, const char *detail);
#endif
