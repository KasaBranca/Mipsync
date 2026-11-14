#ifndef MIPSYNC_FOG_BOUNDS_H
#define MIPSYNC_FOG_BOUNDS_H
#include <stdint.h>

/* Touching the start plane still has zero fog. Widen the sum so distant
 * or oversized bounds cannot wrap into the clear range. */
static inline int ps1_bound_before_fog(int32_t center_z, int32_t radius,
                                      int32_t fog_start_z) {
    return radius >= 0 && (int64_t)center_z + radius <= fog_start_z;
}
#endif
