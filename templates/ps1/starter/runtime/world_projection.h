#ifndef MIPSYNC_WORLD_PROJECTION_H
#define MIPSYNC_WORLD_PROJECTION_H
#include <stdint.h>
#include <limits.h>
#include "watertight_projection.h"

/* Read-only projection for world-anchored overlays. Wide intermediates keep
 * extreme authored coordinates defined without changing the GTE registers. */
static inline int ps1_project_world_point(
        const int16_t rotation[3][3], const int32_t translation[3],
        int screen_w, int screen_h, int geom_screen, int near_z, int far_z,
        int32_t x_q16, int32_t y_q16, int32_t z_q16,
        int16_t* screen_x, int16_t* screen_y, int32_t* depth) {
    const int32_t world[3] = {x_q16 >> 10, y_q16 >> 10, z_q16 >> 10};
    int32_t camera[3];
    int row;
    int64_t x, y;
    if (!rotation || !translation || !screen_x || !screen_y || screen_w <= 0 || screen_h <= 0 ||
        geom_screen <= 0 || near_z <= 0 || far_z < near_z)
        return 0;
    for (row = 0; row < 3; ++row) {
        const int64_t value = (((int64_t)rotation[row][0] * world[0] +
            (int64_t)rotation[row][1] * world[1] +
            (int64_t)rotation[row][2] * world[2]) >> 12) + translation[row];
        if (value < INT32_MIN || value > INT32_MAX)
            return 0;
        camera[row] = (int32_t)value;
    }
    if (camera[2] < near_z || camera[2] > far_z)
        return 0;
    const uint32_t factor = ps1_projection_factor(geom_screen, camera[2]);
    x = ps1_projection_axis(camera[0], factor, screen_w / 2);
    y = ps1_projection_axis(camera[1], factor, screen_h / 2);
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX)
        return 0;
    *screen_x = (int16_t)x;
    *screen_y = (int16_t)y;
    if (depth) *depth = camera[2];
    return 1;
}
#endif
