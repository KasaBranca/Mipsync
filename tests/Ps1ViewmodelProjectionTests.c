#include <stdio.h>
#include <stdlib.h>
#include "../templates/ps1/starter/runtime/viewmodel_projection.h"

static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(void) {
    /* A 60-degree camera has H=208 at 320x240. At Z=32, an arm
     * vertex X=16 belongs at X=264, not the GTE-clamped X~=192. */
    check(ps1_viewmodel_project_axis(16, 32, 208, 160, 2) == 264,
          "Close viewmodel must retain its authored screen size");
    check(ps1_viewmodel_project_axis(-16, 32, 208, 160, 2) == 56,
          "Left and right hands must project symmetrically");
    for (int z = 4; z <= 128; z *= 2) {
        int near_offset = ps1_viewmodel_project_axis(8, z, 208, 160, 2) - 160;
        int far_offset = ps1_viewmodel_project_axis(8, z * 2, 208, 160, 2) - 160;
        check(abs(near_offset - 2 * far_offset) <= 1,
              "Moving closer must enlarge the model, without a projection plateau");
    }
    check(ps1_viewmodel_project_axis(1, -10, 208, 160, 2) == 264,
          "Behind-camera vertices must remain safe until polygon clipping");
    check(ps1_viewmodel_project_axis(32767, 2, 208, 160, 2) == 32767 &&
          ps1_viewmodel_project_axis(-32768, 2, 208, 160, 2) == -32768,
          "Extreme projected coordinates must saturate without wrapping");
    check(ps1_viewmodel_project_axis(16, 32, 208, 120, 2) == 224,
          "Vertical projection must use the actual screen center");
    puts("PS1 viewmodel close-range projection passed.");
    return 0;
}
