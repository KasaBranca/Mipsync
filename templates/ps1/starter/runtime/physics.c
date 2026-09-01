#include "physics.h"
#include "scene.h"

static unsigned int s_active_entity = 0xFFFFu;
static fix16_t      s_vertical_vel = 0;

#define PS1_STATIC_COLLIDER_MAX 1536
#define PS1_DYNAMIC_COLLIDER_MAX 128
#define PS1_MOVE_CANDIDATE_MAX 256
#define PS1_STATIC_NONCONVEX_VERTEX_TOTAL 2048
#define PS1_COLLISION_SKIN (FIX16_ONE / 256)
#define PS1_DEPENETRATION_ITERATIONS 4

typedef struct {
    uint16_t entity_index;
    int16_t  rigid_root_entity_index;
    int16_t  shape;
    int16_t  min_q6[3];
    int16_t  max_q6[3];
    uint16_t world_vertex_offset;
    uint16_t world_vertex_count;
} ps1_cached_collider;

static ps1_cached_collider s_static_colliders[PS1_STATIC_COLLIDER_MAX];
static uint16_t s_dynamic_colliders[PS1_DYNAMIC_COLLIDER_MAX];
static uint16_t s_move_candidates[PS1_MOVE_CANDIDATE_MAX];
static uint16_t s_static_collider_count = 0;
static uint16_t s_dynamic_collider_count = 0;
static unsigned int s_cached_scene_entity_count = 0;
static fix16_t s_static_nonconvex_world_vertices
    [PS1_STATIC_NONCONVEX_VERTEX_TOTAL][3];
static uint16_t s_static_nonconvex_vertex_used = 0;
static unsigned int s_grounded_cache_entity = 0xFFFFu;
static int s_grounded_cache_valid = 0;
static int s_grounded_cache_result = 0;

void ps1_physics_begin_frame(void) {
    s_grounded_cache_valid = 0;
}

void ps1_physics_set_active_entity(unsigned int entity_index) {
    s_active_entity = entity_index;
}

static fix16_t min_fix16(fix16_t a, fix16_t b) {
    return a < b ? a : b;
}

static fix16_t max_fix16(fix16_t a, fix16_t b) {
    return a > b ? a : b;
}

static int16_t collider_bound_q6(fix16_t value, int round_up) {
    int32_t q;
    if (round_up) {
        q = value >= 0
            ? ((int32_t)value + 1023) >> 10
            : -((-(int32_t)value) >> 10);
    } else {
        q = value >= 0
            ? (int32_t)value >> 10
            : -(((-(int32_t)value) + 1023) >> 10);
    }
    if (q < -32768) q = -32768;
    if (q > 32767) q = 32767;
    return (int16_t)q;
}

static void cache_collider_bounds(ps1_cached_collider* cached,
                                  const fix16_t min_bounds[3],
                                  const fix16_t max_bounds[3]) {
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        cached->min_q6[axis] = collider_bound_q6(min_bounds[axis], 0);
        cached->max_q6[axis] = collider_bound_q6(max_bounds[axis], 1);
    }
}

static void cached_collider_bounds(const ps1_cached_collider* cached,
                                   fix16_t min_bounds[3],
                                   fix16_t max_bounds[3]) {
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        min_bounds[axis] = (fix16_t)((int32_t)cached->min_q6[axis] << 10);
        max_bounds[axis] = (fix16_t)((int32_t)cached->max_q6[axis] << 10);
    }
}

static int aabb_overlaps(const fix16_t min_a[3], const fix16_t max_a[3],
                         const fix16_t min_b[3], const fix16_t max_b[3]) {
    return min_a[0] < max_b[0] && max_a[0] > min_b[0] &&
           min_a[1] < max_b[1] && max_a[1] > min_b[1] &&
           min_a[2] < max_b[2] && max_a[2] > min_b[2];
}

static int bounds_overlap_xz(const fix16_t min_a[3], const fix16_t max_a[3],
                             const fix16_t min_b[3], const fix16_t max_b[3]) {
    return min_a[0] < max_b[0] && max_a[0] > min_b[0] &&
           min_a[2] < max_b[2] && max_a[2] > min_b[2];
}

static int cached_q6_overlaps(const ps1_cached_collider* cached,
                              const int16_t min_q6[3],
                              const int16_t max_q6[3]) {
    return cached->min_q6[0] < max_q6[0] &&
           cached->max_q6[0] > min_q6[0] &&
           cached->min_q6[1] < max_q6[1] &&
           cached->max_q6[1] > min_q6[1] &&
           cached->min_q6[2] < max_q6[2] &&
           cached->max_q6[2] > min_q6[2];
}

static void collider_rotation_axes(const ps1_entity* e,
                                   fix16_t right[3],
                                   fix16_t up[3],
                                   fix16_t forward[3]) {
    const fix16_t pitch =
        fix16_div(e->rotation[0], FIX16_FROM_INT(360));
    const fix16_t yaw =
        fix16_div(e->rotation[1], FIX16_FROM_INT(360));
    const fix16_t roll =
        fix16_div(e->rotation[2], FIX16_FROM_INT(360));
    const fix16_t sp = fix16_sin(pitch);
    const fix16_t cp = fix16_cos(pitch);
    const fix16_t sy = fix16_sin(yaw);
    const fix16_t cy = fix16_cos(yaw);
    const fix16_t sr = fix16_sin(roll);
    const fix16_t cr = fix16_cos(roll);

    right[0] = fix16_add(fix16_mul(cy, cr),
                         fix16_mul(fix16_mul(sy, sp), sr));
    right[1] = fix16_mul(cp, sr);
    right[2] = fix16_sub(fix16_mul(fix16_mul(cy, sp), sr),
                         fix16_mul(sy, cr));
    up[0] = fix16_sub(fix16_mul(fix16_mul(sy, sp), cr),
                      fix16_mul(cy, sr));
    up[1] = fix16_mul(cp, cr);
    up[2] = fix16_add(fix16_mul(sy, sr),
                      fix16_mul(fix16_mul(cy, sp), cr));
    /* Physics stays in the editor's Unity-style world coordinates. The
     * renderer performs its own PS1 Z-axis conversion later. */
    forward[0] = fix16_mul(sy, cp);
    forward[1] = fix16_neg(sp);
    forward[2] = fix16_mul(cy, cp);
}

static void get_world_aabb(const ps1_entity* e, fix16_t min_out[3], fix16_t max_out[3]) {
    fix16_t center[3];
    fix16_t half[3];
    fix16_t right[3], up[3], forward[3];
    fix16_t local_center[3];
    fix16_t scale_x;
    fix16_t scale_y;
    fix16_t scale_z;

    if (e->collider_shape == -1) {
        min_out[0] = e->position[0]; max_out[0] = e->position[0];
        min_out[1] = e->position[1]; max_out[1] = e->position[1];
        min_out[2] = e->position[2]; max_out[2] = e->position[2];
        return;
    }

    scale_x = fix16_abs(e->scale[0]);
    scale_y = fix16_abs(e->scale[1]);
    scale_z = fix16_abs(e->scale[2]);

    /* Character controllers use an upright capsule with an origin-centred
     * X/Z offset. Yaw cannot change that AABB, so avoid rebuilding Euler axes
     * (three software divisions plus six trig lookups) on every ground and
     * movement query. */
    if (e->collider_shape == 2 &&
        e->collider_center[0] == 0 && e->collider_center[2] == 0 &&
        fix16_abs(e->rotation[0]) <= FIX16_ONE / 1024 &&
        fix16_abs(e->rotation[2]) <= FIX16_ONE / 1024) {
        const fix16_t radius = fix16_mul(e->collider_radius, scale_x);
        const fix16_t height =
            fix16_mul(e->collider_capsule_height, scale_y);
        center[0] = e->position[0];
        center[1] = fix16_add(
            e->position[1], fix16_mul(e->collider_center[1], e->scale[1]));
        center[2] = e->position[2];
        half[0] = radius;
        half[1] = fix16_add(
            radius, fix16_div(height, FIX16_FROM_INT(2)));
        half[2] = radius;
        min_out[0] = fix16_sub(center[0], half[0]);
        max_out[0] = fix16_add(center[0], half[0]);
        min_out[1] = fix16_sub(center[1], half[1]);
        max_out[1] = fix16_add(center[1], half[1]);
        min_out[2] = fix16_sub(center[2], half[2]);
        max_out[2] = fix16_add(center[2], half[2]);
        return;
    }
    collider_rotation_axes(e, right, up, forward);
    local_center[0] =
        fix16_mul(e->collider_center[0], e->scale[0]);
    local_center[1] =
        fix16_mul(e->collider_center[1], e->scale[1]);
    local_center[2] =
        fix16_mul(e->collider_center[2], e->scale[2]);
    center[0] = fix16_add(
        e->position[0],
        fix16_add(fix16_add(fix16_mul(right[0], local_center[0]),
                            fix16_mul(up[0], local_center[1])),
                  fix16_mul(forward[0], local_center[2])));
    center[1] = fix16_add(
        e->position[1],
        fix16_add(fix16_add(fix16_mul(right[1], local_center[0]),
                            fix16_mul(up[1], local_center[1])),
                  fix16_mul(forward[1], local_center[2])));
    center[2] = fix16_add(
        e->position[2],
        fix16_add(fix16_add(fix16_mul(right[2], local_center[0]),
                            fix16_mul(up[2], local_center[1])),
                  fix16_mul(forward[2], local_center[2])));

    if (e->collider_shape == 0) { /* Box */
        half[0] = fix16_mul(e->collider_half_extents[0], scale_x);
        half[1] = fix16_mul(e->collider_half_extents[1], scale_y);
        half[2] = fix16_mul(e->collider_half_extents[2], scale_z);
    } else if (e->collider_shape == 1) { /* Sphere */
        fix16_t r = fix16_mul(e->collider_radius, scale_x);
        half[0] = half[1] = half[2] = r;
    } else if (e->collider_shape == 2) { /* Capsule */
        fix16_t r = fix16_mul(e->collider_radius, scale_x);
        fix16_t h = fix16_mul(e->collider_capsule_height, scale_y);
        half[0] = half[2] = r;
        half[1] = fix16_add(r, fix16_div(h, FIX16_FROM_INT(2)));
    } else { /* MeshPlane/Fallback */
        half[0] = fix16_mul(e->collider_half_extents[0], scale_x);
        half[1] = fix16_mul(e->collider_half_extents[1], scale_y);
        half[2] = fix16_mul(e->collider_half_extents[2], scale_z);
    }

    if (e->collider_shape != 1) {
        const fix16_t hx = half[0];
        const fix16_t hy = half[1];
        const fix16_t hz = half[2];
        half[0] = fix16_add(
            fix16_add(fix16_mul(fix16_abs(right[0]), hx),
                      fix16_mul(fix16_abs(up[0]), hy)),
            fix16_mul(fix16_abs(forward[0]), hz));
        half[1] = fix16_add(
            fix16_add(fix16_mul(fix16_abs(right[1]), hx),
                      fix16_mul(fix16_abs(up[1]), hy)),
            fix16_mul(fix16_abs(forward[1]), hz));
        half[2] = fix16_add(
            fix16_add(fix16_mul(fix16_abs(right[2]), hx),
                      fix16_mul(fix16_abs(up[2]), hy)),
            fix16_mul(fix16_abs(forward[2]), hz));
    }

    min_out[0] = fix16_sub(center[0], half[0]);
    max_out[0] = fix16_add(center[0], half[0]);
    min_out[1] = fix16_sub(center[1], half[1]);
    max_out[1] = fix16_add(center[1], half[1]);
    min_out[2] = fix16_sub(center[2], half[2]);
    max_out[2] = fix16_add(center[2], half[2]);
}

