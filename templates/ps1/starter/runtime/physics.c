#include "physics.h"
#include "scene.h"

static unsigned int s_active_entity = 0xFFFFu;
static fix16_t      s_vertical_vel = 0;

#define PS1_STATIC_COLLIDER_MAX 1536
#define PS1_DYNAMIC_COLLIDER_MAX 128
#define PS1_MOVE_CANDIDATE_MAX 256

typedef struct {
    uint16_t entity_index;
    int16_t  rigid_root_entity_index;
    int16_t  shape;
    int16_t  min_q6[3];
    int16_t  max_q6[3];
} ps1_cached_collider;

static ps1_cached_collider s_static_colliders[PS1_STATIC_COLLIDER_MAX];
static uint16_t s_dynamic_colliders[PS1_DYNAMIC_COLLIDER_MAX];
static uint16_t s_move_candidates[PS1_MOVE_CANDIDATE_MAX];
static uint16_t s_static_collider_count = 0;
static uint16_t s_dynamic_collider_count = 0;
static unsigned int s_cached_scene_entity_count = 0;

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
    forward[0] = fix16_neg(fix16_mul(sy, cp));
    forward[1] = sp;
    forward[2] = fix16_neg(fix16_mul(cy, cp));
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
        entity->mesh != PS1_MESH_CUSTOM || entity->mesh_index == 0 ||
        entity->mesh_index > g_ps1_mesh_count)
        return 0;
    return &g_ps1_meshes[entity->mesh_index - 1u];
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

/* Return the highest triangle surface under X/Z without allocating a cooked
 * collider. This keeps concave ProModeler floors and ramps usable on PS1. */
static int nonconvex_mesh_floor_height(const ps1_entity* entity,
                                       fix16_t point_x,
                                       fix16_t point_z,
                                       fix16_t max_surface,
                                       fix16_t* out_height) {
    const ps1_mesh* mesh = nonconvex_mesh_data(entity);
    fix16_t best = (fix16_t)0x80000000;
    fix16_t right[3], up[3], forward[3];
    int found = 0;
    if (!mesh || !mesh->verts || !mesh->tris)
        return 0;
    collider_rotation_axes(entity, right, up, forward);

    for (uint16_t i = 0; i < mesh->tri_count; ++i) {
        const ps1_mesh_tri* triangle = &mesh->tris[i];
        fix16_t world[3][3];
        int64_t x0, x1, x2, z0, z1, z2, px, pz;
        int64_t denominator, w0, w1, w2;
        fix16_t height;
        if (triangle->i0 >= mesh->vert_count ||
            triangle->i1 >= mesh->vert_count ||
            triangle->i2 >= mesh->vert_count)
            continue;
        mesh_vertex_world(entity, mesh, &mesh->verts[triangle->i0],
                          right, up, forward, world[0]);
        mesh_vertex_world(entity, mesh, &mesh->verts[triangle->i1],
                          right, up, forward, world[1]);
        mesh_vertex_world(entity, mesh, &mesh->verts[triangle->i2],
                          right, up, forward, world[2]);

        /* Q8 X/Z keeps barycentric products inside int64 on large levels. */
        x0 = world[0][0] >> 8; x1 = world[1][0] >> 8; x2 = world[2][0] >> 8;
        z0 = world[0][2] >> 8; z1 = world[1][2] >> 8; z2 = world[2][2] >> 8;
        px = point_x >> 8; pz = point_z >> 8;
        if (px < (x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2)) ||
            px > (x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2)) ||
            pz < (z0 < z1 ? (z0 < z2 ? z0 : z2) : (z1 < z2 ? z1 : z2)) ||
            pz > (z0 > z1 ? (z0 > z2 ? z0 : z2) : (z1 > z2 ? z1 : z2)))
            continue;
        denominator = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        if (denominator > -4 && denominator < 4)
            continue;
        w0 = (z1 - z2) * (px - x2) + (x2 - x1) * (pz - z2);
        w1 = (z2 - z0) * (px - x2) + (x0 - x2) * (pz - z2);
        w2 = denominator - w0 - w1;
        if (denominator > 0) {
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
        } else if (w0 > 0 || w1 > 0 || w2 > 0) {
            continue;
        }
        height = (fix16_t)((w0 * world[0][1] +
                            w1 * world[1][1] +
                            w2 * world[2][1]) / denominator);
        if (height <= max_surface && (!found || height > best)) {
            best = height;
            found = 1;
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
    const fix16_t point_x = fix16_div(fix16_add(min_a[0], max_a[0]), FIX16_FROM_INT(2));
    const fix16_t point_z = fix16_div(fix16_add(min_a[2], max_a[2]), FIX16_FROM_INT(2));
    fix16_t surface;
    if (disp >= 0 || !nonconvex_mesh_floor_height(
            floor_entity, point_x, point_z,
            fix16_add(previous_min[1], tolerance), &surface))
        return 0;
    if (previous_min[1] < fix16_sub(surface, tolerance) ||
        min_a[1] > fix16_add(surface, tolerance))
        return 0;
    active->position[1] = fix16_add(active->position[1],
                                    fix16_sub(surface, min_a[1]));
    s_vertical_vel = 0;
    get_world_aabb(active, min_a, max_a);
    return 1;
}

