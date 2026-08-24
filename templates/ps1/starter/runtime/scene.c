#include "scene.h"
#include <string.h>

static ps1_entity s_runtime_entities[128];
static uint32_t s_vertex_anim_time_q16[128];
static uint32_t s_rigid_anim_time_q16[128];
static uint32_t s_transform_anim_time_q16[128];
static unsigned int s_runtime_count = 0;
static int16_t s_runtime_lookup[4096];
static const ps1_mesh* s_meshes = 0;
static unsigned int s_mesh_count = 0;
static int s_active_camera_index = -1;
static int s_pending_camera_index = -1;
static uint8_t s_pending_camera_frames = 0;

static void rotate_scaled_vec(const ps1_entity* e, const fix16_t local_scaled[3], fix16_t out[3]);
static void rotate_half_extents(const ps1_entity* e, fix16_t half[3]);
static fix16_t abs_fix(fix16_t v);
static void select_rigid_clip(ps1_entity* e, int aiming, int moving);

static fix16_t lerp_fix16(fix16_t a, fix16_t b, uint32_t t_q16) {
    return (fix16_t)((int64_t)a +
        ((((int64_t)b - (int64_t)a) * (int64_t)t_q16) >> 16));
}

static void sample_transform_animation(ps1_entity* e, uint32_t time_q16) {
    const ps1_transform_anim_key* keys;
    const ps1_transform_anim_key* previous;
    const ps1_transform_anim_key* next;
    uint32_t frame_q16;
    uint32_t end_q16;
    uint32_t t_q16 = 0;
    unsigned int i;

    if (!e || e->transform_anim_key_count == 0)
        return;
    if ((uint32_t)e->transform_anim_first_key + (uint32_t)e->transform_anim_key_count >
        g_ps1_scene.transform_anim_key_count)
        return;

    keys = g_ps1_scene.transform_anim_keys + e->transform_anim_first_key;
    frame_q16 = (uint32_t)((uint64_t)time_q16 * (uint32_t)e->transform_anim_fps);
    end_q16 = (uint32_t)e->transform_anim_length_frames << 16;
    if (e->transform_anim_loop && end_q16 > 0)
        frame_q16 %= end_q16;
    else if (frame_q16 > end_q16)
        frame_q16 = end_q16;

    previous = &keys[0];
    next = &keys[e->transform_anim_key_count - 1u];
    for (i = 0; i < e->transform_anim_key_count; ++i) {
        const uint32_t key_frame_q16 = (uint32_t)keys[i].frame << 16;
        if (key_frame_q16 <= frame_q16)
            previous = &keys[i];
        if (key_frame_q16 >= frame_q16) {
            next = &keys[i];
            break;
        }
    }

    if (next->frame > previous->frame) {
        const uint32_t previous_q16 = (uint32_t)previous->frame << 16;
        t_q16 = (frame_q16 - previous_q16) /
                (uint32_t)(next->frame - previous->frame);
        if (t_q16 > 65536u)
            t_q16 = 65536u;
    }
    for (i = 0; i < 3u; ++i) {
        e->position[i] = lerp_fix16(previous->position[i], next->position[i], t_q16);
        e->rotation[i] = lerp_fix16(previous->rotation[i], next->rotation[i], t_q16);
        e->scale[i] = lerp_fix16(previous->scale[i], next->scale[i], t_q16);
    }
}

