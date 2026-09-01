#include "mipsync_gte.h"

#include <limits.h>

static int16_t clamp_i16(int64_t value) {
    if (value < INT16_MIN) return INT16_MIN;
    if (value > INT16_MAX) return INT16_MAX;
    return (int16_t)value;
}

static int32_t round_q24_to_q12_i32(int32_t value) {
    if (value >= 0)
        return (int32_t)((value + (ONE / 2)) >> 12);
    return -(int32_t)(((-value) + (ONE / 2)) >> 12);
}

static uint32_t isqrt_u64(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = (uint64_t)1 << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

/*
 * 0x1000 is one complete turn. The first quadrant uses Bhaskara I's sine
 * approximation in integer form. This avoids importing any third-party lookup
 * table while retaining sufficient precision for Mipsync's PS1 transforms.
 */
static int32_t sin_turn_q12(int32_t angle) {
    int32_t phase = angle & 0xfff;
    int sign = 1;
    int32_t x;
    int64_t product;
    int64_t numerator;
    int64_t denominator;
    if (phase >= 2048) {
        phase -= 2048;
        sign = -1;
    }
    x = phase <= 1024 ? phase : 2048 - phase;
    product = (int64_t)x * (2048 - x);
    numerator = 16 * product * ONE;
    denominator = 5LL * 2048 * 2048 - 4 * product;
    if (denominator == 0) return 0;
    return sign * (int32_t)((numerator + denominator / 2) / denominator);
}

static int32_t cos_turn_q12(int32_t angle) {
    return sin_turn_q12(angle + 1024);
}

void mipsync_gte_init(void) {
    /* Enable COP2 in COP0.Status, then establish Mipsync's depth averaging. */
    __asm__ volatile(
        "mfc0 $8,$12\n"
        "lui $9,0x4000\n"
        "or $8,$8,$9\n"
        "mtc0 $8,$12\n"
        "nop\nnop\n"
        : : : "$8", "$9");
    {
        const int32_t zsf3 = 341; /* (z0+z1+z2) / 12 */
        const int32_t zsf4 = 256; /* (z0+z1+z2+z3) / 16 */
        __asm__ volatile("ctc2 %0,$29\nctc2 %1,$30\n"
                         : : "r"(zsf3), "r"(zsf4));
    }
}

MATRIX* mipsync_gte_mul_matrix(const MATRIX* left, const MATRIX* right,
                               MATRIX* out) {
    MATRIX result;
    int row, column;
    for (row = 0; row < 3; ++row) {
        for (column = 0; column < 3; ++column) {
            /* These matrices contain Q12 rotations and one already-scaled
             * object matrix. Their three-term dot product fits int32 in the
             * runtime's supported transform range. Avoiding int64 here is
             * significant on the 32-bit R3000A and removes wide-arithmetic
             * work from every rigid character part. */
            const int32_t sum =
                (int32_t)left->m[row][0] * (int32_t)right->m[0][column] +
                (int32_t)left->m[row][1] * (int32_t)right->m[1][column] +
                (int32_t)left->m[row][2] * (int32_t)right->m[2][column];
            result.m[row][column] = clamp_i16(round_q24_to_q12_i32(sum));
        }
        result.t[row] = 0;
    }
    *out = result;
    return out;
}

MATRIX* mipsync_gte_rot_matrix(const SVECTOR* rotation, MATRIX* out) {
    MATRIX x = {{{ONE,0,0},{0,0,0},{0,0,0}}, {0,0,0}};
    MATRIX y = {{{0,0,0},{0,ONE,0},{0,0,0}}, {0,0,0}};
    MATRIX z = {{{0,0,0},{0,0,0},{0,0,ONE}}, {0,0,0}};
    MATRIX yx;
    const int32_t sx = sin_turn_q12(rotation->vx);
    const int32_t cx = cos_turn_q12(rotation->vx);
    const int32_t sy = sin_turn_q12(rotation->vy);
    const int32_t cy = cos_turn_q12(rotation->vy);
    const int32_t sz = sin_turn_q12(rotation->vz);
    const int32_t cz = cos_turn_q12(rotation->vz);
    x.m[1][1] = (int16_t)cx; x.m[1][2] = (int16_t)-sx;
    x.m[2][1] = (int16_t)sx; x.m[2][2] = (int16_t)cx;
    y.m[0][0] = (int16_t)cy; y.m[0][2] = (int16_t)sy;
    y.m[2][0] = (int16_t)-sy; y.m[2][2] = (int16_t)cy;
    z.m[0][0] = (int16_t)cz; z.m[0][1] = (int16_t)-sz;
    z.m[1][0] = (int16_t)sz; z.m[1][1] = (int16_t)cz;
    /* Match Mipsync's editor transform convention: Ry * Rx * Rz. */
    mipsync_gte_mul_matrix(&y, &x, &yx);
    mipsync_gte_mul_matrix(&yx, &z, out);
    out->t[0] = out->t[1] = out->t[2] = 0;
    return out;
}

MATRIX* mipsync_gte_trans_matrix(MATRIX* matrix, const VECTOR* translation) {
    matrix->t[0] = translation->vx;
    matrix->t[1] = translation->vy;
    matrix->t[2] = translation->vz;
    return matrix;
}

MATRIX* mipsync_gte_scale_matrix(MATRIX* matrix, const VECTOR* scale) {
    int row;
    for (row = 0; row < 3; ++row) {
        matrix->m[row][0] = clamp_i16(((int64_t)matrix->m[row][0] * scale->vx) >> 12);
        matrix->m[row][1] = clamp_i16(((int64_t)matrix->m[row][1] * scale->vy) >> 12);
        matrix->m[row][2] = clamp_i16(((int64_t)matrix->m[row][2] * scale->vz) >> 12);
    }
    return matrix;
}

VECTOR* mipsync_gte_apply_matrix(const MATRIX* matrix, const VECTOR* input,
                                 VECTOR* out) {
    const int64_t x = (int64_t)matrix->m[0][0] * input->vx +
                      (int64_t)matrix->m[0][1] * input->vy +
                      (int64_t)matrix->m[0][2] * input->vz;
    const int64_t y = (int64_t)matrix->m[1][0] * input->vx +
                      (int64_t)matrix->m[1][1] * input->vy +
                      (int64_t)matrix->m[1][2] * input->vz;
    const int64_t z = (int64_t)matrix->m[2][0] * input->vx +
                      (int64_t)matrix->m[2][1] * input->vy +
                      (int64_t)matrix->m[2][2] * input->vz;
    out->vx = (int32_t)(x >> 12);
    out->vy = (int32_t)(y >> 12);
    out->vz = (int32_t)(z >> 12);
    return out;
}

MATRIX* mipsync_gte_comp_matrix(const MATRIX* parent, const MATRIX* local,
                                MATRIX* out) {
    MATRIX rotation;
    VECTOR local_translation = {local->t[0], local->t[1], local->t[2]};
    VECTOR translated;
    mipsync_gte_mul_matrix(parent, local, &rotation);
    mipsync_gte_apply_matrix(parent, &local_translation, &translated);
    rotation.t[0] = translated.vx + parent->t[0];
    rotation.t[1] = translated.vy + parent->t[1];
    rotation.t[2] = translated.vz + parent->t[2];
    *out = rotation;
    return out;
}

void mipsync_gte_normalize(const VECTOR* input, SVECTOR* out) {
    const int64_t x = input->vx;
    const int64_t y = input->vy;
    const int64_t z = input->vz;
    const uint32_t length = isqrt_u64((uint64_t)(x*x + y*y + z*z));
    if (length == 0) {
        out->vx = out->vy = out->vz = out->pad = 0;
        return;
    }
    out->vx = clamp_i16((x * ONE) / length);
    out->vy = clamp_i16((y * ONE) / length);
    out->vz = clamp_i16((z * ONE) / length);
    out->pad = 0;
}
