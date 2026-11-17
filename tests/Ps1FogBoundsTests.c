#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "../templates/ps1/starter/runtime/fog_bounds.h"

static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}

int main(void) {
    check(ps1_bound_before_fog(100, 20, 121), "A fully clear sphere needs no fog");
    check(ps1_bound_before_fog(100, 20, 120), "Touching the start plane has zero fog");
    check(!ps1_bound_before_fog(100, 20, 119), "A partially fogged sphere keeps vertex fog");
    check(!ps1_bound_before_fog(200, 20, 120), "A sphere beyond the start plane keeps fog");
    check(ps1_bound_before_fog(-100, 20, -80), "Signed camera depths retain the boundary");
    check(!ps1_bound_before_fog(0, -1, 100), "An invalid radius cannot certify clear geometry");
    check(!ps1_bound_before_fog(INT_MAX, 1, INT_MAX), "Positive overflow must not disable fog");
    check(ps1_bound_before_fog(INT_MIN, INT_MAX, -1), "A wide signed sum stays exact");
    check(ps1_bound_before_fog(INT_MAX, 0, INT_MAX), "A zero-radius bound retains its plane");
    puts("PS1 fog-bound tests passed");
    return 0;
}