static void rebuild_collider_cache(void) {
    const unsigned int count = ps1_scene_entity_count();
    unsigned int i;
    s_static_collider_count = 0;
    s_dynamic_collider_count = 0;
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
            {
                fix16_t min_bounds[3];
                fix16_t max_bounds[3];
                get_world_aabb(entity, min_bounds, max_bounds);
                cache_collider_bounds(cached, min_bounds, max_bounds);
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
        overlap = fix16_sub(max_a[axis], min_b[axis]);
        if (overlap > disp) overlap = disp;
        entity->position[axis] = fix16_sub(entity->position[axis], overlap);
    } else {
        overlap = fix16_sub(max_b[axis], min_a[axis]);
        if (overlap > fix16_abs(disp)) overlap = fix16_abs(disp);
        entity->position[axis] = fix16_add(entity->position[axis], overlap);
    }

    if (axis == 1 && s_vertical_vel < 0)
        s_vertical_vel = 0;
    get_world_aabb(entity, min_a, max_a);
    return 0;
}

void ps1_physics_move(fix16_t vx, fix16_t vy, fix16_t vz, fix16_t dt) {
    ps1_entity* e = ps1_scene_mutable_entity(s_active_entity);
    uint16_t move_candidate_count = 0;
    int move_candidates_overflow = 0;
    if (!e) return;

    ensure_collider_cache();
    s_vertical_vel = vy;

    if (e->collider_shape != -1 && !e->collider_is_trigger) {
        fix16_t sweep_min[3], sweep_max[3];
        int16_t sweep_min_q6[3], sweep_max_q6[3];
        const fix16_t displacement[3] = {
            fix16_mul(vx, dt),
            fix16_mul(vy, dt),
            fix16_mul(vz, dt)
        };
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

    for (int axis = 0; axis < 3; ++axis) {
        fix16_t disp = 0;
        fix16_t axis_start;
        fix16_t previous_min[3], previous_max[3];
        if (axis == 0) disp = fix16_mul(vx, dt);
        else if (axis == 1) disp = fix16_mul(vy, dt);
        else disp = fix16_mul(vz, dt);

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
                        if (axis == 1 && resolve_nonconvex_mesh_floor(
                                e, mesh_entity, disp, previous_min, min_a, max_a))
                            stop_axis = 1;
                        /* A concave mesh cannot use its whole bounding box:
                         * that fills holes and often contains the player. */
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
                    if (axis == 1 && resolve_nonconvex_mesh_floor(
                            e, other, disp, previous_min, min_a, max_a))
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

}

int ps1_physics_is_grounded(void) {
    const ps1_entity* e = ps1_scene_entity(s_active_entity);
    ps1_entity probe;
    fix16_t min_a[3], max_a[3];
    unsigned int count;
    if (!e) return 0;

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
            if (nonconvex_mesh_data(mesh_entity)) {
                const fix16_t point_x = fix16_div(
                    fix16_add(min_a[0], max_a[0]), FIX16_FROM_INT(2));
                const fix16_t point_z = fix16_div(
                    fix16_add(min_a[2], max_a[2]), FIX16_FROM_INT(2));
                fix16_t surface;
                if (nonconvex_mesh_floor_height(
                        mesh_entity, point_x, point_z,
                        fix16_add(min_a[1], FIX16_ONE / 10), &surface) &&
                    fix16_abs(fix16_sub(min_a[1], surface)) <= FIX16_ONE / 10)
                    return 1;
                continue;
            }
        }
        cached_collider_bounds(other, min_b, max_b);
        if (aabb_overlaps(min_a, max_a, min_b, max_b))
            return 1;
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
        if (nonconvex_mesh_data(other)) {
            const fix16_t point_x = fix16_div(
                fix16_add(min_a[0], max_a[0]), FIX16_FROM_INT(2));
            const fix16_t point_z = fix16_div(
                fix16_add(min_a[2], max_a[2]), FIX16_FROM_INT(2));
            fix16_t surface;
            if (nonconvex_mesh_floor_height(
                    other, point_x, point_z,
                    fix16_add(min_a[1], FIX16_ONE / 10), &surface) &&
                fix16_abs(fix16_sub(min_a[1], surface)) <= FIX16_ONE / 10)
                return 1;
            continue;
        }
        get_world_aabb(other, min_b, max_b);
        if (aabb_overlaps(min_a, max_a, min_b, max_b))
            return 1;
    }
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
