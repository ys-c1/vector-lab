/* 把 HEAD 版本 svgpath.c 的符号改名后编进来，供 compat.c 与新实现对比。
 * 不修改 legacy 源码本身，只在本翻译单元里做宏替换。 */
#define svg_parse legacy_parse
#define svg_fill  legacy_fill
#define svg_save_pgm legacy_save_pgm
#include "legacy_svgpath.c"
