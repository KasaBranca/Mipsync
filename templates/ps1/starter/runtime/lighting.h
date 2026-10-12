#ifndef MIPSYNC_LIGHTING_H
#define MIPSYNC_LIGHTING_H
#include <stdint.h>

#define PS1_LIGHT_LIMIT 8
/* Positions/range use the same integer world units as the render matrices.
 * Directions, cosines and linear radiance use Q12 (4096 = 1). */
typedef struct ps1_lighting_light {
    int32_t position[3];
    int32_t color_q12[3];
    int32_t range;
    int16_t direction[3];
    int16_t cos_outer, cos_inner;
    uint8_t type;
} ps1_lighting_light;

void ps1_lighting_evaluate(const ps1_lighting_light* lights, unsigned int count,
    const int32_t position[3], const int16_t normal[3],
    const uint8_t base[3], uint8_t result[3]);
/* Sample a small animated part once, then reuse directional contributions for
 * its normals. Static geometry continues to sample every vertex/face. */
unsigned int ps1_lighting_prepare(const ps1_lighting_light* lights, unsigned int count,
    const int32_t position[3], ps1_lighting_light* prepared);
#endif
