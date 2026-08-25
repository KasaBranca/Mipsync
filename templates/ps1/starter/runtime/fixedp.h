#ifndef MIPSYNC_FIXEDP_H
#define MIPSYNC_FIXEDP_H

#include <stdint.h>

/*
 * Q16.16 fixed-point math for the PS1 mini-VM. The PSX has no FPU so we
 * cannot ship Mipsync's IEEE 754 doubles directly; every numeric constant
 * is converted on the host (see src/mips/PS1Export.cpp::ToFixed16) and
 * every arithmetic op below operates on `int32_t` Q16.16 values.
 *
 * Layout:   bit 31      bit 16 | bit 15      bit 0
 *           ┌─────────────────┬────────────────────┐
 *           │  signed integer │  fractional        │
 *           └─────────────────┴────────────────────┘
 *
 * Range: ±32767.99998, resolution ≈ 1.5e-5 (good enough for transforms,
 * physics deltas, and audio params on a 30-fps PS1 game; specifically NOT
 * good enough for anything timescale-sensitive across long sessions).
 */

typedef int32_t fix16_t;

#define FIX16_ONE        ((fix16_t)0x00010000)
#define FIX16_FROM_INT(i) ((fix16_t)((int32_t)(i) << 16))
#define FIX16_TO_INT(f)   ((int32_t)((f) >> 16))

fix16_t fix16_add(fix16_t a, fix16_t b);
fix16_t fix16_sub(fix16_t a, fix16_t b);
fix16_t fix16_mul(fix16_t a, fix16_t b);
fix16_t fix16_div(fix16_t a, fix16_t b);
fix16_t fix16_neg(fix16_t a);

/* IEEE 754 → Q16.16 (kept here for completeness; not used at runtime since
 * the host already converted constants ahead of time). */
fix16_t fix16_from_float_bits(uint32_t bits);

/* Lookup-table sin/cos. Input is "turns × 2π" represented in Q16.16, so
 * sin(0)=0, sin(0.25)=1, sin(0.5)=0, sin(0.75)=-1. */
fix16_t fix16_sin(fix16_t turns);
fix16_t fix16_cos(fix16_t turns);

/* Mips# Mathf uses radians, matching the desktop VM and C/C++ math APIs. */
fix16_t fix16_sin_radians(fix16_t radians);
fix16_t fix16_cos_radians(fix16_t radians);

fix16_t fix16_sqrt(fix16_t a);
fix16_t fix16_abs(fix16_t a);
fix16_t fix16_clamp(fix16_t v, fix16_t lo, fix16_t hi);
/* Nearest integer, with exact halves rounded away from zero (std::round). */
fix16_t fix16_round(fix16_t v);

/* Pretty-print to a NUL-terminated buffer (sign + at most 5 frac digits).
 * `out` should be at least 16 bytes. Returns chars written (excluding NUL). */
int fix16_format(fix16_t v, char* out, int outCap);

#endif /* MIPSYNC_FIXEDP_H */