static const ps1_mesh* nonconvex_mesh_data(const ps1_entity* entity) {
    if (!entity || entity->collider_shape != 3 || entity->collider_convex ||
        entity->collider_mesh_index == 0 ||
        entity->collider_mesh_index > g_ps1_mesh_count)
        return 0;
    return &g_ps1_meshes[entity->collider_mesh_index - 1u];
}

static void mesh_vertex_world(const ps1_entity* entity,
                              const ps1_mesh* mesh,
                              const SVECTOR* vertex,
                              const fix16_t right[3],
                              const fix16_t up[3],
                              const fix16_t forward[3],
                              fix16_t out[3]) {
    fix16_t local[3];
    /* Exported custom vertices are normalized to +/-1024. scale_q12 restores
     * source size in PSX units (64 units per world unit), hence /4 here. */
    local[0] = fix16_mul(
        (fix16_t)(((int64_t)vertex->vx * mesh->scale_q12) >> 2),
        entity->scale[0]);
    local[1] = fix16_mul(
        (fix16_t)(((int64_t)vertex->vy * mesh->scale_q12) >> 2),
        entity->scale[1]);
    local[2] = fix16_mul(
        (fix16_t)(((int64_t)vertex->vz * mesh->scale_q12) >> 2),
        entity->scale[2]);
    out[0] = fix16_add(entity->position[0],
        fix16_add(fix16_add(fix16_mul(right[0], local[0]),
                            fix16_mul(up[0], local[1])),
                  fix16_mul(forward[0], local[2])));
    out[1] = fix16_add(entity->position[1],
        fix16_add(fix16_add(fix16_mul(right[1], local[0]),
                            fix16_mul(up[1], local[1])),
                  fix16_mul(forward[1], local[2])));
    out[2] = fix16_add(entity->position[2],
        fix16_add(fix16_add(fix16_mul(right[2], local[0]),
                            fix16_mul(up[2], local[1])),
                  fix16_mul(forward[2], local[2])));
}

#define PS1_NONCONVEX_VERTEX_MAX 1024
static const ps1_entity* s_nonconvex_cached_entity = 0;
static const ps1_mesh* s_nonconvex_cached_mesh = 0;
static fix16_t s_nonconvex_cached_position[3];
static fix16_t s_nonconvex_cached_rotation[3];
static fix16_t s_nonconvex_cached_scale[3];
static fix16_t s_nonconvex_world_vertices[PS1_NONCONVEX_VERTEX_MAX][3];

static const ps1_mesh* prepare_nonconvex_mesh(
        const ps1_entity* entity,
        const fix16_t (**out_world_vertices)[3]) {
    const ps1_mesh* mesh = nonconvex_mesh_data(entity);
    int dirty;
    unsigned int cached_index;
    if (out_world_vertices)
        *out_world_vertices = 0;
    if (!mesh || mesh->vert_count > PS1_NONCONVEX_VERTEX_MAX)
        return 0;

    /* Static non-convex colliders own a persistent transformed-vertex slice.
     * The old one-entry cache thrashed when four floors were queried in turn,
     * redoing every Q16 transform on every physics query. */
    for (cached_index = 0; cached_index < s_static_collider_count;
         ++cached_index) {
        const ps1_cached_collider* cached = &s_static_colliders[cached_index];
        if (cached->world_vertex_count == mesh->vert_count &&
            ps1_scene_entity(cached->entity_index) == entity) {
            if (out_world_vertices) {
                *out_world_vertices =
                    &s_static_nonconvex_world_vertices
                        [cached->world_vertex_offset];
            }
            return mesh;
        }
    }
    dirty = entity != s_nonconvex_cached_entity || mesh != s_nonconvex_cached_mesh;
    for (int axis = 0; axis < 3 && !dirty; ++axis) {
        dirty = entity->position[axis] != s_nonconvex_cached_position[axis] ||
                entity->rotation[axis] != s_nonconvex_cached_rotation[axis] ||
                entity->scale[axis] != s_nonconvex_cached_scale[axis];
    }
    if (dirty) {
        fix16_t right[3], up[3], forward[3];
        collider_rotation_axes(entity, right, up, forward);
        for (uint16_t i = 0; i < mesh->vert_count; ++i)
            mesh_vertex_world(entity, mesh, &mesh->verts[i], right, up, forward,
                              s_nonconvex_world_vertices[i]);
        for (int axis = 0; axis < 3; ++axis) {
            s_nonconvex_cached_position[axis] = entity->position[axis];
            s_nonconvex_cached_rotation[axis] = entity->rotation[axis];
            s_nonconvex_cached_scale[axis] = entity->scale[axis];
        }
        s_nonconvex_cached_entity = entity;
        s_nonconvex_cached_mesh = mesh;
    }
    if (out_world_vertices)
        *out_world_vertices = s_nonconvex_world_vertices;
    return mesh;
}

/* Compute floor((numerator / denominator) * 65536) without overflowing a
 * 64-bit intermediate. Both inputs are non-negative and numerator <=
 * denominator. */
static fix16_t ratio_u64_q16(uint64_t numerator, uint64_t denominator) {
    uint32_t result = 0;
    int bit;
    if (!denominator || numerator >= denominator)
        return FIX16_ONE;
    for (bit = 0; bit < 16; ++bit) {
        result <<= 1;
        /* This is remainder *= 2 followed by a comparison, written this way
         * so a large determinant cannot overflow uint64_t. */
        if (numerator >= denominator - numerator) {
            numerator -= denominator - numerator;
            result |= 1u;
        } else {
            numerator += numerator;
        }
    }
    return (fix16_t)result;
}

/* Two-sided fixed-point Moller-Trumbore segment/triangle intersection. World
 * values are reduced from Q16 to Q8 first; that is still 1/256 world-unit
 * precision and keeps the PS1's 64-bit software arithmetic bounded. */
static int camera_segment_triangle_hit(const fix16_t from[3],
                                       const fix16_t desired[3],
                                       const fix16_t a[3],
                                       const fix16_t b[3],
                                       const fix16_t c[3],
                                       fix16_t* out_t) {
    int64_t d[3], edge1[3], edge2[3], origin_to_a[3];
    int64_t p[3], q[3];
    int64_t determinant, u, v, t;
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        const int64_t origin = (int64_t)from[axis] >> 8;
        d[axis] = ((int64_t)desired[axis] >> 8) - origin;
        edge1[axis] = ((int64_t)b[axis] >> 8) - ((int64_t)a[axis] >> 8);
        edge2[axis] = ((int64_t)c[axis] >> 8) - ((int64_t)a[axis] >> 8);
        origin_to_a[axis] = origin - ((int64_t)a[axis] >> 8);
    }

    p[0] = d[1] * edge2[2] - d[2] * edge2[1];
    p[1] = d[2] * edge2[0] - d[0] * edge2[2];
    p[2] = d[0] * edge2[1] - d[1] * edge2[0];
    determinant = edge1[0] * p[0] + edge1[1] * p[1] + edge1[2] * p[2];
    if (determinant == 0)
        return 0;

    u = origin_to_a[0] * p[0] + origin_to_a[1] * p[1] +
        origin_to_a[2] * p[2];
    q[0] = origin_to_a[1] * edge1[2] - origin_to_a[2] * edge1[1];
    q[1] = origin_to_a[2] * edge1[0] - origin_to_a[0] * edge1[2];
    q[2] = origin_to_a[0] * edge1[1] - origin_to_a[1] * edge1[0];
    v = d[0] * q[0] + d[1] * q[1] + d[2] * q[2];
    t = edge2[0] * q[0] + edge2[1] * q[1] + edge2[2] * q[2];

    if (determinant < 0) {
        determinant = -determinant;
        u = -u;
        v = -v;
        t = -t;
    }
    if (u < 0 || v < 0 || u > determinant || v > determinant - u ||
        t <= 0 || t > determinant)
        return 0;

    *out_t = ratio_u64_q16((uint64_t)t, (uint64_t)determinant);
    return 1;
}