static void get_entity_aabb(const ps1_entity* e, fix16_t min_out[3], fix16_t max_out[3]) {
    fix16_t center[3];
    fix16_t half[3];
    const fix16_t scale_x = e ? abs_fix(e->scale[0]) : 0;
    const fix16_t scale_y = e ? abs_fix(e->scale[1]) : 0;
    const fix16_t scale_z = e ? abs_fix(e->scale[2]) : 0;

    if (!e || e->collider_shape == -1) {
        fix16_t x = e ? e->position[0] : 0;
        fix16_t y = e ? e->position[1] : 0;
        fix16_t z = e ? e->position[2] : 0;
        min_out[0] = max_out[0] = x;
        min_out[1] = max_out[1] = y;
        min_out[2] = max_out[2] = z;
        return;
    }

    center[0] = fix16_add(e->position[0], fix16_mul(e->collider_center[0], e->scale[0]));
    center[1] = fix16_add(e->position[1], fix16_mul(e->collider_center[1], e->scale[1]));
    center[2] = fix16_add(e->position[2], fix16_mul(e->collider_center[2], e->scale[2]));

    if (e->collider_shape == 0) {
        half[0] = fix16_mul(e->collider_half_extents[0], scale_x);
        half[1] = fix16_mul(e->collider_half_extents[1], scale_y);
        half[2] = fix16_mul(e->collider_half_extents[2], scale_z);
    } else if (e->collider_shape == 1) {
        fix16_t r = fix16_mul(e->collider_radius, scale_x);
        half[0] = half[1] = half[2] = r;
    } else if (e->collider_shape == 2) {
        fix16_t r = fix16_mul(e->collider_radius, scale_x);
        fix16_t h = fix16_mul(e->collider_capsule_height, scale_y);
        half[0] = half[2] = r;
        half[1] = fix16_add(r, fix16_div(h, FIX16_FROM_INT(2)));
    } else {
        half[0] = fix16_mul(e->collider_half_extents[0], scale_x);
        half[1] = fix16_mul(e->collider_half_extents[1], scale_y);
        half[2] = fix16_mul(e->collider_half_extents[2], scale_z);
    }

    {
        fix16_t local_center[3];
        fix16_t rotated_center[3];
        local_center[0] = fix16_mul(e->collider_center[0], e->scale[0]);
        local_center[1] = fix16_mul(e->collider_center[1], e->scale[1]);
        local_center[2] = fix16_mul(e->collider_center[2], e->scale[2]);
        rotate_scaled_vec(e, local_center, rotated_center);
        center[0] = fix16_add(e->position[0], rotated_center[0]);
        center[1] = fix16_add(e->position[1], rotated_center[1]);
        center[2] = fix16_add(e->position[2], rotated_center[2]);
    }
    if (e->collider_shape != 1) {
        rotate_half_extents(e, half);
    }

    min_out[0] = fix16_sub(center[0], half[0]);
    max_out[0] = fix16_add(center[0], half[0]);
    min_out[1] = fix16_sub(center[1], half[1]);
    max_out[1] = fix16_add(center[1], half[1]);
    min_out[2] = fix16_sub(center[2], half[2]);
    max_out[2] = fix16_add(center[2], half[2]);
}

static int aabb_overlap(const ps1_entity* a, const ps1_entity* b) {
    fix16_t min_a[3], max_a[3], min_b[3], max_b[3];
    get_entity_aabb(a, min_a, max_a);
    get_entity_aabb(b, min_b, max_b);
    return min_a[0] < max_b[0] && max_a[0] > min_b[0] &&
           min_a[1] < max_b[1] && max_a[1] > min_b[1] &&
           min_a[2] < max_b[2] && max_a[2] > min_b[2];
}

static fix16_t abs_fix(fix16_t v) {
    return v < 0 ? -v : v;
}

