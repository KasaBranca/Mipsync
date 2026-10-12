#ifndef MIPSYNC_WATERTIGHT_PROJECTION_H
#define MIPSYNC_WATERTIGHT_PROJECTION_H
#include <stdint.h>
/* RTPS/RTPT's UNR reciprocal and arithmetic shift, shared by CPU clipping.
 * Exact integer division with truncation is a different projection and opens
 * cracks where clipped triangles meet cached hardware-projected triangles.
 * https://psx-spx.consoledev.net/geometrytransformationenginegte/#gte-division-inaccuracy */
static const uint8_t ps1_projection_unr_table[257] = {
    255, 253, 251, 249, 247, 245, 243, 241, 239, 238, 236, 234, 232, 230, 228, 227,
    225, 223, 221, 220, 218, 216, 214, 213, 211, 209, 208, 206, 205, 203, 201, 200,
    198, 197, 195, 193, 192, 190, 189, 187, 186, 184, 183, 181, 180, 178, 177, 176,
    174, 173, 171, 170, 169, 167, 166, 164, 163, 162, 160, 159, 158, 156, 155, 154,
    153, 151, 150, 149, 148, 146, 145, 144, 143, 141, 140, 139, 138, 137, 135, 134,
    133, 132, 131, 130, 129, 127, 126, 125, 124, 123, 122, 121, 120, 119, 117, 116,
    115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102, 101, 100,
    99, 98, 97, 96, 95, 94, 93, 93, 92, 91, 90, 89, 88, 87, 86, 85,
    84, 83, 83, 82, 81, 80, 79, 78, 77, 77, 76, 75, 74, 73, 72, 72,
    71, 70, 69, 68, 67, 67, 66, 65, 64, 63, 63, 62, 61, 60, 60, 59,
    58, 57, 57, 56, 55, 54, 54, 53, 52, 51, 51, 50, 49, 49, 48, 47,
    46, 46, 45, 44, 44, 43, 42, 42, 41, 40, 40, 39, 38, 38, 37, 36,
    36, 35, 34, 34, 33, 32, 32, 31, 30, 30, 29, 29, 28, 27, 27, 26,
    25, 25, 24, 24, 23, 22, 22, 21, 21, 20, 20, 19, 18, 18, 17, 17,
    16, 15, 15, 14, 14, 13, 13, 12, 12, 11, 10, 10, 9, 9, 8, 8,
    7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0,
    0,
};
static inline uint32_t ps1_projection_factor(int focal, int z) {
    if (z > 0 && z <= 65535 && focal < z * 2) {
        unsigned shift = 0;
        uint32_t d = (uint32_t)z;
        while (d < 0x8000u) { d <<= 1; ++shift; }
        const uint32_t n = (uint32_t)focal << shift;
        const uint32_t u = ps1_projection_unr_table[(d-0x7fc0u)>>7] + 0x101u;
        d = (0x2000080u - d*u) >> 8;
        d = (0x80u + d*u) >> 8;
        uint32_t factor = (uint32_t)(((uint64_t)n*d + 0x8000u) >> 16);
        return factor > 0x1ffffu ? 0x1ffffu : factor;
    }
    /* Near-plane intersections still need an unclamped projection before
     * viewport clipping. Preserve wide coordinates instead of GTE overflow. */
    if (z <= 0) z = 1;
    if ((unsigned)focal <= 65535u && (unsigned)z <= 65535u)
        return ((uint32_t)(unsigned)focal * 65536u + (unsigned)z/2u)/(unsigned)z;
    return (uint32_t)(((uint64_t)(unsigned)focal * 65536u + (unsigned)z/2u)/(unsigned)z);
}
static inline int64_t ps1_projection_axis(int32_t axis, uint32_t factor, int offset) {
    return offset + (((int64_t)axis * factor) >> 16);
}
#endif