static int camera_segment_aabb_hit(const fix16_t from[3],
                                   const fix16_t desired[3],
                                   const fix16_t min_bounds[3],
                                   const fix16_t max_bounds[3],
                                   fix16_t clearance,
                                   fix16_t* out_t) {
    fix16_t enter = 0;
    fix16_t leave = FIX16_ONE;
    int starts_inside = 1;
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        const fix16_t lower = fix16_sub(min_bounds[axis], clearance);
        const fix16_t upper = fix16_add(max_bounds[axis], clearance);
        const fix16_t delta = fix16_sub(desired[axis], from[axis]);
        fix16_t first, second;
        if (from[axis] < lower || from[axis] > upper)
            starts_inside = 0;
        if (delta == 0) {
            if (from[axis] < lower || from[axis] > upper)
                return 0;
            continue;
        }
        first = fix16_div(fix16_sub(lower, from[axis]), delta);
        second = fix16_div(fix16_sub(upper, from[axis]), delta);
        if (first > second) {
            const fix16_t swap = first;
            first = second;
            second = swap;
        }
        if (first > enter) enter = first;
        if (second < leave) leave = second;
        if (enter > leave)
            return 0;
    }

    /* An overlap at the look pivot is normally the player's floor or a wall
     * beside the player. It must not collapse the camera onto the pivot. */
    if (starts_inside || leave < 0 || enter > FIX16_ONE)
        return 0;
    *out_t = enter < 0 ? 0 : enter;
    return 1;
}

/* Plane primitives are exported as BoxColliders with one zero half-extent.
 * Keep the camera on the same side as its look pivot explicitly; this also
 * closes the fixed-point edge case where the pivot starts inside the expanded
 * zero-thickness slab and a conventional sweep reports an initial overlap. */
static int camera_flat_aabb_hit(const fix16_t from[3],
                                const fix16_t desired[3],
                                const fix16_t min_bounds[3],
                                const fix16_t max_bounds[3],
                                fix16_t clearance,
                                fix16_t* out_t) {
    int flat_axis = -1;
    int axis;
    for (axis = 0; axis < 3; ++axis) {
        if (fix16_abs(fix16_sub(max_bounds[axis], min_bounds[axis])) <=
            PS1_COLLISION_SKIN * 2) {
            flat_axis = axis;
            break;
        }
    }
    if (flat_axis < 0)
        return 0;

    {
        const fix16_t surface = fix16_add(
            min_bounds[flat_axis],
            fix16_div(fix16_sub(max_bounds[flat_axis], min_bounds[flat_axis]),
                      FIX16_FROM_INT(2)));
        const fix16_t from_side = fix16_sub(from[flat_axis], surface);
        const fix16_t desired_side = fix16_sub(desired[flat_axis], surface);
        const fix16_t limit = from_side >= 0
            ? fix16_add(surface, clearance)
            : fix16_sub(surface, clearance);
        const fix16_t delta = fix16_sub(desired[flat_axis], from[flat_axis]);
        fix16_t hit_t;

        if (delta == 0 ||
            (from_side >= 0 && desired_side >= clearance) ||
            (from_side < 0 && desired_side <= fix16_neg(clearance)))
            return 0;
        hit_t = fix16_div(fix16_sub(limit, from[flat_axis]), delta);
        if (hit_t < 0) hit_t = 0;
        if (hit_t > FIX16_ONE) return 0;

        for (axis = 0; axis < 3; ++axis) {
            fix16_t at_hit;
            if (axis == flat_axis)
                continue;
            at_hit = fix16_add(from[axis], fix16_mul(
                fix16_sub(desired[axis], from[axis]), hit_t));
            if (at_hit < fix16_sub(min_bounds[axis], clearance) ||
                at_hit > fix16_add(max_bounds[axis], clearance))
                return 0;
        }
        *out_t = hit_t;
        return 1;
    }
}

/* A rendered Plane is an infinitely thin surface even when its BoxCollider is
 * deliberately given depth.  Testing the collider AABB alone is not enough:
 * once the camera target is inside that depth, the usual sweep treats it as an
 * initial overlap and ignores it.  Always sweep against the actual rendered
 * plane and keep the camera on the same side as the look pivot. */
static int camera_primitive_plane_hit(const ps1_entity* plane,
                                      const fix16_t from[3],
                                      const fix16_t desired[3],
                                      fix16_t clearance,
                                      fix16_t* out_t) {
    fix16_t right[3], up[3], forward[3];
    fix16_t from_delta[3], desired_delta[3];
    fix16_t from_distance = 0;
    fix16_t desired_distance = 0;
    fix16_t delta_distance;
    fix16_t side;
    fix16_t limit;
    fix16_t hit_t;
    fix16_t hit_delta[3];
    fix16_t local_x = 0;
    fix16_t local_z = 0;
    fix16_t footprint_center_x;
    fix16_t footprint_center_z;
    fix16_t footprint_half_x;
    fix16_t footprint_half_z;
    int axis;

    if (!plane || plane->mesh != PS1_MESH_PLANE)
        return 0;

    collider_rotation_axes(plane, right, up, forward);
    for (axis = 0; axis < 3; ++axis) {
        from_delta[axis] = fix16_sub(from[axis], plane->position[axis]);
        desired_delta[axis] = fix16_sub(desired[axis], plane->position[axis]);
        from_distance = fix16_add(
            from_distance, fix16_mul(from_delta[axis], up[axis]));
        desired_distance = fix16_add(
            desired_distance, fix16_mul(desired_delta[axis], up[axis]));
    }

    side = from_distance >= 0 ? FIX16_ONE : fix16_neg(FIX16_ONE);
    limit = fix16_mul(side, clearance);
    /* The candidate is still safely on the pivot's side. */
    if (fix16_mul(side, desired_distance) >= clearance)
        return 0;

    delta_distance = fix16_sub(desired_distance, from_distance);
    if (delta_distance == 0)
        return 0;
    hit_t = fix16_div(fix16_sub(limit, from_distance), delta_distance);
    if (hit_t < 0 || hit_t > FIX16_ONE)
        return 0;

    for (axis = 0; axis < 3; ++axis) {
        hit_delta[axis] = fix16_add(
            from_delta[axis],
            fix16_mul(fix16_sub(desired_delta[axis], from_delta[axis]), hit_t));
        local_x = fix16_add(local_x, fix16_mul(hit_delta[axis], right[axis]));
        local_z = fix16_add(local_z, fix16_mul(hit_delta[axis], forward[axis]));
    }

    footprint_center_x = fix16_mul(
        plane->collider_center[0], plane->scale[0]);
    footprint_center_z = fix16_mul(
        plane->collider_center[2], plane->scale[2]);
    footprint_half_x = fix16_add(
        fix16_mul(fix16_abs(plane->collider_half_extents[0]),
                  fix16_abs(plane->scale[0])), clearance);
    footprint_half_z = fix16_add(
        fix16_mul(fix16_abs(plane->collider_half_extents[2]),
                  fix16_abs(plane->scale[2])), clearance);

    /* Older scenes can have an uninitialised/tiny Plane collider footprint.
     * Fall back to the renderer's mesh size so collision matches what is seen. */
    if (footprint_half_x <= clearance)
        footprint_half_x = fix16_add(
            fix16_mul(fix16_abs(plane->mesh_size),
                      fix16_abs(plane->scale[0])) / 2, clearance);
    if (footprint_half_z <= clearance)
        footprint_half_z = fix16_add(
            fix16_mul(fix16_abs(plane->mesh_size),
                      fix16_abs(plane->scale[2])) / 2, clearance);

    if (fix16_abs(fix16_sub(local_x, footprint_center_x)) > footprint_half_x ||
        fix16_abs(fix16_sub(local_z, footprint_center_z)) > footprint_half_z)
        return 0;

    *out_t = hit_t;
    return 1;
}

static int camera_collider_is_related(const ps1_entity* target,
                                      unsigned int target_index,
                                      unsigned int camera_index,
                                      unsigned int other_index,
                                      int16_t other_root_index) {
    return other_index == target_index || other_index == camera_index ||
           other_root_index == (int16_t)target_index ||
           (int16_t)other_index == target->rigid_root_entity_index ||
           (target->rigid_root_entity_index >= 0 &&
            other_root_index == target->rigid_root_entity_index);
}

static int camera_collider_hit(const ps1_entity* collider,
                               const fix16_t from[3],
                               const fix16_t desired[3],
                               const fix16_t min_bounds[3],
                               const fix16_t max_bounds[3],
                               fix16_t clearance,
                               fix16_t* out_t) {
    const ps1_mesh* mesh = nonconvex_mesh_data(collider);
    const fix16_t (*world_vertices)[3] = 0;
    /* A primitive Plane has no front volume. The PS1 renderer cannot clip a
     * large polygon cleanly against the near plane, so the view can cross or
     * drop that polygon before the camera center reaches the BoxCollider.
     * Keep the near-plane footprint clear without changing ordinary boxes. */
    const fix16_t effective_clearance = collider->mesh == PS1_MESH_PLANE
        ? max_fix16(clearance, FIX16_ONE / 2)
        : clearance;
    if (camera_primitive_plane_hit(collider, from, desired,
                                   effective_clearance, out_t))
        return 1;
    if (!mesh && camera_flat_aabb_hit(from, desired, min_bounds, max_bounds,
                                       effective_clearance, out_t))
        return 1;
    if (mesh) {
        fix16_t best = FIX16_ONE;
        fix16_t delta_max = 0;
        fix16_t margin_t;
        int found = 0;
        int axis;

        if (!camera_segment_aabb_hit(from, desired, min_bounds, max_bounds,
                                     effective_clearance, out_t)) {
            /* The center ray can still cross a zero-thickness Plane even when
             * its expanded AABB test starts on a boundary, so only use this as
             * a broad rejection when the unexpanded segment also misses. */
            if (!camera_segment_aabb_hit(from, desired, min_bounds, max_bounds,
                                         0, out_t))
                return 0;
        }
        mesh = prepare_nonconvex_mesh(collider, &world_vertices);
        if (!mesh || !mesh->verts || !mesh->tris)
            return camera_segment_aabb_hit(from, desired, min_bounds, max_bounds,
                                           effective_clearance, out_t);
        for (axis = 0; axis < 3; ++axis) {
            const fix16_t magnitude = fix16_abs(
                fix16_sub(desired[axis], from[axis]));
            if (magnitude > delta_max) delta_max = magnitude;
        }
        margin_t = delta_max > 0
            ? fix16_div(effective_clearance, delta_max) : 0;
        for (uint16_t triangle_index = 0;
             triangle_index < mesh->tri_count; ++triangle_index) {
            const ps1_mesh_tri* triangle = &mesh->tris[triangle_index];
            fix16_t hit_t;
            if (triangle->i0 >= mesh->vert_count ||
                triangle->i1 >= mesh->vert_count ||
                triangle->i2 >= mesh->vert_count)
                continue;
            if (camera_segment_triangle_hit(
                    from, desired,
                    world_vertices[triangle->i0],
                    world_vertices[triangle->i1],
                    world_vertices[triangle->i2], &hit_t)) {
                hit_t = hit_t > margin_t ? fix16_sub(hit_t, margin_t) : 0;
                if (!found || hit_t < best) {
                    best = hit_t;
                    found = 1;
                }
            }
        }
        if (found) *out_t = best;
        return found;
    }
    return camera_segment_aabb_hit(from, desired, min_bounds, max_bounds,
                                   effective_clearance, out_t);
}