static void rotate_scaled_vec(const ps1_entity* e, const fix16_t local_scaled[3], fix16_t out[3]) {
    const fix16_t p = fix16_div(e->rotation[0], FIX16_FROM_INT(360));
    const fix16_t y = fix16_div(e->rotation[1], FIX16_FROM_INT(360));
    const fix16_t r = fix16_div(e->rotation[2], FIX16_FROM_INT(360));
    const fix16_t sp = fix16_sin(p);
    const fix16_t cp = fix16_cos(p);
    const fix16_t sy = fix16_sin(y);
    const fix16_t cy = fix16_cos(y);
    const fix16_t sr = fix16_sin(r);
    const fix16_t cr = fix16_cos(r);

    const fix16_t right_x = fix16_add(fix16_mul(cy, cr), fix16_mul(fix16_mul(sy, sp), sr));
    const fix16_t right_y = fix16_mul(cp, sr);
    const fix16_t right_z = fix16_sub(fix16_mul(fix16_mul(cy, sp), sr), fix16_mul(sy, cr));

    const fix16_t up_x = fix16_sub(fix16_mul(fix16_mul(sy, sp), cr), fix16_mul(cy, sr));
    const fix16_t up_y = fix16_mul(cp, cr);
    const fix16_t up_z = fix16_add(fix16_mul(sy, sr), fix16_mul(fix16_mul(cy, sp), cr));

    const fix16_t forward_x = fix16_neg(fix16_mul(sy, cp));
    const fix16_t forward_y = sp;
    const fix16_t forward_z = fix16_neg(fix16_mul(cy, cp));

    out[0] = fix16_add(fix16_add(fix16_mul(right_x, local_scaled[0]),
                                 fix16_mul(up_x, local_scaled[1])),
                       fix16_mul(forward_x, local_scaled[2]));
    out[1] = fix16_add(fix16_add(fix16_mul(right_y, local_scaled[0]),
                                 fix16_mul(up_y, local_scaled[1])),
                       fix16_mul(forward_y, local_scaled[2]));
    out[2] = fix16_add(fix16_add(fix16_mul(right_z, local_scaled[0]),
                                 fix16_mul(up_z, local_scaled[1])),
                       fix16_mul(forward_z, local_scaled[2]));
}

static void rotate_half_extents(const ps1_entity* e, fix16_t half[3]) {
    const fix16_t p = fix16_div(e->rotation[0], FIX16_FROM_INT(360));
    const fix16_t y = fix16_div(e->rotation[1], FIX16_FROM_INT(360));
    const fix16_t r = fix16_div(e->rotation[2], FIX16_FROM_INT(360));
    const fix16_t sp = fix16_sin(p);
    const fix16_t cp = fix16_cos(p);
    const fix16_t sy = fix16_sin(y);
    const fix16_t cy = fix16_cos(y);
    const fix16_t sr = fix16_sin(r);
    const fix16_t cr = fix16_cos(r);

    const fix16_t right_x = fix16_add(fix16_mul(cy, cr), fix16_mul(fix16_mul(sy, sp), sr));
    const fix16_t right_y = fix16_mul(cp, sr);
    const fix16_t right_z = fix16_sub(fix16_mul(fix16_mul(cy, sp), sr), fix16_mul(sy, cr));
    const fix16_t up_x = fix16_sub(fix16_mul(fix16_mul(sy, sp), cr), fix16_mul(cy, sr));
    const fix16_t up_y = fix16_mul(cp, cr);
    const fix16_t up_z = fix16_add(fix16_mul(sy, sr), fix16_mul(fix16_mul(cy, sp), cr));
    const fix16_t forward_x = fix16_neg(fix16_mul(sy, cp));
    const fix16_t forward_y = sp;
    const fix16_t forward_z = fix16_neg(fix16_mul(cy, cp));

    const fix16_t hx = half[0];
    const fix16_t hy = half[1];
    const fix16_t hz = half[2];
    half[0] = fix16_add(fix16_add(fix16_mul(abs_fix(right_x), hx), fix16_mul(abs_fix(up_x), hy)),
                        fix16_mul(abs_fix(forward_x), hz));
    half[1] = fix16_add(fix16_add(fix16_mul(abs_fix(right_y), hx), fix16_mul(abs_fix(up_y), hy)),
                        fix16_mul(abs_fix(forward_y), hz));
    half[2] = fix16_add(fix16_add(fix16_mul(abs_fix(right_z), hx), fix16_mul(abs_fix(up_z), hy)),
                        fix16_mul(abs_fix(forward_z), hz));
}

