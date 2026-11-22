/* 复算驱动的四倍预算判据，证明正常裁剪几何下永远不会误触发。
 * work = 2*(n+1)*sizeof(CPoint)，input = (n+wn)*sizeof(CPoint)。 */
#include <stdio.h>
int main(void) {
    double worst = 0.0;
    for (int wn = 3; wn <= 64; ++wn)
        for (int n = 3; n <= 4096; ++n) {
            double r = (2.0 * (n + 1)) / (n + wn);
            if (r > worst) worst = r;
        }
    printf("work/input 最大比值 %.4f（4.0 才拒绝），%s\n",
           worst, worst < 4.0 ? "预算安全" : "会误触发");
    return worst < 4.0 ? 0 : 1;
}
