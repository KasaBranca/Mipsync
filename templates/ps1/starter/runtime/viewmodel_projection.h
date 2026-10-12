#ifndef MIPSYNC_VIEWMODEL_PROJECTION_H
#define MIPSYNC_VIEWMODEL_PROJECTION_H

#include <stdint.h>
#include "clip_math.h"

/* RTPS/RTPT saturate H/Z at just under 2. Viewmodels deliberately live
 * closer than H/2, so their projection must preserve the full ratio.
 * Keep signed depth separately for the near-plane polygon clipper. */
static inline int16_t ps1_viewmodel_project_axis(int32_t position, int32_t depth,
                                                int focal_length, int center,
                                                int near_depth) {
    int64_t screen;
    if (near_depth < 1)
        near_depth = 1;
    if (depth < near_depth)
        depth = near_depth;
    screen = center + ps1_clip_mul_div(focal_length, position, depth);
    if (screen < -32768) screen = -32768;
    if (screen > 32767) screen = 32767;
    return (int16_t)screen;
}

#endif
