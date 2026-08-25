#include "fixedp.h"

#include <stdint.h>

fix16_t fix16_add(fix16_t a, fix16_t b) { return (fix16_t)(a + b); }
fix16_t fix16_sub(fix16_t a, fix16_t b) { return (fix16_t)(a - b); }
fix16_t fix16_neg(fix16_t a)            { return (fix16_t)(-a);   }
fix16_t fix16_abs(fix16_t a)            { return a < 0 ? -a : a;  }

fix16_t fix16_mul(fix16_t a, fix16_t b) {
    /* Use 64-bit intermediate so the implicit << 16 doesn't lose bits. */
    int64_t product = (int64_t)a * (int64_t)b;
    return (fix16_t)(product >> 16);
}

fix16_t fix16_div(fix16_t a, fix16_t b) {
    if (b == 0) return a < 0 ? (fix16_t)0x80000000 : (fix16_t)0x7FFFFFFF;
    int64_t scaled = ((int64_t)a) << 16;
    return (fix16_t)(scaled / (int64_t)b);
}

fix16_t fix16_from_float_bits(uint32_t bits) {
    /* IEEE 754 single → Q16.16. Used only as a fallback for ad-hoc consts
     * built on-device; the host converter handles project-level constants. */
    int32_t sign     = (bits >> 31) & 0x1;
    int32_t exponent = (int32_t)((bits >> 23) & 0xFF) - 127;
    uint32_t mantissa = (bits & 0x7FFFFF) | (bits ? 0x800000 : 0u);
    int32_t shift = 23 - 16 - exponent;
    int64_t mag;
    if (shift >= 0) mag = (int64_t)mantissa >> shift;
    else            mag = (int64_t)mantissa << -shift;
    if (mag > 0x7FFFFFFF) mag = 0x7FFFFFFF;
    return (fix16_t)(sign ? -mag : mag);
}

/* 64-entry quarter-cycle sin table covering [0, π/2]. Values are Q16.16.
 * Generated offline from sin(i / 256 * 2π) for i = 0..63. Symmetry handles
 * the remaining three quadrants. */
static const fix16_t k_sin_quarter[65] = {
    0x00000000, 0x00000648, 0x00000C90, 0x000012D5, 0x00001918, 0x00001F56, 0x00002590, 0x00002BC4,
    0x000031F1, 0x00003817, 0x00003E34, 0x00004447, 0x00004A50, 0x0000504D, 0x0000563E, 0x00005C22,
    0x000061F8, 0x000067BE, 0x00006D74, 0x0000731A, 0x000078AD, 0x00007E2F, 0x0000839C, 0x000088F6,
    0x00008E3A, 0x00009368, 0x00009880, 0x00009D80, 0x0000A268, 0x0000A736, 0x0000ABEB, 0x0000B086,
    0x0000B505, 0x0000B968, 0x0000BDAF, 0x0000C1D8, 0x0000C5E4, 0x0000C9D1, 0x0000CD9F, 0x0000D14D,
    0x0000D4DB, 0x0000D848, 0x0000DB94, 0x0000DEBE, 0x0000E1C6, 0x0000E4AA, 0x0000E76C, 0x0000EA0A,
    0x0000EC83, 0x0000EED9, 0x0000F109, 0x0000F314, 0x0000F4FA, 0x0000F6BA, 0x0000F854, 0x0000F9C8,
    0x0000FB15, 0x0000FC3B, 0x0000FD3B, 0x0000FE13, 0x0000FEC4, 0x0000FF4E, 0x0000FFB1, 0x0000FFEC,
    0x00010000
};

static fix16_t sin_slot(int32_t idx256) {
    int32_t quadrant;
    int32_t local;

    idx256 &= 255;
    quadrant = idx256 / 64;
    local = idx256 % 64;

    switch (quadrant) {
        case 0: return k_sin_quarter[local];
        case 1: return k_sin_quarter[64 - local];
        case 2: return -k_sin_quarter[local];
        default: return -k_sin_quarter[64 - local];
    }
}

/* `turns` is in units where 1.0 == one full rotation. Reduce to [0,1) and
 * fold by quadrant. Interpolate between 256 angular slots so shallow camera
 * pitches do not snap to 1.40625-degree steps. */
static fix16_t sin_internal(fix16_t turns) {
    uint32_t frac = (uint32_t)turns & 0xFFFFu;
    int32_t idx256 = (int32_t)(frac >> 8);
    int32_t lerp = (int32_t)(frac & 0xFFu);
    fix16_t a = sin_slot(idx256);
    fix16_t b = sin_slot(idx256 + 1);
    return (fix16_t)((int32_t)a + (int32_t)(((int64_t)((int32_t)b - (int32_t)a) * lerp) >> 8));
}

fix16_t fix16_sin(fix16_t turns) { return sin_internal(turns); }
fix16_t fix16_cos(fix16_t turns) {
    /* cos(x) = sin(x + 0.25). Adding FIX16_ONE/4 = 0x4000. */
    return sin_internal((fix16_t)(turns + 0x4000));
}

/* round(2 * pi * 65536). Kept as an integer constant so no floating-point
 * support is pulled into the PS1 runtime. */
#define FIX16_TWO_PI ((fix16_t)411775)

fix16_t fix16_sin_radians(fix16_t radians) {
    return fix16_sin(fix16_div(radians, FIX16_TWO_PI));
}

fix16_t fix16_cos_radians(fix16_t radians) {
    return fix16_cos(fix16_div(radians, FIX16_TWO_PI));
}

fix16_t fix16_sqrt(fix16_t a) {
    if (a <= 0) return 0;
    /* Newton-Raphson, 6 iters is enough for Q16.16 precision. Start guess
     * scales with magnitude so we converge fast for large inputs too. */
    int32_t guess = a > FIX16_ONE ? a >> 1 : FIX16_ONE;
    for (int i = 0; i < 6; ++i) {
        if (guess <= 0) { guess = FIX16_ONE; break; }
        int32_t quot = (int32_t)(((int64_t)a << 16) / guess);
        guess = (guess + quot) >> 1;
    }
    return guess;
}

fix16_t fix16_clamp(fix16_t v, fix16_t lo, fix16_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

fix16_t fix16_round(fix16_t v) {
    const int64_t magnitude = v < 0 ? -(int64_t)v : (int64_t)v;
    const int64_t rounded = (magnitude + FIX16_ONE / 2) & ~0xFFFFLL;
    return (fix16_t)(v < 0 ? -rounded : rounded);
}

static int fmt_uint(uint32_t v, char* out, int cap) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    int written = 0;
    for (int i = n - 1; i >= 0 && written < cap; --i) out[written++] = tmp[i];
    return written;
}

int fix16_format(fix16_t v, char* out, int outCap) {
    if (outCap < 2) { if (outCap) out[0] = 0; return 0; }
    int n = 0;
    int negative = v < 0;
    uint32_t mag = negative ? (uint32_t)(-v) : (uint32_t)v;
    if (negative) out[n++] = '-';

    uint32_t whole = mag >> 16;
    uint32_t frac  = mag & 0xFFFF;
    n += fmt_uint(whole, out + n, outCap - n - 1);
    if (frac != 0 && n + 2 < outCap) {
        out[n++] = '.';
        /* Up to 5 frac digits. */
        for (int i = 0; i < 5 && n + 1 < outCap; ++i) {
            frac *= 10;
            uint32_t d = frac >> 16;
            out[n++] = (char)('0' + (d % 10));
            frac &= 0xFFFF;
            if (frac == 0) break;
        }
    }
    out[n] = 0;
    return n;
}
