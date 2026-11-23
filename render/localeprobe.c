/* 打印当前进程的小数点，用来证明 adoptlocale.so 真的让 guest 采用了环境 locale。
 * scanenv: harness — 本文件按设计采用环境 locale，用于验证被测程序。
 * 自身不调用 setlocale(LC_ALL,"")：只有 shim 生效时输出才会是 ','。 */
#include <locale.h>
#include <stdio.h>

int main(void) {
    struct lconv *lc = localeconv();
    printf("decimal_point=%s\n", lc->decimal_point ? lc->decimal_point : "?");
    return 0;
}