/* A projected mesh triangle is clipped against the moving collider's
 * rectangular footprint.  Keeping the third coordinate as `value` gives us
 * the exact plane height/depth at every vertex of the intersection polygon.
 * A triangle clipped by a rectangle has at most seven vertices. */
typedef struct ps1_projected_vertex {
    int64_t u;
    int64_t v;
    int64_t value;
} ps1_projected_vertex;

static int64_t projected_component(const ps1_projected_vertex* vertex,
                                   int component) {
    return component == 0 ? vertex->u : vertex->v;
}

static int clip_projected_edge(const ps1_projected_vertex* input,
                               int input_count,
                               ps1_projected_vertex* output,
                               int component,
                               int64_t boundary,
                               int keep_greater) {
    int output_count = 0;
    if (input_count <= 0)
        return 0;
    for (int i = 0; i < input_count; ++i) {
        const ps1_projected_vertex* start =
            &input[(i + input_count - 1) % input_count];
        const ps1_projected_vertex* end = &input[i];
        const int64_t start_component = projected_component(start, component);
        const int64_t end_component = projected_component(end, component);
        const int start_inside = keep_greater ? start_component >= boundary
                                              : start_component <= boundary;
        const int end_inside = keep_greater ? end_component >= boundary
                                            : end_component <= boundary;
        if (start_inside != end_inside && output_count < 8) {
            const int64_t denominator = end_component - start_component;
            ps1_projected_vertex intersection = *start;
            if (denominator != 0) {
                intersection.u = start->u +
                    (end->u - start->u) * (boundary - start_component) /
                        denominator;
                intersection.v = start->v +
                    (end->v - start->v) * (boundary - start_component) /
                        denominator;
                intersection.value = start->value +
                    (end->value - start->value) *
                        (boundary - start_component) / denominator;
            }
            if (component == 0) intersection.u = boundary;
            else intersection.v = boundary;
            output[output_count++] = intersection;
        }
        if (end_inside && output_count < 8)
            output[output_count++] = *end;
    }
    return output_count;
}

static int clip_projected_triangle(const ps1_projected_vertex triangle[3],
                                   int64_t min_u,
                                   int64_t max_u,
                                   int64_t min_v,
                                   int64_t max_v,
                                   ps1_projected_vertex output[8]) {
    ps1_projected_vertex buffers[2][8];
    int count = 3;
    int source = 0;
    for (int i = 0; i < 3; ++i)
        buffers[0][i] = triangle[i];
    count = clip_projected_edge(buffers[source], count, buffers[1 - source],
                                0, min_u, 1);
    source = 1 - source;
    count = clip_projected_edge(buffers[source], count, buffers[1 - source],
                                0, max_u, 0);
    source = 1 - source;
    count = clip_projected_edge(buffers[source], count, buffers[1 - source],
                                1, min_v, 1);
    source = 1 - source;
    count = clip_projected_edge(buffers[source], count, buffers[1 - source],
                                1, max_v, 0);
    source = 1 - source;
    for (int i = 0; i < count; ++i)
        output[i] = buffers[source][i];
    return count;
}

static int nonconvex_triangle_dominant_axis(const int64_t normal[3]) {
    const int64_t ax = normal[0] < 0 ? -normal[0] : normal[0];
    const int64_t ay = normal[1] < 0 ? -normal[1] : normal[1];
    const int64_t az = normal[2] < 0 ? -normal[2] : normal[2];
    /* Prefer Y on ties so walkable 45-degree faces remain floors instead of
     * becoming invisible horizontal walls. */
    if (ay >= ax && ay >= az)
        return 1;
    return ax >= az ? 0 : 2;
}

static int nonconvex_mesh_floor_height_footprint(const ps1_entity* entity,
                                                 const fix16_t min_bounds[3],
                                                 const fix16_t max_bounds[3],
                                                 fix16_t max_surface,
                                                 fix16_t* out_height) {
    const fix16_t (*world_vertices)[3] = 0;
    const ps1_mesh* mesh = prepare_nonconvex_mesh(entity, &world_vertices);
    const int64_t min_x = min_bounds[0] >> 8;
    const int64_t max_x = max_bounds[0] >> 8;
    const int64_t min_z = min_bounds[2] >> 8;
    const int64_t max_z = max_bounds[2] >> 8;
    fix16_t best = (fix16_t)0x80000000;
    int found = 0;
    if (!mesh || !mesh->verts || !mesh->tris)
        return 0;
    for (uint16_t i = 0; i < mesh->tri_count; ++i) {
        const ps1_mesh_tri* tri = &mesh->tris[i];
        ps1_projected_vertex projected[3];
        ps1_projected_vertex clipped[8];
        int64_t edge_ab[3], edge_ac[3], normal[3];
        int clipped_count;
        if (tri->i0 >= mesh->vert_count || tri->i1 >= mesh->vert_count ||
            tri->i2 >= mesh->vert_count)
            continue;
        {
            const uint16_t indices[3] = { tri->i0, tri->i1, tri->i2 };
            for (int vertex = 0; vertex < 3; ++vertex) {
                projected[vertex].u =
                    world_vertices[indices[vertex]][0] >> 8;
                projected[vertex].v =
                    world_vertices[indices[vertex]][2] >> 8;
                projected[vertex].value =
                    world_vertices[indices[vertex]][1] >> 8;
            }
        }
        /* Reject triangles that cannot touch the capsule footprint before
         * paying for 64-bit cross products and polygon clipping. Large
         * subdivided floors otherwise run the exact test for every face. */
        {
            int64_t tri_min_x = projected[0].u;
            int64_t tri_max_x = projected[0].u;
            int64_t tri_min_z = projected[0].v;
            int64_t tri_max_z = projected[0].v;
            for (int vertex = 1; vertex < 3; ++vertex) {
                if (projected[vertex].u < tri_min_x)
                    tri_min_x = projected[vertex].u;
                if (projected[vertex].u > tri_max_x)
                    tri_max_x = projected[vertex].u;
                if (projected[vertex].v < tri_min_z)
                    tri_min_z = projected[vertex].v;
                if (projected[vertex].v > tri_max_z)
                    tri_max_z = projected[vertex].v;
            }
            if (tri_max_x < min_x || tri_min_x > max_x ||
                tri_max_z < min_z || tri_min_z > max_z)
                continue;
        }
        edge_ab[0] = projected[1].u - projected[0].u;
        edge_ab[1] = projected[1].value - projected[0].value;
        edge_ab[2] = projected[1].v - projected[0].v;
        edge_ac[0] = projected[2].u - projected[0].u;
        edge_ac[1] = projected[2].value - projected[0].value;
        edge_ac[2] = projected[2].v - projected[0].v;
        normal[0] = edge_ab[1] * edge_ac[2] - edge_ab[2] * edge_ac[1];
        normal[1] = edge_ab[2] * edge_ac[0] - edge_ab[0] * edge_ac[2];
        normal[2] = edge_ab[0] * edge_ac[1] - edge_ab[1] * edge_ac[0];
        if (nonconvex_triangle_dominant_axis(normal) != 1 ||
            (normal[1] > -4 && normal[1] < 4))
            continue;
        clipped_count = clip_projected_triangle(
            projected, min_x, max_x, min_z, max_z, clipped);
        for (int vertex = 0; vertex < clipped_count; ++vertex) {
            const fix16_t height = (fix16_t)(clipped[vertex].value * 256);
            if (height <= max_surface && (!found || height > best)) {
                best = height;
                found = 1;
            }
        }
    }
    if (found && out_height)
        *out_height = best;
    return found;
}

static int resolve_nonconvex_mesh_floor(ps1_entity* active,
                                        const ps1_entity* floor_entity,
                                        fix16_t disp,
                                        const fix16_t previous_min[3],
                                        fix16_t min_a[3],
                                        fix16_t max_a[3]) {
    const fix16_t tolerance = FIX16_ONE / 20;
    fix16_t surface;
    if (disp >= 0 || !nonconvex_mesh_floor_height_footprint(
            floor_entity, min_a, max_a,
            fix16_add(previous_min[1], tolerance), &surface))
        return 0;
    if (previous_min[1] < fix16_sub(surface, tolerance) ||
        min_a[1] > fix16_add(surface, tolerance))
        return 0;
    active->position[1] = fix16_add(
        active->position[1],
        fix16_add(fix16_sub(surface, min_a[1]), PS1_COLLISION_SKIN));
    s_vertical_vel = 0;
    get_world_aabb(active, min_a, max_a);
    return 1;
}

