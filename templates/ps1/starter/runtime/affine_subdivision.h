#ifndef MIPSYNC_AFFINE_SUBDIVISION_H
#define MIPSYNC_AFFINE_SUBDIVISION_H
#include "clip_math.h"
#define PS1_AFFINE_FRAGMENTS 32
typedef struct {
    int32_t x, y, z;
    int u, v, r, g, b;
} ps1_screen_clip_vert;

static inline ps1_screen_clip_vert ps1_affine_midpoint(
        const ps1_screen_clip_vert* a, const ps1_screen_clip_vert* b) {
    ps1_screen_clip_vert out;
    out.x = (a->x + b->x) / 2;
    out.y = (a->y + b->y) / 2;
    out.z = ps1_clip_screen_attribute(a->z, b->z, a->z, b->z, 1, 2);
    out.u = ps1_clip_screen_attribute(a->u, b->u, a->z, b->z, 1, 2);
    out.v = ps1_clip_screen_attribute(a->v, b->v, a->z, b->z, 1, 2);
    out.r = ps1_clip_screen_attribute(a->r, b->r, a->z, b->z, 1, 2);
    out.g = ps1_clip_screen_attribute(a->g, b->g, a->z, b->z, 1, 2);
    out.b = ps1_clip_screen_attribute(a->b, b->b, a->z, b->z, 1, 2);
    return out;
}

static inline int ps1_affine_edge_error(const ps1_screen_clip_vert* a,
                                       const ps1_screen_clip_vert* b) {
    int dx = a->x - b->x, dy = a->y - b->y;
    int du, dv;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx <= 32 && dy <= 32) return 0;
    du = 2 * ps1_clip_screen_attribute(a->u, b->u, a->z, b->z, 1, 2) - a->u - b->u;
    dv = 2 * ps1_clip_screen_attribute(a->v, b->v, a->z, b->z, 1, 2) - a->v - b->v;
    if (du < 0) du = -du;
    if (dv < 0) dv = -dv;
    int error = du > dv ? du : dv;
    const int min_z = a->z < b->z ? a->z : b->z;
    const int max_z = a->z > b->z ? a->z : b->z;
    /* UV distortion and painter-order errors have the same large depth-span
       trigger. Also divide long low-UV-span surfaces (stairs/roof panels), so
       a coarse rear face cannot cover fine fragments of the front wall. */
    if (max_z - min_z > min_z / 8) {
        const int depth_error = (dx > dy ? dx : dy) / 8;
        if (depth_error > error) error = depth_error;
    }
    return error;
}

/* Spend the bounded budget on the worst remaining edge across all fragments.
   A depth-first split exhausts its budget in one corner and leaves the rest of
   a close wall as a giant affine triangle. Preserve winding; the caller sorts
   each resulting fragment at its own camera-space depth. */
static inline int ps1_affine_subdivide(const ps1_screen_clip_vert triangle[3],
        ps1_screen_clip_vert output[PS1_AFFINE_FRAGMENTS][3]) {
    int count = 1;
    int errors[PS1_AFFINE_FRAGMENTS], edges[PS1_AFFINE_FRAGMENTS];
    for (int v = 0; v < 3; ++v) output[0][v] = triangle[v];
    errors[0] = 0; edges[0] = 0;
    for (int e = 0; e < 3; ++e) {
        const int error = ps1_affine_edge_error(&output[0][e], &output[0][(e + 1) % 3]);
        if (error > errors[0]) { errors[0] = error; edges[0] = e; }
    }
    while (count < PS1_AFFINE_FRAGMENTS) {
        int best_triangle = -1, best_edge = -1, largest = 4;
        for (int t = 0; t < count; ++t)
            if (errors[t] > largest) {
                largest = errors[t]; best_triangle = t; best_edge = edges[t];
            }
        if (best_triangle < 0) break;
        const ps1_screen_clip_vert a = output[best_triangle][best_edge];
        const ps1_screen_clip_vert b = output[best_triangle][(best_edge + 1) % 3];
        const ps1_screen_clip_vert c = output[best_triangle][(best_edge + 2) % 3];
        const ps1_screen_clip_vert mid = ps1_affine_midpoint(&a, &b);
        output[best_triangle][0] = a; output[best_triangle][1] = mid; output[best_triangle][2] = c;
        output[count][0] = mid; output[count][1] = b; output[count][2] = c;
        /* Only the two changed fragments need expensive depth/UV division. */
        for (int changed = 0; changed < 2; ++changed) {
            const int t = changed ? count : best_triangle;
            errors[t] = 0; edges[t] = 0;
            for (int e = 0; e < 3; ++e) {
                const int error = ps1_affine_edge_error(&output[t][e], &output[t][(e + 1) % 3]);
                if (error > errors[t]) { errors[t] = error; edges[t] = e; }
            }
        }
        ++count;
    }
    return count;
}
#endif
