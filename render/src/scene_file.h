#ifndef RSCENE_FILE_H
#define RSCENE_FILE_H
#include "scene.h"

/* 场景文件错误码：六类错误输入各占一个固定码。 */
enum {
    SF_OK            = 0,
    SF_ERR_FILE      = 1,  /* 文件打不开/读取失败           */
    SF_ERR_VERSION   = 2,  /* 缺版本号、版本号不支持、重复   */
    SF_ERR_SYNTAX    = 3,  /* 词法/段结构错误、未知段、缺end */
    SF_ERR_MISSING   = 4,  /* 必填段缺失（canvas/poly）      */
    SF_ERR_VALUE     = 5,  /* 字段值越界或非有限数           */
    SF_ERR_CAPACITY  = 6   /* 超过场景容量（多边形数）       */
};

/* 画布尺寸上限：防止异常场景文件导致超大分配。 */
#define SF_DIM_MAX 4096
#define SF_LINE_MAX 512

typedef struct {
    int      code;          /* SF_*；SF_OK 表示无错 */
    const char *field;      /* 出错字段，如 "canvas.w"、"poly.verts" */
    char     fbuf[32];      /* 动态字段名（如未知段关键字）的持有缓冲 */
    int      line;          /* 场景文件行号；文件级错误为 0 */
    const char *detail;     /* 静态说明文字 */
} SFError;

typedef struct {
    int     version;        /* 目前仅支持 1 */
    int     width;
    int     height;
    double  scale;
    int     has_canvas;
    int     has_version;
    RScene  scene;
} SceneFile;

void scene_file_init(SceneFile *sf);
/* 解析 path；成功返回 SF_OK，失败返回错误码并填充 err。 */
int  scene_file_load(const char *path, SceneFile *sf, SFError *err);
const char *sf_code_name(int code);

/* 旧式无版本场景（仅 legacy 入口使用）：fscanf 风格逐行多边形。 */
int scene_file_load_legacy(const char *path, RScene *out, SFError *err);
#endif
