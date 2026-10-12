#include "lighting.h"

static uint32_t light_sqrt(uint32_t value) {
    uint32_t bit = (uint32_t)1 << 30;
    uint32_t result = 0;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else result >>= 1;
        bit >>= 2;
    }
    return (uint32_t)result;
}

static int sample_light(const ps1_lighting_light* light, const int32_t position[3],
    int32_t direction[3], int32_t* atten) {
    int32_t attenuation = 4096;
    int axis;
        if (light->type == 0) {
            for (axis = 0; axis < 3; ++axis) direction[axis] = light->direction[axis];
        } else {
            int64_t wide_delta[3];
            int32_t delta[3], range;
            uint64_t largest = 0;
            uint32_t length_squared = 0;
            uint32_t distance;
            int shift = 0, outside = 0;
            for (axis = 0; axis < 3; ++axis) {
                uint64_t magnitude;
                wide_delta[axis] = (int64_t)light->position[axis] - position[axis];
                magnitude = wide_delta[axis] < 0 ? -wide_delta[axis] : wide_delta[axis];
                if (magnitude > largest) largest = magnitude;
                if (light->range > 0 && magnitude >= (uint32_t)light->range) outside = 1;
            }
            if (outside) return 0;
            /* Keep the hot path in 32-bit arithmetic on the R3000A. Scaling
             * very distant lights preserves direction and distance/range. */
            while (largest > 8191) { largest >>= 1; ++shift; }
            range = light->range >> shift;
            for (axis = 0; axis < 3; ++axis) {
                delta[axis] = (int32_t)(wide_delta[axis] >> shift);
                length_squared += (uint32_t)(delta[axis] * delta[axis]);
            }
            if (range > 0 && range <= 16384 && length_squared >= (uint32_t)range * range) return 0;
            distance = light_sqrt(length_squared);
            if (distance) {
                const int32_t reciprocal = (1 << 24) / distance;
                for (axis = 0; axis < 3; ++axis)
                    direction[axis] = (delta[axis] * reciprocal) >> 12;
            } else {
                direction[0] = 0; direction[1] = 4096; direction[2] = 0;
            }
            if (range > 0) {
                int32_t t = 4096 - (int32_t)(distance * 4096 / range);
                attenuation = (t * t) >> 12;
            }
            if (light->type == 2) {
                int32_t cosine = 0;
                int32_t t, cone;
                for (axis = 0; axis < 3; ++axis)
                    cosine += light->direction[axis] * direction[axis];
                cosine >>= 12; /* both directions point toward the source */
                if (cosine <= light->cos_outer) return 0;
                if (cosine >= light->cos_inner) cone = 4096;
                else {
                    t = ((cosine - light->cos_outer) << 12) /
                        (light->cos_inner - light->cos_outer);
                    cone = (((t * t) >> 12) * (12288 - 2 * t)) >> 12;
                }
                attenuation = (attenuation * cone) >> 12;
            }
        }
    *atten = attenuation;
    return 1;
}

unsigned int ps1_lighting_prepare(const ps1_lighting_light* lights, unsigned int count,
    const int32_t position[3], ps1_lighting_light* prepared) {
    unsigned int i, used = 0;
    if (count > PS1_LIGHT_LIMIT) count = PS1_LIGHT_LIMIT;
    for (i = 0; i < count; ++i) {
        int32_t direction[3], attenuation;
        int axis;
        ps1_lighting_light output = { 0 };
        if (!sample_light(&lights[i], position, direction, &attenuation)) continue;
        for (axis = 0; axis < 3; ++axis) {
            output.direction[axis] = (int16_t)direction[axis];
            output.color_q12[axis] = (lights[i].color_q12[axis] * attenuation) >> 12;
        }
        prepared[used++] = output;
    }
    return used;
}

void ps1_lighting_evaluate(const ps1_lighting_light* lights, unsigned int count,
    const int32_t position[3], const int16_t normal[3],
    const uint8_t base[3], uint8_t result[3]) {
    int32_t sum[3] = { 74, 74, 74 }; /* editor .15 * .12 ambient */
    unsigned int i;
    if (count > PS1_LIGHT_LIMIT) count = PS1_LIGHT_LIMIT;
    for (i = 0; i < count; ++i) {
        const ps1_lighting_light* light = &lights[i];
        int32_t direction[3];
        int32_t attenuation = 4096;
        int32_t diffuse;
        int axis;
        if (!sample_light(light, position, direction, &attenuation)) continue;
        diffuse = (normal[0] * direction[0] + normal[1] * direction[1] +
            normal[2] * direction[2]) >> 12;
        if (diffuse <= 0) continue;
        if (diffuse > 4096) diffuse = 4096;
        diffuse = (diffuse * attenuation) >> 12;
        for (axis = 0; axis < 3; ++axis)
            sum[axis] += (light->color_q12[axis] * diffuse) >> 12;
    }
    for (i = 0; i < 3; ++i) {
        int32_t value = (sum[i] * base[i] + 2048) >> 12;
        result[i] = (uint8_t)(value > 255 ? 255 : value);
    }
}