void ps1_scene_init(void) {
    unsigned int entity_count = g_ps1_scene.entity_count;
    unsigned int r_count = 0;

    for (unsigned int i = 0; i < 4096; ++i) {
        s_runtime_lookup[i] = -1;
    }

    /* Identify and promote entities that actually require runtime mutability */
    for (unsigned int i = 0; i < entity_count; ++i) {
        const ps1_entity* src = &g_ps1_scene.entities[i];
        int need_runtime = 0;

        if (src->rigid_root_entity_index >= 0) {
            need_runtime = 1;
        } else if (src->rigid_anim_frame_count > 0 || src->vertex_anim_frame_count > 0 ||
                   src->rigid_idle_frame_count > 0 || src->rigid_walk_frame_count > 0 ||
                   src->rigid_aim_frame_count > 0 || src->transform_anim_key_count > 0) {
            need_runtime = 1;
        } else if (src->has_camera || src->collider_camera_shot_trigger) {
            need_runtime = 1;
        } else {
            for (unsigned int bi = 0; bi < g_ps1_scene.binding_count; ++bi) {
                if (g_ps1_scene.bindings[bi].entity_index == i) {
                    need_runtime = 1;
                    break;
                }
            }
        }

        if (need_runtime && r_count < 128u) {
            s_runtime_entities[r_count] = *src;
            s_vertex_anim_time_q16[r_count] = 0;
            s_rigid_anim_time_q16[r_count] = 0;
            s_transform_anim_time_q16[r_count] = 0;
            s_runtime_lookup[i] = (int16_t)r_count;
            r_count++;
        }
    }

    /* Fill remaining slots with the standard entities from the start of the list */
    for (unsigned int i = 0; i < entity_count; ++i) {
        if (r_count >= 128u) break;
        if (s_runtime_lookup[i] < 0) {
            const ps1_entity* src = &g_ps1_scene.entities[i];
            s_runtime_entities[r_count] = *src;
            s_vertex_anim_time_q16[r_count] = 0;
            s_rigid_anim_time_q16[r_count] = 0;
            s_transform_anim_time_q16[r_count] = 0;
            s_runtime_lookup[i] = (int16_t)r_count;
            r_count++;
        }
    }

    s_runtime_count = r_count;
    s_meshes = g_ps1_scene.meshes;
    s_mesh_count = g_ps1_scene.mesh_count;
    s_active_camera_index = g_ps1_scene.camera_entity_index;
    s_pending_camera_index = -1;
    s_pending_camera_frames = 0;
    ps1_scene_begin_frame();
}