#if 0 /* Replaced by exact triangle-plane sweep below. */
static int resolve_nonconvex_mesh_wall(ps1_entity* active,
                                       const ps1_entity* mesh_entity,
                                       int axis,
                                       fix16_t disp,
                                       const fix16_t previous_min[3],
                                       const fix16_t previous_max[3],
                                       fix16_t min_a[3],
                                       fix16_t max_a[3]) {
    const fix16_t (*world_vertices)[3] = 0;
    const ps1_mesh* mesh = prepare_nonconvex_mesh(
        mesh_entity, &world_vertices);
    fix16_t boundary = 0;
    const fix16_t tolerance = FIX16_ONE / 100;
    int found = 0;
    const int side_axis = axis == 0 ? 2 : 0;
    if (!mesh || (axis != 0 && axis != 2) || disp == 0)
        return 0;

    for (uint16_t i = 0; i < mesh->tri_count; ++i) {
        const ps1_mesh_tri* triangle = &mesh->tris[i];
        fix16_t world[3][3];
        fix16_t tri_min[3], tri_max[3];
        int64_t ab[3], ac[3], nx, ny, nz, horizontal;
        if (triangle->i0 >= mesh->vert_count ||
            triangle->i1 >= mesh->vert_count ||
            triangle->i2 >= mesh->vert_count)
            continue;
        for (int component = 0; component < 3; ++component) {
            world[0][component] = world_vertices[triangle->i0][component];
            world[1][component] = world_vertices[triangle->i1][component];
            world[2][component] = world_vertices[triangle->i2][component];
        }
        for (int component = 0; component < 3; ++component) {
            tri_min[component] = min_fix16(world[0][component],
                min_fix16(world[1][component], world[2][component]));
            tri_max[component] = max_fix16(world[0][component],
                max_fix16(world[1][component], world[2][component]));
            ab[component] = (world[1][component] - world[0][component]) >> 8;
            ac[component] = (world[2][component] - world[0][component]) >> 8;
        }
        nx = ab[1] * ac[2] - ab[2] * ac[1];
        ny = ab[2] * ac[0] - ab[0] * ac[2];
        nz = ab[0] * ac[1] - ab[1] * ac[0];
        if (nx < 0) nx = -nx;
        if (ny < 0) ny = -ny;
        if (nz < 0) nz = -nz;
        horizontal = nx > nz ? nx : nz;
        if (horizontal == 0 || ny > horizontal)
            continue;
        if (max_a[1] <= tri_min[1] || min_a[1] >= tri_max[1] ||
            max_a[side_axis] <= tri_min[side_axis] ||
            min_a[side_axis] >= tri_max[side_axis])
            continue;

        if (disp > 0) {
            const fix16_t candidate = tri_min[axis];
            if (previous_max[axis] > fix16_add(candidate, tolerance) ||
                max_a[axis] < candidate)
                continue;
            if (!found || candidate < boundary) {
                boundary = candidate;
                found = 1;
            }
        } else {
            const fix16_t candidate = tri_max[axis];
            if (previous_min[axis] < fix16_sub(candidate, tolerance) ||
                min_a[axis] > candidate)
                continue;
            if (!found || candidate > boundary) {
                boundary = candidate;
                found = 1;
            }
        }
    }
    if (!found)
        return 0;
    if (disp > 0)
        active->position[axis] = fix16_add(active->position[axis],
                                           fix16_sub(boundary, max_a[axis]));
    else
        active->position[axis] = fix16_add(active->position[axis],
                                           fix16_sub(boundary, min_a[axis]));
    get_world_aabb(active, min_a, max_a);
    return 1;
}

#endif

static int resolve_nonconvex_mesh_axis(ps1_entity* active,
                                       const ps1_entity* mesh_entity,
                                       int axis,
                                       fix16_t disp,
                                       const fix16_t previous_min[3],
                                       const fix16_t previous_max[3],
                                       fix16_t min_a[3],
                                       fix16_t max_a[3]) {
    const fix16_t (*world_vertices)[3] = 0;
    const ps1_mesh* mesh = prepare_nonconvex_mesh(
        mesh_entity, &world_vertices);
    fix16_t boundary = 0;
    const fix16_t tolerance = FIX16_ONE / 100;
    int64_t min_u;
    int64_t max_u;
    int64_t min_v;
    int64_t max_v;
    int u_axis, v_axis;
    int found = 0;
    if (!mesh || axis < 0 || axis > 2 || disp == 0)
        return 0;
    if (axis == 0) {
        u_axis = 1;
        v_axis = 2;
    } else if (axis == 1) {
        u_axis = 0;
        v_axis = 2;
    } else {
        u_axis = 0;
        v_axis = 1;
    }
    min_u = min_a[u_axis] >> 8;
    max_u = max_a[u_axis] >> 8;
    min_v = min_a[v_axis] >> 8;
    max_v = max_a[v_axis] >> 8;

    for (uint16_t i = 0; i < mesh->tri_count; ++i) {
        const ps1_mesh_tri* triangle = &mesh->tris[i];
        int64_t vertex[3][3];
        int64_t ab[3], ac[3], normal[3];
        ps1_projected_vertex projected[3];
        ps1_projected_vertex clipped[8];
        int clipped_count;
        if (triangle->i0 >= mesh->vert_count || triangle->i1 >= mesh->vert_count ||
            triangle->i2 >= mesh->vert_count)
            continue;
        for (int component = 0; component < 3; ++component) {
            vertex[0][component] = world_vertices[triangle->i0][component] >> 8;
            vertex[1][component] = world_vertices[triangle->i1][component] >> 8;
            vertex[2][component] = world_vertices[triangle->i2][component] >> 8;
            ab[component] = vertex[1][component] - vertex[0][component];
            ac[component] = vertex[2][component] - vertex[0][component];
        }
        /* Projected triangle AABB is much cheaper than the exact clip below
         * and removes almost every face on a large collision floor. */
        {
            int64_t tri_min_u = vertex[0][u_axis];
            int64_t tri_max_u = vertex[0][u_axis];
            int64_t tri_min_v = vertex[0][v_axis];
            int64_t tri_max_v = vertex[0][v_axis];
            for (int triangle_vertex = 1; triangle_vertex < 3;
                 ++triangle_vertex) {
                const int64_t u = vertex[triangle_vertex][u_axis];
                const int64_t v = vertex[triangle_vertex][v_axis];
                if (u < tri_min_u) tri_min_u = u;
                if (u > tri_max_u) tri_max_u = u;
                if (v < tri_min_v) tri_min_v = v;
                if (v > tri_max_v) tri_max_v = v;
            }
            if (tri_max_u < min_u || tri_min_u > max_u ||
                tri_max_v < min_v || tri_min_v > max_v)
                continue;
        }
        normal[0] = ab[1] * ac[2] - ab[2] * ac[1];
        normal[1] = ab[2] * ac[0] - ab[0] * ac[2];
        normal[2] = ab[0] * ac[1] - ab[1] * ac[0];
        if (nonconvex_triangle_dominant_axis(normal) != axis ||
            (normal[axis] > -4 && normal[axis] < 4))
            continue;
        for (int component = 0; component < 3; ++component) {
            projected[component].u = vertex[component][u_axis];
            projected[component].v = vertex[component][v_axis];
            projected[component].value = vertex[component][axis];
        }
        clipped_count = clip_projected_triangle(
            projected, min_u, max_u, min_v, max_v, clipped);
        for (int clipped_vertex = 0;
             clipped_vertex < clipped_count;
             ++clipped_vertex) {
            const fix16_t candidate =
                (fix16_t)(clipped[clipped_vertex].value * 256);
            if (disp > 0) {
                if (previous_max[axis] > fix16_add(candidate, tolerance) ||
                    max_a[axis] < candidate)
                    continue;
                if (!found || candidate < boundary) {
                    boundary = candidate;
                    found = 1;
                }
            } else {
                if (previous_min[axis] < fix16_sub(candidate, tolerance) ||
                    min_a[axis] > candidate)
                    continue;
                if (!found || candidate > boundary) {
                    boundary = candidate;
                    found = 1;
                }
            }
        }
    }
    if (!found) return 0;
    active->position[axis] = fix16_add(
        active->position[axis],
        disp > 0
            ? fix16_sub(fix16_sub(boundary, PS1_COLLISION_SKIN), max_a[axis])
            : fix16_sub(fix16_add(boundary, PS1_COLLISION_SKIN), min_a[axis]));
    if (axis == 1 && disp < 0)
        s_vertical_vel = 0;
    get_world_aabb(active, min_a, max_a);
    return 1;
}

static void rebuild_collider_cache(void) {
    const unsigned int count = ps1_scene_entity_count();
    unsigned int i;
    s_static_collider_count = 0;
    s_dynamic_collider_count = 0;
    s_static_nonconvex_vertex_used = 0;
    s_cached_scene_entity_count = count;

    for (i = 0; i < count; ++i) {
        const ps1_entity* entity = ps1_scene_entity(i);
        const ps1_entity* source = ps1_scene_source_entity(i);
        if (!entity || entity->collider_shape == -1 || entity->collider_is_trigger)
            continue;

        /* Entities without a mutable runtime copy cannot move, so their
         * world AABB is safe to calculate once. */
        if (entity == source && s_static_collider_count < PS1_STATIC_COLLIDER_MAX) {
            ps1_cached_collider* cached =
                &s_static_colliders[s_static_collider_count++];
            cached->entity_index = (uint16_t)i;
            cached->rigid_root_entity_index = entity->rigid_root_entity_index;
            cached->shape = entity->collider_shape;
            cached->world_vertex_offset = 0;
            cached->world_vertex_count = 0;
            {
                fix16_t min_bounds[3];
                fix16_t max_bounds[3];
                get_world_aabb(entity, min_bounds, max_bounds);
                cache_collider_bounds(cached, min_bounds, max_bounds);
            }
            {
                const ps1_mesh* mesh = nonconvex_mesh_data(entity);
                if (mesh && mesh->verts &&
                    mesh->vert_count <= PS1_NONCONVEX_VERTEX_MAX &&
                    s_static_nonconvex_vertex_used + mesh->vert_count <=
                        PS1_STATIC_NONCONVEX_VERTEX_TOTAL) {
                    fix16_t right[3], up[3], forward[3];
                    uint16_t vertex_index;
                    cached->world_vertex_offset =
                        s_static_nonconvex_vertex_used;
                    cached->world_vertex_count = mesh->vert_count;
                    collider_rotation_axes(entity, right, up, forward);
                    for (vertex_index = 0; vertex_index < mesh->vert_count;
                         ++vertex_index) {
                        mesh_vertex_world(
                            entity, mesh, &mesh->verts[vertex_index],
                            right, up, forward,
                            s_static_nonconvex_world_vertices
                                [s_static_nonconvex_vertex_used + vertex_index]);
                    }
                    s_static_nonconvex_vertex_used =
                        (uint16_t)(s_static_nonconvex_vertex_used +
                                   mesh->vert_count);
                }
            }
        } else if (s_dynamic_collider_count < PS1_DYNAMIC_COLLIDER_MAX) {
            s_dynamic_colliders[s_dynamic_collider_count++] = (uint16_t)i;
        }
    }
}

