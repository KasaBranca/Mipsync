#ifndef MIPSYNC_CLIP_MATH_H
#define MIPSYNC_CLIP_MATH_H
#include <stdint.h>
#include <limits.h>
/* Exact signed truncation, with native R3000 DIV for ordinary viewport
 * coordinates and the original wide arithmetic for extreme authored scales. */
static inline int64_t ps1_clip_mul_div(int32_t a, int32_t b, int32_t divisor) {
    const int64_t product = (int64_t)a * b;
    if (product >= INT32_MIN && product <= INT32_MAX &&
        !(product == INT32_MIN && divisor == -1))
        return (int32_t)product / divisor;
    return product / divisor;
}
/* Adjacent polygons walk a shared edge in opposite directions. Always round
 * its intersection from the same endpoint; a + trunc(delta*t) otherwise
 * differs by one camera-space unit when the edge is reversed. */
static inline int32_t ps1_clip_lerp(int32_t a, int32_t b,
                                   int32_t numerator, int32_t denominator) {
    if (denominator < 0) {
        const int32_t swap = a; a = b; b = swap;
        numerator -= denominator;
        denominator = -denominator;
    }
    return a + (int32_t)ps1_clip_mul_div(b - a, numerator, denominator);
}
/* Screen-edge t is not the camera-space edge t. Recover the latter before
   interpolating UV or depth, otherwise close walls inherit texels from the
   far endpoint and smear diagonally across the clipped polygon. */
static inline int32_t ps1_clip_screen_attribute(int32_t a, int32_t b,
        int32_t za, int32_t zb, int32_t numerator, int32_t denominator) {
    if (a == b) return a;
    const int64_t weight = (int64_t)za * numerator;
    const int64_t total = (int64_t)zb * ((int64_t)denominator - numerator) + weight;
    if (!total) return a;
    if (weight >= INT32_MIN && weight <= INT32_MAX &&
        total >= INT32_MIN && total <= INT32_MAX)
        return a + (int32_t)ps1_clip_mul_div(b - a, (int32_t)weight, (int32_t)total);
    return a + (int32_t)(((int64_t)b - a) * weight / total);
}

/* All attributes at one intersection share a perspective weight. Use one
 * native unsigned DIV to seed a reciprocal, then correct its product to the
 * exact quotient. This retains signed truncation, including shared-edge UVs,
 * without invoking software 64-bit division for each depth/UV/colour. */
typedef struct {
    int64_t weight, total;
    uint32_t ratio_q16;
    int exact_fast;
} ps1_clip_weight;

static inline ps1_clip_weight ps1_clip_make_weight(
        int32_t za, int32_t zb, int32_t numerator, int32_t denominator) {
    ps1_clip_weight out;
    out.weight = (int64_t)za * numerator;
    out.total = (int64_t)zb * ((int64_t)denominator - numerator) + out.weight;
    out.ratio_q16 = 0;
    out.exact_fast = out.weight >= 0 && out.weight <= out.total &&
        out.total > 0 && out.total < ((int64_t)1 << 47);
    if (out.exact_fast) {
        uint64_t w = (uint64_t)out.weight, t = (uint64_t)out.total;
        while (t > 65535u) { w >>= 1; t >>= 1; }
        out.ratio_q16 = ((uint32_t)w << 16) / (uint32_t)t;
    }
    return out;
}

static inline int32_t ps1_clip_weight_attribute(int32_t a, int32_t b,
                                              const ps1_clip_weight* weight) {
    if (a == b || !weight->total) return a;
    const int64_t delta = (int64_t)b - a;
    if (weight->exact_fast && delta >= -65535 && delta <= 65535) {
        const uint32_t magnitude = delta < 0 ? (uint32_t)-delta : (uint32_t)delta;
        uint32_t q = (magnitude * weight->ratio_q16) >> 16;
        const int64_t numerator = magnitude * weight->weight;
        int64_t product = q * weight->total;
        while (product > numerator) { --q; product -= weight->total; }
        while (numerator - product >= weight->total) { ++q; product += weight->total; }
        return a + (delta < 0 ? -(int32_t)q : (int32_t)q);
    }
    return a + (int32_t)(delta * weight->weight / weight->total);
}
#endif
