#include "budget.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int use_lagrange = 1;
    if (argc > 1 && strcmp(argv[1], "--no-lagrange") == 0) use_lagrange = 0;
    BCurve cs[BUDGET_MAX_MOD];
    double tiers[8];
    int n = budget_std_curves(cs, BUDGET_MAX_MOD);
    int nt = budget_std_tiers(tiers, 8);
    if (n < 0 || nt < 0) return 1;
    if (use_lagrange) {
        BAlloc al[BUDGET_MAX_MOD];
        int rc = budget_lagrange(cs, n, 0.0, al, NULL, NULL, NULL);
        printf("reject budget=0: %s\n", budget_strerror(rc));
        BCurve bad = {"bad", 2, {10.0, 10.0}, {1.0, 2.0}};
        rc = budget_lagrange(&bad, 1, 10.0, al, NULL, NULL, NULL);
        printf("reject convex curve: %s\n", budget_strerror(rc));
    }
    return budget_report(stdout, cs, n, tiers, nt, use_lagrange) == BUDGET_OK ? 0 : 1;
}
