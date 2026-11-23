/* adoptlocale.c — LD_PRELOAD shim。
 *
 * 按 C 标准，进程启动时处于 "C" 语言环境；只有调用 setlocale(LC_ALL, "")
 * 才会切换到 LANG/LC_* 指定的环境。很多宿主程序（GUI 工具包、脚本运行时、
 * 测试框架）都会在启动阶段这么做，此时它们加载的库就必须自己负责。
 *
 * 本 shim 在 main 之前执行 setlocale(LC_ALL, "")，把“宿主已采用环境 locale”
 * 这一现实条件搬到独立的 CLI / demo 上，从而让语言维度的测试真正有区分力。
 * scanenv: harness — 本文件按设计采用环境 locale，用于验证被测程序。
 */
#define _GNU_SOURCE
#include <locale.h>

__attribute__((constructor)) static void adopt_env_locale(void) {
    setlocale(LC_ALL, "");
}
