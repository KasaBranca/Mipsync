/* Affine subdivision now derives every midpoint attribute from one shared
 * perspective weight. It must match the per-attribute division exactly. */
#include "../templates/ps1/starter/runtime/affine_subdivision.h"
#include <stdio.h>

static uint32_t s_seed = 0x12345678u;
static int32_t rnd(int32_t lo, int32_t hi) {
    s_seed = s_seed * 1664525u + 1013904223u;
    return lo + (int32_t)((s_seed >> 8) % (uint32_t)(hi - lo + 1));
}

static ps1_screen_clip_vert reference_midpoint(const ps1_screen_clip_vert* a,
                                               const ps1_screen_clip_vert* b) {
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

static int reference_edge_error(const ps1_screen_clip_vert* a, const ps1_screen_clip_vert* b) {
    int dx = a->x - b->x, dy = a->y - b->y, du, dv;
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
    if (max_z - min_z > min_z / 8) {
        const int depth_error = (dx > dy ? dx : dy) / 8;
        if (depth_error > error) error = depth_error;
    }
    return error;
}

static ps1_screen_clip_vert random_vertex(void) {
    ps1_screen_clip_vert v;
    v.x = rnd(-1024, 1023); v.y = rnd(-1024, 1023);
    v.z = rnd(0, 7) == 0 ? rnd(1, 64) : rnd(1, 65535);
    v.u = rnd(0, 255); v.v = rnd(0, 255);
    v.r = rnd(0, 255); v.g = rnd(0, 255); v.b = rnd(0, 255);
    return v;
}

int main(void) {
    for (int i = 0; i < 2000000; ++i) {
        const ps1_screen_clip_vert a = random_vertex();
        const ps1_screen_clip_vert b = rnd(0, 15) == 0 ? a : random_vertex();
        const ps1_screen_clip_vert e = reference_midpoint(&a, &b);
        const ps1_screen_clip_vert m = ps1_affine_midpoint(&a, &b);
        if (e.x != m.x || e.y != m.y || e.z != m.z || e.u != m.u || e.v != m.v ||
            e.r != m.r || e.g != m.g || e.b != m.b) {
            printf("midpoint mismatch: z %d/%d u %d/%d -> %d vs %d\n",
                   (int)a.z, (int)b.z, a.u, b.u, e.u, m.u);
            return 1;
        }
        if (reference_edge_error(&a, &b) != ps1_affine_edge_error(&a, &b)) {
            printf("edge error mismatch\n");
            return 1;
        }
    }
    printf("Affine subdivision shared-weight midpoints match per-attribute division\n");
    return 0;
}