void ps1_scene_update_vertex_anims(fix16_t delta_time) {
    uint32_t dt = delta_time > 0 ? (uint32_t)delta_time : 0u;
    for (unsigned int i = 0; i < s_runtime_count; ++i) {
        ps1_entity* e = &s_runtime_entities[i];
        if (e->transform_anim_key_count == 0)
            continue;
        sample_transform_animation(e, s_transform_anim_time_q16[i]);
        if (e->transform_anim_fps > 0 && (e->transform_anim_loop ||
            s_transform_anim_time_q16[i] <
                ((uint32_t)e->transform_anim_length_frames << 16) /
                    (uint32_t)e->transform_anim_fps)) {
            s_transform_anim_time_q16[i] += dt;
        }
    }
    for (unsigned int i = 0; i < s_runtime_count; ++i) {
        ps1_entity* e = &s_runtime_entities[i];
        if (!e->mesh_enabled || e->mesh != PS1_MESH_CUSTOM)
            continue;
        if (e->vertex_anim_first_mesh_index == 0 || e->vertex_anim_frame_count <= 1 || e->vertex_anim_fps == 0)
            continue;

        s_vertex_anim_time_q16[i] += dt;
        const uint32_t phase =
            (uint32_t)(((uint64_t)s_vertex_anim_time_q16[i] *
                        (uint32_t)e->vertex_anim_fps * 256u) >> 16u);
        const uint32_t anim_frame =
            (phase >> 8u) % (uint32_t)e->vertex_anim_frame_count;
        const uint32_t next_frame =
            (anim_frame + 1u) % (uint32_t)e->vertex_anim_frame_count;
        e->mesh_index = (uint16_t)(e->vertex_anim_first_mesh_index + (uint16_t)anim_frame);
        e->vertex_anim_next_mesh_index =
            (uint16_t)(e->vertex_anim_first_mesh_index + (uint16_t)next_frame);
        e->vertex_anim_lerp_q8 = (uint8_t)(phase & 255u);
    }

    for (unsigned int i = 0; i < s_runtime_count; ++i) {
        ps1_entity* e = &s_runtime_entities[i];
        if (e->rigid_root_entity_index >= 0) {
            const ps1_entity* root =
                ps1_scene_entity((unsigned int)e->rigid_root_entity_index);
            if (root) {
                const int aiming = root->animator_aim > (FIX16_ONE / 2);
                const int moving = root->animator_speed > (FIX16_ONE / 100);
                /* Keep every exported rigid-bone part derived from the root
                 * Animator parameters each frame.  Selecting only inside
                 * Animator.Set* allowed a part to retain its previous Walk
                 * clip after the root had already returned to Speed=0. */
                select_rigid_clip(e, aiming, moving);
            }
        }
        if (e->rigid_anim_frame_count == 0 || e->rigid_anim_fps == 0)
            continue;
        if ((uint32_t)e->rigid_anim_first_frame + (uint32_t)e->rigid_anim_frame_count >
            g_ps1_scene.rigid_anim_frame_count)
            continue;

        s_rigid_anim_time_q16[i] += dt;
        const uint32_t phase =
            (uint32_t)(((uint64_t)s_rigid_anim_time_q16[i] *
                        (uint32_t)e->rigid_anim_fps * 256u) >> 16u);
        const uint32_t anim_frame =
            (phase >> 8u) % (uint32_t)e->rigid_anim_frame_count;
        const uint32_t next_frame =
            (anim_frame + 1u) % (uint32_t)e->rigid_anim_frame_count;
        e->rigid_anim_current_frame = (uint16_t)anim_frame;
        e->rigid_anim_next_frame = (uint16_t)next_frame;
        e->rigid_anim_lerp_q8 = (uint8_t)(phase & 255u);
    }
}

static void select_rigid_clip(ps1_entity* e, int aiming, int moving) {
    uint16_t first = 0;
    uint16_t count = 0;
    uint8_t fps = 0;
    if (!e)
        return;
    if (aiming && e->rigid_aim_frame_count > 0) {
        first = e->rigid_aim_first_frame;
        count = e->rigid_aim_frame_count;
        fps = e->rigid_aim_fps;
    } else if (moving && e->rigid_walk_frame_count > 0) {
        first = e->rigid_walk_first_frame;
        count = e->rigid_walk_frame_count;
        fps = e->rigid_walk_fps;
    } else if (e->rigid_idle_frame_count > 0) {
        first = e->rigid_idle_first_frame;
        count = e->rigid_idle_frame_count;
        fps = e->rigid_idle_fps;
    }
    if (count == 0)
        return;
    if (e->rigid_anim_first_frame != first) {
        e->rigid_anim_current_frame = 0;
        e->rigid_anim_next_frame = count > 1 ? 1 : 0;
        e->rigid_anim_lerp_q8 = 0;
        s_rigid_anim_time_q16[(unsigned int)(e - s_runtime_entities)] = 0;
    }
    e->rigid_anim_first_frame = first;
    e->rigid_anim_frame_count = count;
    e->rigid_anim_fps = fps;
}