static void ensure_collider_cache(void) {
    if (s_cached_scene_entity_count != ps1_scene_entity_count())
        rebuild_collider_cache();
}

static int collider_is_related(const ps1_entity* active,
                               unsigned int other_index,
                               int16_t other_root_index) {
    return other_root_index == (int16_t)s_active_entity ||
           (int16_t)other_index == active->rigid_root_entity_index ||
           (active->rigid_root_entity_index >= 0 &&
            other_root_index == active->rigid_root_entity_index);
}

int ps1_physics_camera_sweep(unsigned int target_entity_index,
                             unsigned int camera_entity_index,
                             const fix16_t from[3],
                             const fix16_t desired[3],
                             fix16_t radius,
                             fix16_t out_position[3]) {
    const ps1_entity* target = ps1_scene_entity(target_entity_index);
    const fix16_t clearance = fix16_add(
        radius < 0 ? fix16_neg(radius) : radius, PS1_COLLISION_SKIN);
    fix16_t nearest = FIX16_ONE;
    int found = 0;
    unsigned int i;

    if (!target || !from || !desired || !out_position)
        return 0;
    ensure_collider_cache();

    for (i = 0; i < s_static_collider_count; ++i) {
        const ps1_cached_collider* cached = &s_static_colliders[i];
        const ps1_entity* collider;
        fix16_t min_bounds[3], max_bounds[3], hit_t;
        if (camera_collider_is_related(target, target_entity_index,
                                       camera_entity_index,
                                       cached->entity_index,
                                       cached->rigid_root_entity_index))
            continue;
        collider = ps1_scene_entity(cached->entity_index);
        if (!collider || collider->collider_is_trigger)
            continue;
        cached_collider_bounds(cached, min_bounds, max_bounds);
        if (camera_collider_hit(collider, from, desired, min_bounds, max_bounds,
                                clearance, &hit_t) &&
            (!found || hit_t < nearest)) {
            nearest = hit_t;
            found = 1;
        }
    }

    for (i = 0; i < s_dynamic_collider_count; ++i) {
        const unsigned int collider_index = s_dynamic_colliders[i];
        const ps1_entity* collider = ps1_scene_entity(collider_index);
        fix16_t min_bounds[3], max_bounds[3], hit_t;
        if (!collider || collider->collider_shape == -1 ||
            collider->collider_is_trigger ||
            camera_collider_is_related(target, target_entity_index,
                                       camera_entity_index, collider_index,
                                       collider->rigid_root_entity_index))
            continue;
        get_world_aabb(collider, min_bounds, max_bounds);
        if (camera_collider_hit(collider, from, desired, min_bounds, max_bounds,
                                clearance, &hit_t) &&
            (!found || hit_t < nearest)) {
            nearest = hit_t;
            found = 1;
        }
    }

    for (i = 0; i < 3; ++i) {
        out_position[i] = found
            ? fix16_add(from[i], fix16_mul(
                  fix16_sub(desired[i], from[i]), nearest))
            : desired[i];
    }
    return found;
}

static void follow_nonconvex_ground(ps1_entity* active, fix16_t max_step) {
    fix16_t min_a[3], max_a[3];
    fix16_t best = (fix16_t)0x80000000;
    const fix16_t tolerance = FIX16_ONE / 20;
    int found = 0;
    get_world_aabb(active, min_a, max_a);

    for (unsigned int i = 0; i < s_static_collider_count; ++i) {
        const ps1_cached_collider* cached = &s_static_colliders[i];
        const ps1_entity* mesh_entity;
        fix16_t min_b[3], max_b[3];
        fix16_t surface;
        if (cached->entity_index == s_active_entity ||
            collider_is_related(active, cached->entity_index,
                                cached->rigid_root_entity_index))
            continue;
        mesh_entity = ps1_scene_entity(cached->entity_index);
        if (!nonconvex_mesh_data(mesh_entity))
            continue;
        cached_collider_bounds(cached, min_b, max_b);
        if (!bounds_overlap_xz(min_a, max_a, min_b, max_b) ||
            min_b[1] > fix16_add(min_a[1], max_step) ||
            max_b[1] < fix16_sub(min_a[1], max_step))
            continue;
        if (nonconvex_mesh_floor_height_footprint(
                mesh_entity, min_a, max_a,
                fix16_add(min_a[1], max_step), &surface) &&
            surface >= fix16_sub(min_a[1], max_step) &&
            (!found || surface > best)) {
            best = surface;
            found = 1;
        }
    }
    for (unsigned int i = 0; i < s_dynamic_collider_count; ++i) {
        const unsigned int entity_index = s_dynamic_colliders[i];
        const ps1_entity* mesh_entity;
        fix16_t min_b[3], max_b[3];
        fix16_t surface;
        if (entity_index == s_active_entity)
            continue;
        mesh_entity = ps1_scene_entity(entity_index);
        if (!mesh_entity ||
            collider_is_related(active, entity_index,
                                mesh_entity->rigid_root_entity_index) ||
            !nonconvex_mesh_data(mesh_entity))
            continue;
        get_world_aabb(mesh_entity, min_b, max_b);
        if (!bounds_overlap_xz(min_a, max_a, min_b, max_b) ||
            min_b[1] > fix16_add(min_a[1], max_step) ||
            max_b[1] < fix16_sub(min_a[1], max_step))
            continue;
        if (nonconvex_mesh_floor_height_footprint(
                mesh_entity, min_a, max_a,
                fix16_add(min_a[1], max_step), &surface) &&
            surface >= fix16_sub(min_a[1], max_step) &&
            (!found || surface > best)) {
            best = surface;
            found = 1;
        }
    }
    if (found && fix16_abs(fix16_sub(best, min_a[1])) > tolerance / 4) {
        active->position[1] = fix16_add(
            active->position[1], fix16_sub(best, min_a[1]));
        s_vertical_vel = 0;
    }
}

static int resolve_axis_collision(ps1_entity* entity,
                                  int axis,
                                  fix16_t disp,
                                  fix16_t axis_start,
                                  const fix16_t previous_min[3],
                                  const fix16_t previous_max[3],
                                  fix16_t min_a[3],
                                  fix16_t max_a[3],
                                  const fix16_t min_b[3],
                                  const fix16_t max_b[3]) {
    fix16_t overlap;
    const int u_axis = axis == 0 ? 1 : 0;
    const int v_axis = axis == 2 ? 1 : 2;
    const int transverse_overlap =
        min_a[u_axis] < max_b[u_axis] && max_a[u_axis] > min_b[u_axis] &&
        min_a[v_axis] < max_b[v_axis] && max_a[v_axis] > min_b[v_axis];

    if (!transverse_overlap)
        return 0;

    /* Swept AABB contact.  Testing whether the leading face crossed a
     * boundary catches thin walls and floors even when the final AABBs no
     * longer overlap (the usual low-frame-rate tunnelling failure). */
    if (disp > 0 && previous_max[axis] <= min_b[axis] &&
        max_a[axis] >= min_b[axis]) {
        entity->position[axis] = fix16_add(
            entity->position[axis],
            fix16_sub(fix16_sub(min_b[axis], PS1_COLLISION_SKIN),
                      max_a[axis]));
        if (axis == 1)
            s_vertical_vel = 0;
        get_world_aabb(entity, min_a, max_a);
        return 0;
    }
    if (disp < 0 && previous_min[axis] >= max_b[axis] &&
        min_a[axis] <= max_b[axis]) {
        entity->position[axis] = fix16_add(
            entity->position[axis],
            fix16_sub(fix16_add(max_b[axis], PS1_COLLISION_SKIN),
                      min_a[axis]));
        if (axis == 1)
            s_vertical_vel = 0;
        get_world_aabb(entity, min_a, max_a);
        return 0;
    }

    if (!aabb_overlaps(min_a, max_a, min_b, max_b))
        return 0;

    if (aabb_overlaps(previous_min, previous_max, min_b, max_b)) {
        const fix16_t previous_depth =
            fix16_sub(min_fix16(previous_max[axis], max_b[axis]),
                      max_fix16(previous_min[axis], min_b[axis]));
        const fix16_t current_depth =
            fix16_sub(min_fix16(max_a[axis], max_b[axis]),
                      max_fix16(min_a[axis], min_b[axis]));

        if (current_depth <= previous_depth)
            return 0;

        entity->position[axis] = axis_start;
        if (axis == 1 && s_vertical_vel < 0)
            s_vertical_vel = 0;
        return 1;
    }

    if (disp > 0) {
        overlap = fix16_add(fix16_sub(max_a[axis], min_b[axis]),
                            PS1_COLLISION_SKIN);
        if (overlap > disp) overlap = disp;
        entity->position[axis] = fix16_sub(entity->position[axis], overlap);
    } else {
        overlap = fix16_add(fix16_sub(max_b[axis], min_a[axis]),
                            PS1_COLLISION_SKIN);
        if (overlap > fix16_abs(disp)) overlap = fix16_abs(disp);
        entity->position[axis] = fix16_add(entity->position[axis], overlap);
    }

    if (axis == 1 && s_vertical_vel < 0)
        s_vertical_vel = 0;
    get_world_aabb(entity, min_a, max_a);
    return 0;
}

static int depenetrate_against_bounds(ps1_entity* entity,
                                      const fix16_t min_b[3],
                                      const fix16_t max_b[3]) {
    fix16_t min_a[3], max_a[3];
    fix16_t best_distance = (fix16_t)0x7fffffff;
    fix16_t best_delta = 0;
    int best_axis = -1;

    get_world_aabb(entity, min_a, max_a);
    if (!aabb_overlaps(min_a, max_a, min_b, max_b))
        return 0;

    /* Minimum translation vector (MTV), the same recovery principle used by
     * modern character controllers.  It repairs spawn/rounding penetration
     * before the continuous sweep is applied. */
    for (int axis = 0; axis < 3; ++axis) {
        const fix16_t toward_negative = fix16_sub(max_a[axis], min_b[axis]);
        const fix16_t toward_positive = fix16_sub(max_b[axis], min_a[axis]);
        fix16_t distance;
        fix16_t delta;
        if (toward_negative < toward_positive) {
            distance = toward_negative;
            delta = fix16_neg(fix16_add(distance, PS1_COLLISION_SKIN));
        } else {
            distance = toward_positive;
            delta = fix16_add(distance, PS1_COLLISION_SKIN);
        }
        if (distance < best_distance) {
            best_distance = distance;
            best_delta = delta;
            best_axis = axis;
        }
    }

    if (best_axis < 0)
        return 0;
    entity->position[best_axis] =
        fix16_add(entity->position[best_axis], best_delta);
    if (best_axis == 1)
        s_vertical_vel = 0;
    return 1;
}

