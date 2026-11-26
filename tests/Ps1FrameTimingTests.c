#include "../templates/ps1/starter/runtime/frame_timing.h"
#include "../templates/ps1/starter/runtime/fixedp.h"
#include <limits.h>

/* Compile the shared header as C and compare it with the previous PS1 loop's
 * actual fixed-point division, including startup and stalled frame counts. */
int CheckPS1FrameTiming(void) {
    const int32_t extremes[] = {INT32_MIN, INT32_MAX};
    for (int32_t fields = -10; fields <= 1000; ++fields) {
        int32_t clamped = fields < 1 ? 1 : fields > 4 ? 4 : fields;
        fix16_t previous = fix16_div(FIX16_FROM_INT(clamped), FIX16_FROM_INT(60));
        if (ps1_frame_delta_q16(fields) != previous) return 1;
    }
    for (unsigned i = 0; i < sizeof(extremes) / sizeof(extremes[0]); ++i) {
        int32_t clamped = extremes[i] < 1 ? 1 : 4;
        if (ps1_frame_delta_q16(extremes[i]) !=
            fix16_div(FIX16_FROM_INT(clamped), FIX16_FROM_INT(60))) return 1;
    }
    return 0;
}