void ps1_scene_set_animator_float(unsigned int root_index, const char* name, fix16_t value) {
    ps1_entity* root;
    int aiming;
    int moving;
    if (root_index >= g_ps1_scene.entity_count || !name)
        return;
    root = ps1_scene_mutable_entity(root_index);
    if (!root)
        return;
    if (strcmp(name, "Speed") == 0) {
        root->animator_speed = value < 0 ? fix16_neg(value) : value;
    } else if (strcmp(name, "Aim") == 0 || strcmp(name, "Aiming") == 0) {
        root->animator_aim = value;
    } else if (strcmp(name, "Moving") == 0) {
        root->animator_speed = value ? FIX16_ONE : 0;
    } else {
        return;
    }

    aiming = root->animator_aim > (FIX16_ONE / 2);
    moving = root->animator_speed > (FIX16_ONE / 100);
    for (unsigned int i = 0; i < s_runtime_count; ++i) {
        ps1_entity* e = &s_runtime_entities[i];
        if (e->rigid_root_entity_index != (int16_t)root_index)
            continue;
        select_rigid_clip(e, aiming, moving);
    }
}

ps1_entity* ps1_scene_mutable_entity(unsigned int index) {
    if (index < g_ps1_scene.entity_count) {
        int16_t runtime_idx = s_runtime_lookup[index];
        if (runtime_idx >= 0) {
            return &s_runtime_entities[runtime_idx];
        }
    }
    return 0;
}

const ps1_entity* ps1_scene_entity(unsigned int index) {
    if (index < g_ps1_scene.entity_count) {
        int16_t runtime_idx = s_runtime_lookup[index];
        if (runtime_idx >= 0) {
            return &s_runtime_entities[runtime_idx];
        }
        return &g_ps1_scene.entities[index];
    }
    return 0;
}

const ps1_entity* ps1_scene_source_entity(unsigned int index) {
    if (index >= g_ps1_scene.entity_count) return 0;
    return &g_ps1_scene.entities[index];
}

unsigned int ps1_scene_entity_count(void) {
    return g_ps1_scene.entity_count;
}

void ps1_scene_begin_frame(void) {
    int best = -1;
    int best_priority = -32768;

    for (unsigned int trigger_idx = 0; trigger_idx < s_runtime_count; ++trigger_idx) {
        const ps1_entity* trigger = &s_runtime_entities[trigger_idx];
        int target_camera_idx;
        const ps1_entity* target_camera;
        if (trigger->collider_shape == -1 ||
            !trigger->collider_camera_shot_trigger ||
            trigger->collider_camera_target_index < 0)
            continue;
        target_camera_idx = trigger->collider_camera_target_index;
        if ((unsigned int)target_camera_idx >= g_ps1_scene.entity_count)
            continue;
        target_camera = ps1_scene_entity((unsigned int)target_camera_idx);
        if (!target_camera || !target_camera->has_camera)
            continue;

        for (unsigned int bi = 0; bi < g_ps1_scene.binding_count; ++bi) {
            const unsigned int target_idx = g_ps1_scene.bindings[bi].entity_index;
            if (target_idx >= g_ps1_scene.entity_count)
                continue;
            const ps1_entity* target_ent = ps1_scene_entity(target_idx);
            if (!target_ent || !aabb_overlap(target_ent, trigger))
                continue;

            if (target_camera->camera_shot_priority > best_priority) {
                best = target_camera_idx;
                best_priority = target_camera->camera_shot_priority;
            }
            break;
        }
    }

    if (best < 0 || best == s_active_camera_index) {
        s_pending_camera_index = -1;
        s_pending_camera_frames = 0;
        return;
    }

    if (s_pending_camera_index != best) {
        s_pending_camera_index = best;
        s_pending_camera_frames = 1;
        return;
    }

    if (s_pending_camera_frames < 2)
        ++s_pending_camera_frames;
    if (s_pending_camera_frames >= 2) {
        s_active_camera_index = best;
        s_pending_camera_index = -1;
        s_pending_camera_frames = 0;
    }
}

int ps1_scene_camera_index(void) {
    return s_active_camera_index;
}

const ps1_mesh* ps1_scene_meshes(void) { return s_meshes; }
unsigned int ps1_scene_mesh_count(void) { return s_mesh_count; }