static void recover_initial_penetration(ps1_entity* entity,
                                        uint16_t candidate_count,
                                        int candidates_overflow) {
    for (int iteration = 0;
         iteration < PS1_DEPENETRATION_ITERATIONS;
         ++iteration) {
        int corrected = 0;
        const unsigned int static_test_count =
            candidates_overflow ? (unsigned int)s_static_collider_count
                                : (unsigned int)candidate_count;

        for (unsigned int i = 0; i < static_test_count; ++i) {
            const unsigned int collider_index =
                candidates_overflow ? i : s_move_candidates[i];
            const ps1_cached_collider* other =
                &s_static_colliders[collider_index];
            const ps1_entity* other_entity;
            fix16_t min_b[3], max_b[3];
            if (other->entity_index == s_active_entity ||
                collider_is_related(entity, other->entity_index,
                                    other->rigid_root_entity_index))
                continue;
            other_entity = ps1_scene_entity(other->entity_index);
            if (nonconvex_mesh_data(other_entity))
                continue;
            cached_collider_bounds(other, min_b, max_b);
            if (depenetrate_against_bounds(entity, min_b, max_b)) {
                corrected = 1;
                break;
            }
        }

        for (unsigned int i = 0;
             !corrected && i < s_dynamic_collider_count;
             ++i) {
            const unsigned int other_index = s_dynamic_colliders[i];
            const ps1_entity* other;
            fix16_t min_b[3], max_b[3];
            if (other_index == s_active_entity)
                continue;
            other = ps1_scene_entity(other_index);
            if (!other || other->collider_shape == -1 ||
                other->collider_is_trigger || nonconvex_mesh_data(other) ||
                collider_is_related(entity, other_index,
                                    other->rigid_root_entity_index))
                continue;
            get_world_aabb(other, min_b, max_b);
            if (depenetrate_against_bounds(entity, min_b, max_b)) {
                corrected = 1;
                break;
            }
        }

        if (!corrected)
            break;
    }
}

static int ground_within_distance(const ps1_entity* e, fix16_t distance) {
    fix16_t min_a[3], max_a[3];
    if (!e || distance <= 0)
        return 0;
    get_world_aabb(e, min_a, max_a);

    for (unsigned int i = 0; i < s_static_collider_count; ++i) {
        const ps1_cached_collider* cached = &s_static_colliders[i];
        const ps1_entity* other;
        fix16_t min_b[3], max_b[3];
        fix16_t gap;
        if (cached->entity_index == s_active_entity ||
            collider_is_related(e, cached->entity_index,
                                cached->rigid_root_entity_index))
            continue;
        other = ps1_scene_entity(cached->entity_index);
        cached_collider_bounds(cached, min_b, max_b);
        if (!bounds_overlap_xz(min_a, max_a, min_b, max_b) ||
            max_b[1] < fix16_sub(min_a[1], distance) ||
            min_b[1] > fix16_add(min_a[1], PS1_COLLISION_SKIN))
            continue;
        if (nonconvex_mesh_data(other)) {
            fix16_t surface;
            if (nonconvex_mesh_floor_height_footprint(
                    other, min_a, max_a,
                    fix16_add(min_a[1], PS1_COLLISION_SKIN), &surface)) {
                gap = fix16_sub(min_a[1], surface);
                if (gap >= -PS1_COLLISION_SKIN && gap <= distance)
                    return 1;
            }
            continue;
        }
        gap = fix16_sub(min_a[1], max_b[1]);
        if (gap >= -PS1_COLLISION_SKIN && gap <= distance)
            return 1;
    }

    for (unsigned int i = 0; i < s_dynamic_collider_count; ++i) {
        const unsigned int other_index = s_dynamic_colliders[i];
        const ps1_entity* other = ps1_scene_entity(other_index);
        fix16_t min_b[3], max_b[3];
        fix16_t gap;
        if (!other || other_index == s_active_entity || other->collider_is_trigger ||
            collider_is_related(e, other_index, other->rigid_root_entity_index))
            continue;
        get_world_aabb(other, min_b, max_b);
        if (!bounds_overlap_xz(min_a, max_a, min_b, max_b) ||
            max_b[1] < fix16_sub(min_a[1], distance) ||
            min_b[1] > fix16_add(min_a[1], PS1_COLLISION_SKIN))
            continue;
        if (nonconvex_mesh_data(other)) {
            fix16_t surface;
            if (nonconvex_mesh_floor_height_footprint(
                    other, min_a, max_a,
                    fix16_add(min_a[1], PS1_COLLISION_SKIN), &surface)) {
                gap = fix16_sub(min_a[1], surface);
                if (gap >= -PS1_COLLISION_SKIN && gap <= distance)
                    return 1;
            }
            continue;
        }
        gap = fix16_sub(min_a[1], max_b[1]);
        if (gap >= -PS1_COLLISION_SKIN && gap <= distance)
            return 1;
    }
    return 0;
}

void ps1_physics_move_kinematic(fix16_t vx, fix16_t vy, fix16_t vz, fix16_t dt) {
    ps1_entity* platform = ps1_scene_mutable_entity(s_active_entity);
    fix16_t platform_min[3], platform_max[3];
    fix16_t delta[3];
    unsigned int count;
    if (!platform)
        return;

    delta[0] = fix16_mul(vx, dt);
    delta[1] = fix16_mul(vy, dt);
    delta[2] = fix16_mul(vz, dt);
    get_world_aabb(platform, platform_min, platform_max);

    platform->position[0] = fix16_add(platform->position[0], delta[0]);
    platform->position[1] = fix16_add(platform->position[1], delta[1]);
    platform->position[2] = fix16_add(platform->position[2], delta[2]);

    /* Carry mutable colliders whose feet were supported by the platform at
     * the start of this script tick. This mirrors CharacterVirtual's moving
     * ground velocity on desktop and prevents horizontal/descending slip. */
    count = ps1_scene_entity_count();
    for (unsigned int i = 0; i < count; ++i) {
        ps1_entity* passenger;
        fix16_t passenger_min[3], passenger_max[3];
        fix16_t gap;
        if (i == s_active_entity)
            continue;
        passenger = ps1_scene_mutable_entity(i);
        if (!passenger || passenger->collider_shape == -1 ||
            passenger->collider_is_trigger ||
            collider_is_related(platform, i, passenger->rigid_root_entity_index))
            continue;
        get_world_aabb(passenger, passenger_min, passenger_max);
        if (!bounds_overlap_xz(passenger_min, passenger_max,
                               platform_min, platform_max))
            continue;
        gap = fix16_sub(passenger_min[1], platform_max[1]);
        if (gap < -PS1_COLLISION_SKIN || gap > FIX16_ONE / 8)
            continue;
        passenger->position[0] = fix16_add(passenger->position[0], delta[0]);
        passenger->position[1] = fix16_add(passenger->position[1], delta[1]);
        passenger->position[2] = fix16_add(passenger->position[2], delta[2]);
    }
    s_grounded_cache_valid = 0;
}

