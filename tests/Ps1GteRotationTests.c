/* The closed-form RotMatrix must stay bit-identical to the generic
 * Ry * Rx * Rz product it replaced (two mipsync_gte_mul_matrix calls). */
#include "../templates/ps1/starter/runtime/mipsync_gte.c"
#include <stdio.h>
#include <string.h>

static void reference_rot_matrix(const SVECTOR* rotation, MATRIX* out) {
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
    mipsync_gte_mul_matrix(&y, &x, &yx);
    mipsync_gte_mul_matrix(&yx, &z, out);
    out->t[0] = out->t[1] = out->t[2] = 0;
}

static int same(const MATRIX* a, const MATRIX* b) {
    return memcmp(a->m, b->m, sizeof(a->m)) == 0 &&
        a->t[0] == b->t[0] && a->t[1] == b->t[1] && a->t[2] == b->t[2];
}

int main(void) {
    /* Full turn on every axis (16-unit steps), plus wrapped/negative angles. */
    const int extra[] = { -4096, -2048, -1, 0, 1, 1023, 1024, 1025, 2047, 2048,
                          3071, 3072, 4095, 4096, 8191, 32767, -32768 };
    unsigned long checked = 0;
    for (int ax = -4096; ax <= 4096; ax += 16)
        for (int ay = -4096; ay <= 4096; ay += 16)
            for (int az = -4096; az <= 4096; az += 64) {
                const SVECTOR r = { (int16_t)ax, (int16_t)ay, (int16_t)az, 0 };
                MATRIX expected, actual;
                memset(&expected, 0x5a, sizeof(expected));
                memset(&actual, 0xa5, sizeof(actual));
                reference_rot_matrix(&r, &expected);
                mipsync_gte_rot_matrix(&r, &actual);
                if (!same(&expected, &actual)) {
                    printf("RotMatrix mismatch at %d %d %d\n", ax, ay, az);
                    return 1;
                }
                ++checked;
            }
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); ++i)
        for (unsigned j = 0; j < sizeof(extra) / sizeof(extra[0]); ++j)
            for (unsigned k = 0; k < sizeof(extra) / sizeof(extra[0]); ++k) {
                const SVECTOR r = { (int16_t)extra[i], (int16_t)extra[j], (int16_t)extra[k], 0 };
                MATRIX expected, actual;
                reference_rot_matrix(&r, &expected);
                mipsync_gte_rot_matrix(&r, &actual);
                if (!same(&expected, &actual)) {
                    printf("RotMatrix mismatch at %d %d %d\n", extra[i], extra[j], extra[k]);
                    return 1;
                }
                ++checked;
            }
    printf("RotMatrix closed form matches the generic product (%lu rotations)\n", checked);
    return 0;
}
