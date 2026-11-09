#ifndef MIPSYNC_PROJECTION_SAFETY_H
#define MIPSYNC_PROJECTION_SAFETY_H
#include <stdint.h>
/* Native rasterization clips against the drawing area. Reserve coordinate
   and extent headroom for seam expansion; larger faces still need CPU clips. */
static inline int ps1_polygon_fits_gpu(const int16_t* x, const int16_t* y,
                                     unsigned count) {
    if (!count) return 0;
    int min_x = x[0], max_x = x[0], min_y = y[0], max_y = y[0];
    for (unsigned i = 0; i < count; ++i) {
        if (x[i] < -1020 || x[i] > 1019 || y[i] < -1020 || y[i] > 1019)
            return 0;
        if (x[i] < min_x) min_x = x[i];
        if (x[i] > max_x) max_x = x[i];
        if (y[i] < min_y) min_y = y[i];
        if (y[i] > max_y) max_y = y[i];
    }
    return max_x - min_x <= 1020 && max_y - min_y <= 508;
}
/* RTPS clamps H/Z to two and screen coordinates to signed 11 bits.
   A clamped point cannot be used for winding, visibility, or GPU packets. */
static inline int ps1_projection_vertex_needs_clip(int32_t xy, int32_t z,
                                                   int near_z, int far_z, int focal) {
    const int x = (int16_t)xy;
    const int y = (int16_t)((uint32_t)xy >> 16);
    return z < near_z || z > far_z || z * (int64_t)2 <= focal ||
           x <= -1024 || x >= 1023 || y <= -1024 || y >= 1023;
}
/* Sort local geometry fragments within the visible depth range; a face
   crossing either depth plane must still draw its remaining visible part. */
static inline int ps1_clipped_face_sort_depth(int32_t z0, int32_t z1, int32_t z2,
                                             int near_z, int far_z) {
    int depth = (int)(((int64_t)z0 + z1 + z2) / 3);
    if (depth < near_z) depth = near_z;
    if (depth > far_z) depth = far_z;
    return depth > 3 ? depth / 4 : 1;
}
#endif