void ps1_physics_move(fix16_t vx, fix16_t vy, fix16_t vz, fix16_t dt) {
    ps1_entity* e = ps1_scene_mutable_entity(s_active_entity);
    uint16_t move_candidate_count = 0;
    int move_candidates_overflow = 0;
    int follow_ground = 0;
    const fix16_t displacement[3] = {
        fix16_mul(vx, dt),
        fix16_mul(vy, dt),
        fix16_mul(vz, dt)
    };
    if (!e) return;

    ensure_collider_cache();
    s_vertical_vel = vy;
    if (e->collider_shape != -1 && !e->collider_is_trigger && vy <= 0)
        follow_ground = ps1_physics_is_grounded();

    if (e->collider_shape != -1 && !e->collider_is_trigger) {
        fix16_t sweep_min[3], sweep_max[3];
        int16_t sweep_min_q6[3], sweep_max_q6[3];
        int broadphase_axis;
        unsigned int collider_index;

        get_world_aabb(e, sweep_min, sweep_max);
        for (broadphase_axis = 0; broadphase_axis < 3; ++broadphase_axis) {
            if (displacement[broadphase_axis] < 0)
                sweep_min[broadphase_axis] =
                    fix16_add(sweep_min[broadphase_axis],
                              displacement[broadphase_axis]);
            else
                sweep_max[broadphase_axis] =
                    fix16_add(sweep_max[broadphase_axis],
                              displacement[broadphase_axis]);
            sweep_min_q6[broadphase_axis] =
                collider_bound_q6(sweep_min[broadphase_axis], 0);
            sweep_max_q6[broadphase_axis] =
                collider_bound_q6(sweep_max[broadphase_axis], 1);
        }

        for (collider_index = 0;
             collider_index < s_static_collider_count; ++collider_index) {
            const ps1_cached_collider* other =
                &s_static_colliders[collider_index];
            if (other->entity_index == s_active_entity)
                continue;
            if (collider_is_related(e, other->entity_index,
                                    other->rigid_root_entity_index))
                continue;
            if (!cached_q6_overlaps(other, sweep_min_q6, sweep_max_q6))
                continue;
            if (move_candidate_count >= PS1_MOVE_CANDIDATE_MAX) {
                move_candidates_overflow = 1;
                break;
            }
            s_move_candidates[move_candidate_count++] =
                (uint16_t)collider_index;
        }
    }

    /* Resolve both horizontal axes before vertical support. Otherwise the Y
     * test uses the old Z position and the final Z move can enter a ramp after
     * its floor triangle has already been tested for this frame. */
    for (int axis_step = 0; axis_step < 3; ++axis_step) {
        const int axis = axis_step == 0 ? 0 : (axis_step == 1 ? 2 : 1);
        fix16_t disp = 0;
        fix16_t axis_start;
        fix16_t previous_min[3], previous_max[3];
        disp = displacement[axis];

        if (disp == 0) continue;

        axis_start = e->position[axis];
        get_world_aabb(e, previous_min, previous_max);
        e->position[axis] = fix16_add(e->position[axis], disp);

        if (e->collider_shape != -1 && !e->collider_is_trigger) {
            fix16_t min_a[3], max_a[3];
            int stop_axis = 0;
            get_world_aabb(e, min_a, max_a);

            const unsigned int static_test_count =
                move_candidates_overflow
                    ? (unsigned int)s_static_collider_count
                    : (unsigned int)move_candidate_count;
            for (unsigned int i = 0; i < static_test_count; ++i) {
                const unsigned int collider_index =
                    move_candidates_overflow ? i : s_move_candidates[i];
                const ps1_cached_collider* other =
                    &s_static_colliders[collider_index];
                fix16_t min_b[3], max_b[3];
                if (other->entity_index == s_active_entity)
                    continue;
                if (collider_is_related(e, other->entity_index,
                                        other->rigid_root_entity_index))
                    continue;
                {
                    const ps1_entity* mesh_entity =
                        ps1_scene_entity(other->entity_index);
                    if (nonconvex_mesh_data(mesh_entity)) {
                        if (resolve_nonconvex_mesh_axis(
                                e, mesh_entity, axis, disp,
                                previous_min, previous_max, min_a, max_a))
                            stop_axis = 1;
                        /* Never collide against the whole mesh AABB: it fills
                         * concave holes and can contain the player at spawn. */
                        if (stop_axis)
                            break;
                        continue;
                    }
                }
                if (axis != 1 && other->shape == 3 &&
                    other->min_q6[1] == other->max_q6[1])
                    continue;
                cached_collider_bounds(other, min_b, max_b);
                if (resolve_axis_collision(e, axis, disp, axis_start,
                                           previous_min, previous_max,
                                           min_a, max_a, min_b, max_b)) {
                    stop_axis = 1;
                    break;
                }
            }

            for (unsigned int i = 0;
                 !stop_axis && i < s_dynamic_collider_count; ++i) {
                const unsigned int other_index = s_dynamic_colliders[i];
                const ps1_entity* other;
                fix16_t min_b[3], max_b[3];
                if (other_index == s_active_entity)
                    continue;
                other = ps1_scene_entity(other_index);
                if (!other || other->collider_shape == -1 || other->collider_is_trigger)
                    continue;
                if (collider_is_related(e, other_index,
                                        other->rigid_root_entity_index))
                    continue;
                if (nonconvex_mesh_data(other)) {
                    if (resolve_nonconvex_mesh_axis(
                            e, other, axis, disp,
                            previous_min, previous_max, min_a, max_a))
                        stop_axis = 1;
                    if (stop_axis)
                        break;
                    continue;
                }
                if (axis != 1 && other->collider_shape == 3 &&
                    other->collider_half_extents[1] == 0)
                    continue;
                get_world_aabb(other, min_b, max_b);
                if (resolve_axis_collision(e, axis, disp, axis_start,
                                           previous_min, previous_max,
                                           min_a, max_a, min_b, max_b))
                    stop_axis = 1;
            }
        }
    }

    if (e->collider_shape != -1 && !e->collider_is_trigger)
        recover_initial_penetration(e, move_candidate_count,
                                    move_candidates_overflow);

    if (follow_ground &&
        (displacement[0] != 0 || displacement[2] != 0)) {
        fix16_t max_step = fix16_add(
            fix16_add(fix16_abs(displacement[0]),
                      fix16_abs(displacement[2])),
            FIX16_ONE / 20);
        if (max_step > FIX16_ONE / 2)
            max_step = FIX16_ONE / 2;
        follow_nonconvex_ground(e, max_step);
    }

    /* The active entity may have moved or been depenetrated. A later query
     * in the same frame must observe the new position. */
    s_grounded_cache_valid = 0;

}

int ps1_physics_is_grounded_within(fix16_t distance) {
    const ps1_entity* e = ps1_scene_entity(s_active_entity);
    if (!e || e->collider_shape == -1 || e->collider_is_trigger)
        return 0;
    ensure_collider_cache();
    return ground_within_distance(e, distance < 0 ? fix16_neg(distance) : distance);
}

int ps1_physics_is_grounded(void) {
    const ps1_entity* e = ps1_scene_entity(s_active_entity);
    ps1_entity probe;
    fix16_t min_a[3], max_a[3];
    unsigned int count;
    if (!e) return 0;

    if (s_grounded_cache_valid && s_grounded_cache_entity == s_active_entity)
        return s_grounded_cache_result;

    if (e->collider_shape == -1 || e->collider_is_trigger)
        return 0;

    ensure_collider_cache();

    /* Probe a small distance downward. Levels may be authored above or below
     * world Y=0, so grounding must come from actual scene collision. */
    probe = *e;
    probe.position[1] = fix16_sub(probe.position[1], FIX16_ONE / 50);
    get_world_aabb(&probe, min_a, max_a);
    for (unsigned int i = 0; i < s_static_collider_count; ++i) {
        const ps1_cached_collider* other = &s_static_colliders[i];
        fix16_t min_b[3], max_b[3];
        if (other->entity_index == s_active_entity)
            continue;
        if (collider_is_related(e, other->entity_index,
                                other->rigid_root_entity_index))
            continue;
        {
            const ps1_entity* mesh_entity =
                ps1_scene_entity(other->entity_index);
            cached_collider_bounds(other, min_b, max_b);
            if (!bounds_overlap_xz(min_a, max_a, min_b, max_b) ||
                max_b[1] < fix16_sub(min_a[1], FIX16_ONE / 10) ||
                min_b[1] > fix16_add(max_a[1], FIX16_ONE / 10))
                continue;
            if (nonconvex_mesh_data(mesh_entity)) {
                fix16_t surface;
                if (nonconvex_mesh_floor_height_footprint(
                        mesh_entity, min_a, max_a,
                        fix16_add(min_a[1], FIX16_ONE / 10), &surface) &&
                    fix16_abs(fix16_sub(min_a[1], surface)) <= FIX16_ONE / 10) {
                    s_grounded_cache_entity = s_active_entity;
                    s_grounded_cache_result = 1;
                    s_grounded_cache_valid = 1;
                    return 1;
                }
                continue;
            }
        }
        if (aabb_overlaps(min_a, max_a, min_b, max_b)) {
            s_grounded_cache_entity = s_active_entity;
            s_grounded_cache_result = 1;
            s_grounded_cache_valid = 1;
            return 1;
        }
    }

    count = s_dynamic_collider_count;
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned int other_index = s_dynamic_colliders[i];
        const ps1_entity* other;
        fix16_t min_b[3], max_b[3];
        if (other_index == s_active_entity)
            continue;
        other = ps1_scene_entity(other_index);
        if (!other || other->collider_shape == -1 || other->collider_is_trigger)
            continue;
        if (collider_is_related(e, other_index,
                                other->rigid_root_entity_index))
            continue;
        get_world_aabb(other, min_b, max_b);
        if (!bounds_overlap_xz(min_a, max_a, min_b, max_b) ||
            max_b[1] < fix16_sub(min_a[1], FIX16_ONE / 10) ||
            min_b[1] > fix16_add(max_a[1], FIX16_ONE / 10))
            continue;
        if (nonconvex_mesh_data(other)) {
            fix16_t surface;
            if (nonconvex_mesh_floor_height_footprint(
                    other, min_a, max_a,
                    fix16_add(min_a[1], FIX16_ONE / 10), &surface) &&
                fix16_abs(fix16_sub(min_a[1], surface)) <= FIX16_ONE / 10) {
                s_grounded_cache_entity = s_active_entity;
                s_grounded_cache_result = 1;
                s_grounded_cache_valid = 1;
                return 1;
            }
            continue;
        }
        if (aabb_overlaps(min_a, max_a, min_b, max_b)) {
            s_grounded_cache_entity = s_active_entity;
            s_grounded_cache_result = 1;
            s_grounded_cache_valid = 1;
            return 1;
        }
    }
    s_grounded_cache_entity = s_active_entity;
    s_grounded_cache_result = 0;
    s_grounded_cache_valid = 1;
    return 0;
}

int16_t ps1_physics_get_collider_shape(unsigned int entity_index) {
    const ps1_entity* e = ps1_scene_entity(entity_index);
    if (!e) return -1;
    return e->collider_shape;
}

void ps1_physics_get_collider_center(unsigned int entity_index, fix16_t out_center[3]) {
    const ps1_entity* e = ps1_scene_entity(entity_index);
    if (e) {
        out_center[0] = e->collider_center[0];
        out_center[1] = e->collider_center[1];
        out_center[2] = e->collider_center[2];
    } else {
        out_center[0] = out_center[1] = out_center[2] = 0;
    }
}

void ps1_physics_get_collider_half_extents(unsigned int entity_index, fix16_t out_half_extents[3]) {
    const ps1_entity* e = ps1_scene_entity(entity_index);
    if (e) {
        out_half_extents[0] = e->collider_half_extents[0];
        out_half_extents[1] = e->collider_half_extents[1];
        out_half_extents[2] = e->collider_half_extents[2];
    } else {
        out_half_extents[0] = out_half_extents[1] = out_half_extents[2] = 0;
    }
}

fix16_t ps1_physics_get_collider_radius(unsigned int entity_index) {
    const ps1_entity* e = ps1_scene_entity(entity_index);
    if (!e) return 0;
    return e->collider_radius;
}

fix16_t ps1_physics_get_collider_capsule_height(unsigned int entity_index) {
    const ps1_entity* e = ps1_scene_entity(entity_index);
    if (!e) return 0;
    return e->collider_capsule_height;
}

int ps1_physics_is_trigger(unsigned int entity_index) {
    const ps1_entity* e = ps1_scene_entity(entity_index);
    if (!e) return 0;
    return e->collider_is_trigger;
}
