#include "render.h"
#include "scene.h"
#include "textures.h"
#include "fixedp.h"
#include "lookat.h"
#include "ui.h"

#include <stdint.h>
#include <psxapi.h>
#include <psxgpu.h>
#include "mipsync_gte.h"

#define OT_LEN 2048
#define PS1_WORLD_OT_MIN 2

/* FBX winding is preserved at export (no runtime backface pass). */
/* 1 editor world unit -> 64 PSX/GTE units (matches typical demo scales). */
#define PSX_UNITS_PER_WORLD 64
/* PSX fpscam default forward is +Z; Unity cameras look down -Z. */
#define PSX_YAW_UNITY_OFFSET 2048
#define PS1_VIEWMODEL_VERTEX_CACHE_MAX 1024
#define PS1_RIGID_VERTEX_CACHE_MAX 2048
#define PS1_STATIC_VERTEX_CACHE_MAX 2048
#define PS1_STATIC_VERTEX_CACHE_TOTAL 4096
#define PS1_STATIC_TRI_CACHE_TOTAL 4096
#define PS1_STATIC_ENTITY_CACHE_MAX 64
#define PS1_UI_PACKET_RESERVE 4096
#define PS1_OCCLUDER_CACHE_MAX 256
#define PS1_OCCLUDER_PROJECTED_VERTEX_MAX 2048
#define PS1_OCCLUDER_PROJECTED_TRI_MAX 2048
#define PS1_RENDER_ENTITY_LIST_MAX 2048
#ifndef PS1_PROJECTION_DEBUG
#define PS1_PROJECTION_DEBUG 0
#endif
#ifndef PS1_OCCLUDER_DEBUG_VISUALIZE
#define PS1_OCCLUDER_DEBUG_VISUALIZE 0
#endif
#ifndef PS1_OCCLUDER_DEBUG_FORCE_FRONT
#define PS1_OCCLUDER_DEBUG_FORCE_FRONT 0
#endif

/* Expand triangle vertices 1 pixel outward from centroid to fill
 * integer-quantisation seams at skeleton joints.  Variables must be
 * mutable int16_t. */
#define SEAM_FILL_EXPAND(x0, y0, x1, y1, x2, y2) \
    do { \
        int _cx = ((int)(x0) + (int)(x1) + (int)(x2)) / 3; \
        int _cy = ((int)(y0) + (int)(y1) + (int)(y2)) / 3; \
        if ((x0) < _cx) (x0)--; else if ((x0) > _cx) (x0)++; \
        if ((y0) < _cy) (y0)--; else if ((y0) > _cy) (y0)++; \
        if ((x1) < _cx) (x1)--; else if ((x1) > _cx) (x1)++; \
        if ((y1) < _cy) (y1)--; else if ((y1) > _cy) (y1)++; \
        if ((x2) < _cx) (x2)--; else if ((x2) > _cx) (x2)++; \
        if ((y2) < _cy) (y2)--; else if ((y2) > _cy) (y2)++; \
    } while(0)

static int mesh_triangle_otz(int32_t avsz) {
    int otz;
    otz = (int)(avsz >> 2);
    if (otz < PS1_WORLD_OT_MIN)
        return PS1_WORLD_OT_MIN;
    if (otz >= OT_LEN)
        return OT_LEN - 1;
    return otz;
}

static int mesh_triangle_otz_fine(int32_t avsz) {
    int otz;
    otz = (int)(avsz >> 1);
    if (otz < PS1_WORLD_OT_MIN)
        return PS1_WORLD_OT_MIN;
    if (otz >= OT_LEN)
        return OT_LEN - 1;
    return otz;
}

typedef struct {
    int16_t v0, v1, v2, v3;
} face_index;

typedef struct {
    int16_t v0, v1, v2;
} tri_index;

static SVECTOR s_cube_verts[] = {
    { -64, -64, -64, 0 }, {  64, -64, -64, 0 },
    { -64,  64, -64, 0 }, {  64,  64, -64, 0 },
    {  64, -64,  64, 0 }, { -64, -64,  64, 0 },
    {  64,  64,  64, 0 }, { -64,  64,  64, 0 },
};

static SVECTOR s_cube_norms[] = {
    { 0, 0, -ONE, 0 }, { 0, 0, ONE, 0 },
    { 0, -ONE, 0, 0 }, { 0, ONE, 0, 0 },
    { -ONE, 0, 0, 0 }, { ONE, 0, 0, 0 },
};

static SVECTOR s_plane_norm_up = { 0, ONE, 0, 0 };

static face_index s_cube_faces[] = {
    /* POLY_F4 uses triangles 0-1-2 and 1-2-3. Keep every face in the
     * renderer's front-facing winding after the Unity camera Y flip. */
    { 1, 0, 3, 2 }, { 5, 4, 7, 6 },
    { 4, 5, 1, 0 }, { 7, 6, 2, 3 },
    { 2, 0, 7, 5 }, { 1, 3, 4, 6 },
};

static SVECTOR s_sphere_verts[] = {
    {   0, -64,   0, 0 },
    {  45, -45,   0, 0 }, {  32, -45,  32, 0 },
    {   0, -45,  45, 0 }, { -32, -45,  32, 0 },
    { -45, -45,   0, 0 }, { -32, -45, -32, 0 },
    {   0, -45, -45, 0 }, {  32, -45, -32, 0 },
    {  64,   0,   0, 0 }, {  45,   0,  45, 0 },
    {   0,   0,  64, 0 }, { -45,   0,  45, 0 },
    { -64,   0,   0, 0 }, { -45,   0, -45, 0 },
    {   0,   0, -64, 0 }, {  45,   0, -45, 0 },
    {  45,  45,   0, 0 }, {  32,  45,  32, 0 },
    {   0,  45,  45, 0 }, { -32,  45,  32, 0 },
    { -45,  45,   0, 0 }, { -32,  45, -32, 0 },
    {   0,  45, -45, 0 }, {  32,  45, -32, 0 },
    {   0,  64,   0, 0 },
};

static tri_index s_sphere_tris[] = {
    { 0,  1,  2 }, { 0,  2,  3 }, { 0,  3,  4 }, { 0,  4,  5 },
    { 0,  5,  6 }, { 0,  6,  7 }, { 0,  7,  8 }, { 0,  8,  1 },
    { 1,  9,  2 }, { 2,  9, 10 }, { 2, 10,  3 }, { 3, 10, 11 },
    { 3, 11,  4 }, { 4, 11, 12 }, { 4, 12,  5 }, { 5, 12, 13 },
    { 5, 13,  6 }, { 6, 13, 14 }, { 6, 14,  7 }, { 7, 14, 15 },
    { 7, 15,  8 }, { 8, 15, 16 }, { 8, 16,  1 }, { 1, 16,  9 },
    { 9, 17, 10 }, { 10, 17, 18 }, { 10, 18, 11 }, { 11, 18, 19 },
    { 11, 19, 12 }, { 12, 19, 20 }, { 12, 20, 13 }, { 13, 20, 21 },
    { 13, 21, 14 }, { 14, 21, 22 }, { 14, 22, 15 }, { 15, 22, 23 },
    { 15, 23, 16 }, { 16, 23, 24 }, { 16, 24,  9 }, { 9, 24, 17 },
    { 25, 18, 17 }, { 25, 19, 18 }, { 25, 20, 19 }, { 25, 21, 20 },
    { 25, 22, 21 }, { 25, 23, 22 }, { 25, 24, 23 }, { 25, 17, 24 },
};

static MATRIX s_scene_color_mtx;

static MATRIX s_light_mtx;

static MATRIX s_cam_mtx;
static int     s_initialized = 0;
static int     s_screen_w = 320;
static int     s_screen_h = 240;
static int     s_geom_screen = 160;
static int     s_near_z = 16;
static int     s_far_z = 6400;
static uint16_t s_occluder_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_animated_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_custom_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_primitive_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_view_model_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_occluder_count;
static uint16_t s_animated_count;
static uint16_t s_custom_count;
static uint16_t s_primitive_count;
static uint16_t s_view_model_count;
static unsigned int s_indexed_entity_count;

typedef struct {
    int valid;
    uint16_t entity_index;
    MATRIX world_matrix;
    int bounding_radius;
    int16_t projected_camera_index;
    uint16_t projected_offset;
    uint16_t projected_count;
    uint16_t projected_built_count;
    int16_t bounds_camera_index;
    uint16_t bounds_offset;
    uint16_t bounds_count;
    uint16_t bounds_built_count;
} occluder_transform_cache;

typedef struct {
    int valid;
    int root_index;
    fix16_t sin_yaw;
    fix16_t cos_yaw;
    MATRIX matrix;
} rigid_root_transform_cache;

static rigid_root_transform_cache s_rigid_root_transform;
static occluder_transform_cache s_occluder_transform_cache[PS1_OCCLUDER_CACHE_MAX];
static int16_t s_occluder_projection_camera_index = -1;
static uint16_t s_occluder_projected_vertex_count = 0;
static uint16_t s_occluder_projected_tri_count = 0;
static int s_occluder_vertex_build_budget = 0;
static int s_occluder_bounds_build_budget = 0;
static uint8_t s_occluder_camera_cooldown = 0;

static int viewmodel_triangle_otz(int32_t avg_z) {
    int dz = (int)(avg_z - s_near_z);
    int otz;
    if (dz < 0)
        dz = 0;
    /* Keep first-person meshes in front of the world, but still sort their
     * own triangles enough to avoid self-overdraw on close weapons. UI uses
     * slot 1, so view models stay at 2+. */
    otz = 2 + (dz >> 3);
    if (otz > 96)
        otz = 96;
    return otz;
}

static int viewmodel_near_z(void) {
    int z = s_near_z >> 2;
    if (z < 2)
        z = 2;
    return z;
}

typedef struct {
    int16_t x;
    int16_t y;
    int32_t z;
} occluder_screen_vert;

static occluder_screen_vert
    s_occluder_projected_vertices[PS1_OCCLUDER_PROJECTED_VERTEX_MAX];

typedef struct {
    uint8_t min_x2;
    uint8_t max_x2;
    uint8_t min_y;
    uint8_t max_y;
} occluder_screen_bounds;

static occluder_screen_bounds
    s_occluder_projected_tri_bounds[PS1_OCCLUDER_PROJECTED_TRI_MAX];

typedef struct {
    int valid;
    int left;
    int top;
    int right;
    int bottom;
    int depth;
    int depth_margin;
} occluder_actor_mask;

static int world_to_psx(fix16_t v);
static void unity_pos_to_psx(const fix16_t pos[3], VECTOR* out);

static unsigned int active_background_texture_index(void) {
    unsigned int index = g_ps1_scene.background_texture_index;
    const int camera_index = ps1_scene_camera_index();
    const ps1_entity* camera =
        camera_index >= 0 ? ps1_scene_entity((unsigned int)camera_index) : 0;
    if (camera && camera->has_camera && camera->camera_background_texture_index != 0)
        index = camera->camera_background_texture_index;
    return index;
}

static void reset_static_entity_caches(void);

static void rebuild_render_entity_lists(void) {
    unsigned int i;
    const unsigned int count = g_ps1_scene.entity_count;
    s_occluder_count = 0;
    s_animated_count = 0;
    s_custom_count = 0;
    s_primitive_count = 0;
    s_view_model_count = 0;
    for (i = 0; i < PS1_OCCLUDER_CACHE_MAX; ++i) {
        s_occluder_transform_cache[i].valid = 0;
        s_occluder_transform_cache[i].projected_camera_index = -1;
        s_occluder_transform_cache[i].projected_count = 0;
        s_occluder_transform_cache[i].projected_built_count = 0;
        s_occluder_transform_cache[i].bounds_camera_index = -1;
        s_occluder_transform_cache[i].bounds_count = 0;
        s_occluder_transform_cache[i].bounds_built_count = 0;
    }
    s_occluder_projection_camera_index = -1;
    s_occluder_projected_vertex_count = 0;
    s_occluder_projected_tri_count = 0;
    reset_static_entity_caches();

    for (i = 0; i < count && i < PS1_RENDER_ENTITY_LIST_MAX; ++i) {
        const ps1_entity* ent = ps1_scene_entity(i);
        if (!ent || ent->has_camera || !ent->mesh_enabled)
            continue;
        if (ent->prerender_occluder) {
            s_occluder_entities[s_occluder_count++] = (uint16_t)i;
            continue;
        }
        if (ent->view_model) {
            s_view_model_entities[s_view_model_count++] = (uint16_t)i;
            continue;
        }
        if (ent->mesh == PS1_MESH_CUSTOM) {
            if (ent->rigid_root_entity_index >= 0 || ent->vertex_anim_frame_count > 0)
                s_animated_entities[s_animated_count++] = (uint16_t)i;
            else
                s_custom_entities[s_custom_count++] = (uint16_t)i;
            continue;
        }
        if (ent->mesh != PS1_MESH_NONE)
            s_primitive_entities[s_primitive_count++] = (uint16_t)i;
    }
    s_indexed_entity_count = count;
}

static occluder_actor_mask build_occluder_actor_mask(void) {
    occluder_actor_mask mask = { 0, 0, 0, 0, 0, 0, 0 };
    unsigned int i;
    for (i = 0; i < s_animated_count; ++i) {
        const ps1_entity* part = ps1_scene_entity(s_animated_entities[i]);
        const ps1_entity* root;
        fix16_t center[3];
        fix16_t radius_x_fix;
        fix16_t radius_y_fix;
        fix16_t depth_radius_fix;
        fix16_t scaled_extent;
        VECTOR world, camera;
        int radius_x;
        int radius_y;
        int depth_radius;
        int screen_radius_x;
        int screen_radius_y;
        int center_x;
        int center_y;
        if (!part || part->rigid_root_entity_index < 0)
            continue;
        root = ps1_scene_entity((unsigned int)part->rigid_root_entity_index);
        if (!root)
            continue;

        center[0] = root->position[0] +
            fix16_mul(root->collider_center[0], root->scale[0]);
        center[1] = root->position[1] +
            fix16_mul(root->collider_center[1], root->scale[1]);
        center[2] = root->position[2] +
            fix16_mul(root->collider_center[2], root->scale[2]);
        unity_pos_to_psx(center, &world);
        ApplyMatrixLV(&s_cam_mtx, &world, &camera);
        camera.vx += s_cam_mtx.t[0];
        camera.vy += s_cam_mtx.t[1];
        camera.vz += s_cam_mtx.t[2];
        if (camera.vz <= s_near_z)
            continue;

        radius_x_fix = fix16_mul(root->collider_half_extents[0],
                                 fix16_abs(root->scale[0]));
        radius_y_fix = fix16_mul(root->collider_half_extents[1],
                                 fix16_abs(root->scale[1]));
        scaled_extent = fix16_mul(root->collider_half_extents[2],
                                  fix16_abs(root->scale[2]));
        if (scaled_extent > radius_x_fix)
            radius_x_fix = scaled_extent;
        scaled_extent = fix16_mul(root->collider_radius,
                                  fix16_abs(root->scale[0]));
        if (scaled_extent > radius_x_fix)
            radius_x_fix = scaled_extent;
        if (radius_x_fix <= 0)
            radius_x_fix = FIX16_ONE / 3;
        if (radius_y_fix <= 0)
            radius_y_fix = FIX16_FROM_INT(1);
        depth_radius_fix = radius_x_fix > radius_y_fix ? radius_x_fix : radius_y_fix;
        radius_x = world_to_psx(radius_x_fix);
        radius_y = world_to_psx(radius_y_fix);
        depth_radius = world_to_psx(depth_radius_fix);
        if (radius_x < 40) radius_x = 40;
        if (radius_y < 48) radius_y = 48;
        screen_radius_x =
            (int)(((int64_t)s_geom_screen * radius_x) / camera.vz) + 6;
        screen_radius_y =
            (int)(((int64_t)s_geom_screen * radius_y) / camera.vz) + 8;
        if (screen_radius_x < 8) screen_radius_x = 8;
        if (screen_radius_y < 12) screen_radius_y = 12;
        center_x = (s_screen_w / 2) +
            (int)(((int64_t)s_geom_screen * camera.vx) / camera.vz);
        center_y = (s_screen_h / 2) +
            (int)(((int64_t)s_geom_screen * camera.vy) / camera.vz);

        mask.valid = 1;
        mask.left = center_x - screen_radius_x;
        mask.top = center_y - screen_radius_y;
        mask.right = center_x + screen_radius_x;
        mask.bottom = center_y + screen_radius_y;
        if (mask.left < 0) mask.left = 0;
        if (mask.top < 0) mask.top = 0;
        if (mask.right >= s_screen_w) mask.right = s_screen_w - 1;
        if (mask.bottom >= s_screen_h) mask.bottom = s_screen_h - 1;
        mask.depth = camera.vz;
        mask.depth_margin = depth_radius / 8;
        return mask;
    }
    return mask;
}

static int clip_occluder_edge(const occluder_screen_vert* input, int input_count,
                              occluder_screen_vert* output, int max_output,
                              int axis, int boundary, int keep_greater) {
    int output_count = 0;
    int i;
    occluder_screen_vert previous;
    if (input_count <= 0)
        return 0;

    previous = input[input_count - 1];
    for (i = 0; i < input_count; ++i) {
        const occluder_screen_vert current = input[i];
        const int32_t current_axis = axis == 0 ? current.x : current.y;
        const int32_t previous_axis = axis == 0 ? previous.x : previous.y;
        const int current_inside =
            keep_greater ? current_axis >= boundary : current_axis <= boundary;
        const int previous_inside =
            keep_greater ? previous_axis >= boundary : previous_axis <= boundary;

        if (current_inside != previous_inside) {
            const int32_t denominator = current_axis - previous_axis;
            occluder_screen_vert intersection = previous;
            if (denominator != 0) {
                const int32_t numerator = boundary - previous_axis;
                intersection.x = previous.x +
                    ((int32_t)(current.x - previous.x) * numerator) / denominator;
                intersection.y = previous.y +
                    ((int32_t)(current.y - previous.y) * numerator) / denominator;
                intersection.z = previous.z +
                    ((current.z - previous.z) * numerator) / denominator;
            }
            if (axis == 0) intersection.x = boundary;
            else intersection.y = boundary;
            if (output_count < max_output) {
                output[output_count++] = intersection;
            }
        }
        if (current_inside) {
            if (output_count < max_output) {
                output[output_count++] = current;
            }
        }
        previous = current;
    }
    return output_count;
}

static int clip_occluder_to_rect(const occluder_screen_vert source[3],
                                 occluder_screen_vert output[32],
                                 int left, int top, int right, int bottom) {
    occluder_screen_vert a[32];
    occluder_screen_vert b[32];
    int count = 3;
    int i;
    for (i = 0; i < 3; ++i)
        a[i] = source[i];
    count = clip_occluder_edge(a, count, b, 32, 0, left, 1);
    count = clip_occluder_edge(b, count, a, 32, 0, right, 0);
    count = clip_occluder_edge(a, count, b, 32, 1, top, 1);
    count = clip_occluder_edge(b, count, output, 32, 1, bottom, 0);
    return count;
}

typedef struct {
    VECTOR p;
    int u;
    int v;
} ps1_clip_vert;

static VECTOR s_viewmodel_vertex_cache[PS1_VIEWMODEL_VERTEX_CACHE_MAX];
static int16_t s_viewmodel_screen_x[PS1_VIEWMODEL_VERTEX_CACHE_MAX];
static int16_t s_viewmodel_screen_y[PS1_VIEWMODEL_VERTEX_CACHE_MAX];
static int32_t s_rigid_screen_cache[PS1_RIGID_VERTEX_CACHE_MAX];
static int32_t s_rigid_depth_cache[PS1_RIGID_VERTEX_CACHE_MAX];
static int32_t s_static_screen_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
static int32_t s_static_depth_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
static uint8_t s_static_light_cache[PS1_STATIC_VERTEX_CACHE_TOTAL][3];
static uint8_t s_static_face_light_cache[PS1_STATIC_TRI_CACHE_TOTAL][3];

typedef struct {
    const ps1_entity* entity;
    const ps1_mesh* mesh;
    const ps1_mesh* next_mesh;
    MATRIX projected_matrix;
    MATRIX light_matrix;
    uint16_t vertex_offset;
    uint16_t vertex_count;
    uint16_t triangle_offset;
    uint16_t triangle_count;
    int geom_screen;
    uint8_t vertex_lerp_q8;
    uint8_t base_r, base_g, base_b;
    uint8_t projection_valid;
    uint8_t lighting_valid;
} ps1_static_entity_cache;

static ps1_static_entity_cache s_static_entity_caches[PS1_STATIC_ENTITY_CACHE_MAX];
static uint16_t s_static_vertex_cache_used;
static uint16_t s_static_triangle_cache_used;

static void reset_static_entity_caches(void) {
    int i;
    s_static_vertex_cache_used = 0;
    s_static_triangle_cache_used = 0;
    for (i = 0; i < PS1_STATIC_ENTITY_CACHE_MAX; ++i) {
        s_static_entity_caches[i].entity = 0;
        s_static_entity_caches[i].projection_valid = 0;
        s_static_entity_caches[i].lighting_valid = 0;
    }
}

static int matrix_equal(const MATRIX* a, const MATRIX* b) {
    int row;
    int col;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            if (a->m[row][col] != b->m[row][col])
                return 0;
        }
        if (a->t[row] != b->t[row])
            return 0;
    }
    return 1;
}

static ps1_static_entity_cache* static_entity_cache_for(
        const ps1_entity* entity, const ps1_mesh* mesh) {
    int i;
    ps1_static_entity_cache* free_entry = 0;
    for (i = 0; i < PS1_STATIC_ENTITY_CACHE_MAX; ++i) {
        ps1_static_entity_cache* entry = &s_static_entity_caches[i];
        if (entry->entity == entity) {
            if (entry->vertex_count == mesh->vert_count &&
                entry->triangle_count == mesh->tri_count)
                return entry;
            return 0;
        }
        if (!entry->entity && !free_entry)
            free_entry = entry;
    }
    if (!free_entry ||
        s_static_vertex_cache_used + mesh->vert_count > PS1_STATIC_VERTEX_CACHE_TOTAL ||
        s_static_triangle_cache_used + mesh->tri_count > PS1_STATIC_TRI_CACHE_TOTAL)
        return 0;
    free_entry->entity = entity;
    free_entry->mesh = 0;
    free_entry->next_mesh = 0;
    free_entry->vertex_offset = s_static_vertex_cache_used;
    free_entry->vertex_count = mesh->vert_count;
    free_entry->triangle_offset = s_static_triangle_cache_used;
    free_entry->triangle_count = mesh->tri_count;
    free_entry->projection_valid = 0;
    free_entry->lighting_valid = 0;
    s_static_vertex_cache_used += mesh->vert_count;
    s_static_triangle_cache_used += mesh->tri_count;
    return free_entry;
}

static int entity_vertex_lerp_mesh(const ps1_entity* ent, const ps1_mesh* current,
                                   const ps1_mesh** out_next) {
    const ps1_mesh* next;
    unsigned int next_idx;

    if (!ent || !current || !out_next)
        return 0;
    if (ent->vertex_anim_frame_count <= 1 || ent->vertex_anim_lerp_q8 == 0)
        return 0;
    if (ent->vertex_anim_next_mesh_index == 0)
        return 0;

    next_idx = (unsigned int)(ent->vertex_anim_next_mesh_index - 1u);
    if (next_idx >= g_ps1_mesh_count)
        return 0;

    next = &g_ps1_meshes[next_idx];
    if (!next || next->vert_count != current->vert_count || next->tri_count != current->tri_count)
        return 0;

    *out_next = next;
    return 1;
}

static SVECTOR lerp_svector_q8(const SVECTOR* a, const SVECTOR* b, uint8_t t) {
    SVECTOR out;
    out.vx = (short)(((int32_t)a->vx * (256 - t) + (int32_t)b->vx * t) >> 8);
    out.vy = (short)(((int32_t)a->vy * (256 - t) + (int32_t)b->vy * t) >> 8);
    out.vz = (short)(((int32_t)a->vz * (256 - t) + (int32_t)b->vz * t) >> 8);
    out.pad = 0;
    return out;
}

/* Shade a mesh-local normal (12.4) with the current GTE light matrices. */
static void gte_shade_normal_vec(SVECTOR* nSafe, uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                 uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    uint8_t lit[4];
    int i;

    if (nSafe->vx == 0 && nSafe->vy == 0 && nSafe->vz == 0)
        nSafe->vy = 3800;

    lit[0] = 128;
    lit[1] = 128;
    lit[2] = 128;
    lit[3] = 0;
    gte_ldrgb(lit);
    gte_ldv0(nSafe);
    gte_ncs();
    gte_strgb(lit);

    /* GTE MAC overflow / back-facing → 0 RGB. Lift shadows (half-Lambert-ish). */
    for (i = 0; i < 3; ++i) {
        if (lit[i] < 64)
            lit[i] = 64;
        else
            lit[i] = (uint8_t)(((uint16_t)lit[i] * 3 + 80) >> 2);
    }

    {
        int r = (int)(((uint16_t)lit[0] * (uint16_t)base_r) >> 8);
        int g = (int)(((uint16_t)lit[1] * (uint16_t)base_g) >> 8);
        int b = (int)(((uint16_t)lit[2] * (uint16_t)base_b) >> 8);
        *out_r = (uint8_t)r;
        *out_g = (uint8_t)g;
        *out_b = (uint8_t)b;
    }
}

static void gte_shade_normal(const SVECTOR* n, uint8_t base_r, uint8_t base_g, uint8_t base_b,
                             uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    SVECTOR nSafe = *n;
    gte_shade_normal_vec(&nSafe, base_r, base_g, base_b, out_r, out_g, out_b);
}

static uint8_t clamp_textured_smooth_channel(uint8_t lit, uint8_t base) {
    /* On textured PS1 primitives RGB 128 is neutral modulation. Values above
     * it brighten the texel and turn smooth highlights into conspicuous,
     * triangle-shaped overexposed regions. Preserve the full per-vertex
     * gradient, but never let lighting amplify the source texture. */
    const uint8_t ceiling = base < 128 ? base : 128;
    return lit < ceiling ? lit : ceiling;
}

static void gte_shade_textured_smooth_normal(const SVECTOR* n,
                                             uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                             uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    uint8_t r, g, b;
    gte_shade_normal(n, base_r, base_g, base_b, &r, &g, &b);
    *out_r = clamp_textured_smooth_channel(r, base_r);
    *out_g = clamp_textured_smooth_channel(g, base_g);
    *out_b = clamp_textured_smooth_channel(b, base_b);
}

static void rigid_light_bin_normal(int bin, SVECTOR* out) {
    const int x = (bin / 9) - 1;
    const int y = ((bin / 3) % 3) - 1;
    const int z = (bin % 3) - 1;
    const int components = (x != 0) + (y != 0) + (z != 0);
    const int magnitude = components <= 1 ? 4096 : (components == 2 ? 2896 : 2365);
    out->vx = (short)(x * magnitude);
    out->vy = (short)(y * magnitude);
    out->vz = (short)(z * magnitude);
    out->pad = 0;
    if (components == 0)
        out->vy = 4096;
}

static void viewmodel_cpu_shade_normal(const MATRIX* light_mtx, const SVECTOR* n,
                                       uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                       uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    const ps1_light* light = &g_ps1_scene.light;
    int32_t dot;
    int intensity;
    int r, g, b;

    if (!light->enabled) {
        *out_r = (uint8_t)(((uint16_t)128 * (uint16_t)base_r) >> 8);
        *out_g = (uint8_t)(((uint16_t)128 * (uint16_t)base_g) >> 8);
        *out_b = (uint8_t)(((uint16_t)128 * (uint16_t)base_b) >> 8);
        return;
    }

    dot = (int32_t)n->vx * light_mtx->m[0][0] +
          (int32_t)n->vy * light_mtx->m[0][1] +
          (int32_t)n->vz * light_mtx->m[0][2];
    intensity = (int)(dot >> 12);
    if (intensity < 0)
        intensity = 0;
    if (intensity > ONE)
        intensity = ONE;

    r = light->ambient_r + (int)(((int32_t)light->color_r * 3 * intensity) / (5 * ONE));
    g = light->ambient_g + (int)(((int32_t)light->color_g * 3 * intensity) / (5 * ONE));
    b = light->ambient_b + (int)(((int32_t)light->color_b * 3 * intensity) / (5 * ONE));

    if (r < 48) r = 48; else if (r > 255) r = 255;
    if (g < 48) g = 48; else if (g > 255) g = 255;
    if (b < 48) b = 48; else if (b > 255) b = 255;

    *out_r = (uint8_t)(((uint16_t)r * (uint16_t)base_r) >> 8);
    *out_g = (uint8_t)(((uint16_t)g * (uint16_t)base_g) >> 8);
    *out_b = (uint8_t)(((uint16_t)b * (uint16_t)base_b) >> 8);
}

static int triangle_outside_screen(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                   int16_t x2, int16_t y2) {
    const int margin = 64;
    if (x0 < -margin && x1 < -margin && x2 < -margin)
        return 1;
    if (x0 > s_screen_w + margin && x1 > s_screen_w + margin && x2 > s_screen_w + margin)
        return 1;
    if (y0 < -margin && y1 < -margin && y2 < -margin)
        return 1;
    if (y0 > s_screen_h + margin && y1 > s_screen_h + margin && y2 > s_screen_h + margin)
        return 1;
    return 0;
}

static void apply_scene_lighting(void) {
    const ps1_light* l = &g_ps1_scene.light;
    if (l->enabled) {
        /* Ambient */
        gte_SetBackColor(l->ambient_r, l->ambient_g, l->ambient_b);

        /* Color matrix: column 0 = light color (12.4, ONE=4096). */
        /* Scale light down a bit to better match editor brightness. */
        const int32_t scale = (ONE * 3) / 5; /* ~60% */
        s_scene_color_mtx.m[0][0] = (int16_t)((int32_t)l->color_r * scale / 255);
        s_scene_color_mtx.m[1][0] = (int16_t)((int32_t)l->color_g * scale / 255);
        s_scene_color_mtx.m[2][0] = (int16_t)((int32_t)l->color_b * scale / 255);
        s_scene_color_mtx.m[0][1] = 0; s_scene_color_mtx.m[1][1] = 0; s_scene_color_mtx.m[2][1] = 0;
        s_scene_color_mtx.m[0][2] = 0; s_scene_color_mtx.m[1][2] = 0; s_scene_color_mtx.m[2][2] = 0;
        gte_SetColorMatrix(&s_scene_color_mtx);

        /* Single directional light in row 0. */
        s_light_mtx.m[0][0] = l->dir_x;
        s_light_mtx.m[0][1] = l->dir_y;
        s_light_mtx.m[0][2] = l->dir_z;
        s_light_mtx.m[1][0] = 0; s_light_mtx.m[1][1] = 0; s_light_mtx.m[1][2] = 0;
        s_light_mtx.m[2][0] = 0; s_light_mtx.m[2][1] = 0; s_light_mtx.m[2][2] = 0;
    } else {
        /* A new scene has no explicit light yet. Keep the fallback close to
         * the editor's neutral preview instead of driving white materials to
         * 255 and washing out every untextured primitive. */
        const int32_t fallback_scale = (ONE * 3) / 5;
        gte_SetBackColor(32, 32, 32);
        s_scene_color_mtx.m[0][0] = fallback_scale;
        s_scene_color_mtx.m[1][0] = fallback_scale;
        s_scene_color_mtx.m[2][0] = fallback_scale;
        s_scene_color_mtx.m[0][1] = 0;   s_scene_color_mtx.m[1][1] = 0;   s_scene_color_mtx.m[2][1] = 0;
        s_scene_color_mtx.m[0][2] = 0;   s_scene_color_mtx.m[1][2] = 0;   s_scene_color_mtx.m[2][2] = 0;
        gte_SetColorMatrix(&s_scene_color_mtx);
        /* No exported light: use a neutral overhead light.  Normals use the
         * engine's +Y-up convention, so -Y would illuminate undersides. */
        s_light_mtx.m[0][0] = 0; s_light_mtx.m[0][1] = ONE; s_light_mtx.m[0][2] = 0;
        s_light_mtx.m[1][0] = 0; s_light_mtx.m[1][1] = 0; s_light_mtx.m[1][2] = 0;
        s_light_mtx.m[2][0] = 0; s_light_mtx.m[2][1] = 0; s_light_mtx.m[2][2] = 0;
    }
}

static int deg_to_gte(fix16_t deg) {
    return (int)((int64_t)deg * 4096 / (360 * 65536));
}

static int world_to_psx(fix16_t v) {
    return (int)((int64_t)v * PSX_UNITS_PER_WORLD >> 16);
}

static int clamp_i16(int v) {
    if (v < -32768)
        return -32768;
    if (v > 32767)
        return 32767;
    return v;
}

static int clamp_u8(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

static void apply_scene_fog(int32_t camera_z, uint8_t* r, uint8_t* g, uint8_t* b) {
    int start_z;
    int end_z;
    int amount_q8;
    if (!g_ps1_scene.fog_enabled || !r || !g || !b)
        return;

    start_z = world_to_psx(g_ps1_scene.fog_start);
    end_z = world_to_psx(g_ps1_scene.fog_end);
    if (end_z <= start_z)
        end_z = start_z + 1;
    if (camera_z <= start_z)
        return;
    if (camera_z >= end_z) {
        *r = g_ps1_scene.fog_r;
        *g = g_ps1_scene.fog_g;
        *b = g_ps1_scene.fog_b;
        return;
    }

    amount_q8 = (int)(((int64_t)(camera_z - start_z) * 256) /
                      (int64_t)(end_z - start_z));
    *r = (uint8_t)(((int)*r * (256 - amount_q8) +
                    (int)g_ps1_scene.fog_r * amount_q8 + 128) >> 8);
    *g = (uint8_t)(((int)*g * (256 - amount_q8) +
                    (int)g_ps1_scene.fog_g * amount_q8 + 128) >> 8);
    *b = (uint8_t)(((int)*b * (256 - amount_q8) +
                    (int)g_ps1_scene.fog_b * amount_q8 + 128) >> 8);
}

/* Convert one material UV axis to the texture's local pixel range. Tiling and
 * offset travel with the entity as signed 8.8 values, so primitive meshes use
 * the same material contract as exported custom meshes. `is_end` preserves
 * the last texel at a positive repeat boundary instead of wrapping it to 0. */
static int primitive_material_uv(int texture_base, int texture_span,
                                 int16_t tiling_q8, int16_t offset_q8,
                                 int grid, int divisions, int is_end) {
    int32_t coordinate_q8;
    int32_t fraction_q8;
    int pixel;
    if (divisions < 1)
        divisions = 1;
    coordinate_q8 = (int32_t)offset_q8 +
        ((int32_t)tiling_q8 * (int32_t)grid) / divisions;
    fraction_q8 = coordinate_q8 % 256;
    if (fraction_q8 < 0)
        fraction_q8 += 256;
    if (is_end && tiling_q8 > 0 && fraction_q8 == 0 && grid > 0)
        fraction_q8 = 256;
    pixel = texture_base + (int)((fraction_q8 * texture_span + 128) >> 8);
    return clamp_u8(pixel);
}

static void transform_point(const MATRIX* m, const SVECTOR* in, VECTOR* out) {
    out->vx = (int)(((int64_t)m->m[0][0] * in->vx +
                     (int64_t)m->m[0][1] * in->vy +
                     (int64_t)m->m[0][2] * in->vz) >> 12) + m->t[0];
    out->vy = (int)(((int64_t)m->m[1][0] * in->vx +
                     (int64_t)m->m[1][1] * in->vy +
                     (int64_t)m->m[1][2] * in->vz) >> 12) + m->t[1];
    out->vz = (int)(((int64_t)m->m[2][0] * in->vx +
                     (int64_t)m->m[2][1] * in->vy +
                     (int64_t)m->m[2][2] * in->vz) >> 12) + m->t[2];
}

static void unity_pos_to_psx(const fix16_t pos[3], VECTOR* out) {
    out->vx = world_to_psx(pos[0]);
    out->vy = world_to_psx(pos[1]);
    out->vz = world_to_psx(pos[2]);
}

static void unity_euler_to_psx_object(fix16_t pitch, fix16_t yaw, fix16_t roll, SVECTOR* out) {
    out->vx = (short)deg_to_gte(pitch);
    out->vy = (short)deg_to_gte(yaw);
    out->vz = (short)deg_to_gte(roll);
}

static void unity_euler_to_psx_camera(fix16_t pitch, fix16_t yaw, fix16_t roll, SVECTOR* out) {
    out->vx = (short)deg_to_gte(pitch);
    out->vy = (short)(PSX_YAW_UNITY_OFFSET + deg_to_gte(yaw));
    out->vz = (short)deg_to_gte(roll);
}

static int primitive_axis_scale_psx(const ps1_entity* ent, int axis) {
    int half = world_to_psx(fix16_mul(ent->mesh_size, ent->scale[axis])) / 2;
    int s;
    if (half < 1)
        half = 1;
    if (half > 32000)
        half = 32000;
    s = (half * ONE) / 64;
    return s == 0 ? 1 : s;
}

static int fov_to_geom_screen(fix16_t fov_deg) {
    static const int focal_240_by_5deg[] = {
        541, 448, 381, 330, 290, 257, 231, 208, 188,
        171, 156, 143, 131, 120, 110, 101, 92, 84
    };
    int deg = FIX16_TO_INT(fov_deg);
    int idx;
    int base;
    if (deg < 25)
        deg = 25;
    if (deg > 110)
        deg = 110;
    idx = (deg - 25 + 2) / 5;
    if (idx < 0)
        idx = 0;
    if (idx >= (int)(sizeof(focal_240_by_5deg) / sizeof(focal_240_by_5deg[0])))
        idx = (int)(sizeof(focal_240_by_5deg) / sizeof(focal_240_by_5deg[0])) - 1;
    base = focal_240_by_5deg[idx];
    return (base * s_screen_h) / 240;
}

static void make_clip_vert(const MATRIX* m, const SVECTOR* p, const ps1_uv8* uv,
                           ps1_clip_vert* out) {
    transform_point(m, p, &out->p);
    if (uv) {
        out->u = uv->u;
        out->v = uv->v;
    } else {
        out->u = 0;
        out->v = 0;
    }
}

static void make_cached_clip_vert(const VECTOR* p, const ps1_uv8* uv, ps1_clip_vert* out) {
    out->p = *p;
    if (uv) {
        out->u = uv->u;
        out->v = uv->v;
    } else {
        out->u = 0;
        out->v = 0;
    }
}

static ps1_clip_vert lerp_clip_vert(const ps1_clip_vert* a, const ps1_clip_vert* b, int near_z) {
    ps1_clip_vert out;
    const int dz = b->p.vz - a->p.vz;
    const int az = near_z - a->p.vz;
    if (dz == 0)
        return *a;
    out.p.vx = a->p.vx + (int)(((int64_t)(b->p.vx - a->p.vx) * az) / dz);
    out.p.vy = a->p.vy + (int)(((int64_t)(b->p.vy - a->p.vy) * az) / dz);
    out.p.vz = near_z;
    out.u = a->u + (int)(((int64_t)(b->u - a->u) * az) / dz);
    out.v = a->v + (int)(((int64_t)(b->v - a->v) * az) / dz);
    return out;
}

static int clip_triangle_near(const ps1_clip_vert in[3], ps1_clip_vert out[4], int near_z) {
    int count = 0;
    int i;

    for (i = 0; i < 3; ++i) {
        const ps1_clip_vert* a = &in[i];
        const ps1_clip_vert* b = &in[(i + 1) % 3];
        const int a_in = a->p.vz >= near_z;
        const int b_in = b->p.vz >= near_z;

        if (a_in && b_in) {
            out[count++] = *b;
        } else if (a_in && !b_in) {
            out[count++] = lerp_clip_vert(a, b, near_z);
        } else if (!a_in && b_in) {
            out[count++] = lerp_clip_vert(a, b, near_z);
            out[count++] = *b;
        }
        if (count >= 4)
            break;
    }
    return count;
}

static void project_clip_vert(const ps1_clip_vert* in, int16_t* x, int16_t* y, int min_z) {
    int z = in->p.vz;
    int sx, sy;
    if (z < min_z)
        z = min_z;
    sx = (s_screen_w / 2) + (int)(((int64_t)s_geom_screen * in->p.vx) / z);
    sy = (s_screen_h / 2) + (int)(((int64_t)s_geom_screen * in->p.vy) / z);
    *x = (int16_t)clamp_i16(sx);
    *y = (int16_t)clamp_i16(sy);
}

static void project_vector_vert(const VECTOR* in, int16_t* x, int16_t* y, int min_z) {
    int z = in->vz;
    int sx, sy;
    if (z < min_z)
        z = min_z;
    sx = (s_screen_w / 2) + (int)(((int64_t)s_geom_screen * in->vx) / z);
    sy = (s_screen_h / 2) + (int)(((int64_t)s_geom_screen * in->vy) / z);
    *x = (int16_t)clamp_i16(sx);
    *y = (int16_t)clamp_i16(sy);
}

static int screen_front_facing(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                               int16_t x2, int16_t y2) {
    const int32_t area =
        (int32_t)(x1 - x0) * (int32_t)(y2 - y0) -
        (int32_t)(y1 - y0) * (int32_t)(x2 - x0);
    /* Very close view-model triangles are clipped and projected in integer
     * screen space, so tiny/edge-on front faces can numerically flip sign.
     * Keep a tolerance to avoid pinholes while still rejecting clear, large
     * backfaces. */
    return area < 2048;
}

static int screen_front_facing_strict(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                      int16_t x2, int16_t y2) {
    const int32_t area =
        (int32_t)(x1 - x0) * (int32_t)(y2 - y0) -
        (int32_t)(y1 - y0) * (int32_t)(x2 - x0);
    return area < 0;
}

static int camera_space_front_facing(const ps1_clip_vert in[3]) {
    const int64_t x0 = in[0].p.vx;
    const int64_t y0 = in[0].p.vy;
    const int64_t z0 = in[0].p.vz;
    const int64_t x1 = in[1].p.vx;
    const int64_t y1 = in[1].p.vy;
    const int64_t z1 = in[1].p.vz;
    const int64_t x2 = in[2].p.vx;
    const int64_t y2 = in[2].p.vy;
    const int64_t z2 = in[2].p.vz;
    const int64_t ax = x1 * z0 - x0 * z1;
    const int64_t ay = y1 * z0 - y0 * z1;
    const int64_t bx = x2 * z0 - x0 * z2;
    const int64_t by = y2 * z0 - y0 * z2;
    const int64_t area = ax * by - ay * bx;
    return area < 0;
}

static void unity_forward_dir(fix16_t pitch_deg, fix16_t yaw_deg, VECTOR* dir) {
    /* Matches TransformComponent::RotationMatrixFromEuler * (0,0,-1). */
    fix16_t p = fix16_div(pitch_deg, FIX16_FROM_INT(360));
    fix16_t y = fix16_div(yaw_deg, FIX16_FROM_INT(360));
    fix16_t sp = fix16_sin(p);
    fix16_t cp = fix16_cos(p);
    fix16_t sy = fix16_sin(y);
    fix16_t cy = fix16_cos(y);

    fix16_t fx = fix16_neg(fix16_mul(cp, sy));
    fix16_t fy = sp;
    fix16_t fz = fix16_neg(fix16_mul(cp, cy));

    /* Forward sample distance: 8 world units. */
    const int dist = PSX_UNITS_PER_WORLD * 8;
    dir->vx = (int)((int64_t)fx * dist >> 16);
    dir->vy = (int)((int64_t)fy * dist >> 16);
    dir->vz = (int)((int64_t)fz * dist >> 16);
    if (dir->vx == 0 && dir->vy == 0 && dir->vz == 0)
        dir->vz = -dist;
}

static short fix16_to_q12(fix16_t v) {
    int32_t q = (int32_t)(v >> 4);
    if (q > 32767)
        q = 32767;
    if (q < -32768)
        q = -32768;
    return (short)q;
}

static void build_unity_camera_matrix(const VECTOR* eye, fix16_t pitch_deg,
                                      fix16_t yaw_deg, fix16_t roll_deg, MATRIX* out) {
    fix16_t p = fix16_div(pitch_deg, FIX16_FROM_INT(360));
    fix16_t y = fix16_div(yaw_deg, FIX16_FROM_INT(360));
    fix16_t r = fix16_div(roll_deg, FIX16_FROM_INT(360));
    fix16_t sp = fix16_sin(p);
    fix16_t cp = fix16_cos(p);
    fix16_t sy = fix16_sin(y);
    fix16_t cy = fix16_cos(y);
    fix16_t sr = fix16_sin(r);
    fix16_t cr = fix16_cos(r);

    /* TransformComponent::RotationMatrixFromEuler uses Ry * Rx * Rz.
     * GTE camera space is +Z forward and +Y down on screen, so the view
     * rows are right, -up, and forward where forward is Unity local -Z. */
    fix16_t right_x = fix16_add(fix16_mul(cy, cr), fix16_mul(fix16_mul(sy, sp), sr));
    fix16_t right_y = fix16_mul(cp, sr);
    fix16_t right_z = fix16_sub(fix16_mul(fix16_mul(cy, sp), sr), fix16_mul(sy, cr));

    fix16_t up_x = fix16_sub(fix16_mul(fix16_mul(sy, sp), cr), fix16_mul(cy, sr));
    fix16_t up_y = fix16_mul(cp, cr);
    fix16_t up_z = fix16_add(fix16_mul(sy, sr), fix16_mul(fix16_mul(cy, sp), cr));

    fix16_t forward_x = fix16_neg(fix16_mul(sy, cp));
    fix16_t forward_y = sp;
    fix16_t forward_z = fix16_neg(fix16_mul(cy, cp));

    VECTOR pos, vec;
    out->m[0][0] = fix16_to_q12(right_x);
    out->m[1][0] = fix16_to_q12(fix16_neg(up_x));
    out->m[2][0] = fix16_to_q12(forward_x);
    out->m[0][1] = fix16_to_q12(right_y);
    out->m[1][1] = fix16_to_q12(fix16_neg(up_y));
    out->m[2][1] = fix16_to_q12(forward_y);
    out->m[0][2] = fix16_to_q12(right_z);
    out->m[1][2] = fix16_to_q12(fix16_neg(up_z));
    out->m[2][2] = fix16_to_q12(forward_z);

    pos.vx = -eye->vx;
    pos.vy = -eye->vy;
    pos.vz = -eye->vz;
    ApplyMatrixLV(out, &pos, &vec);
    TransMatrix(out, &vec);
}

static void build_camera_matrix(const ps1_entity* cam) {
    VECTOR eye, at, forward;
    /* Unity/world convention: Y is up. */
    SVECTOR up = { 0, ONE, 0, 0 };

    if (!cam) {
        eye.vx = 0;
        eye.vy = world_to_psx(FIX16_FROM_INT(2));
        eye.vz = world_to_psx(FIX16_FROM_INT(6));
        at.vx = 0;
        at.vy = 0;
        at.vz = 0;
    } else {
        unity_pos_to_psx(cam->position, &eye);
        build_unity_camera_matrix(&eye, cam->rotation[0], cam->rotation[1],
                                  cam->rotation[2], &s_cam_mtx);
        return;
    }

    LookAt(&eye, &at, &up, &s_cam_mtx);
}

static void set_camera(const ps1_entity* cam) {
    build_camera_matrix(cam);
    if (cam && cam->has_camera) {
        s_geom_screen = fov_to_geom_screen(cam->camera_fov);
        s_near_z = world_to_psx(cam->camera_near);
        if (s_near_z < 8)
            s_near_z = 8;
        s_far_z = world_to_psx(cam->camera_far);
        if (s_far_z < s_near_z + 64)
            s_far_z = s_near_z + 64;
    } else {
        s_geom_screen = s_screen_w / 2;
        s_near_z = 16;
        s_far_z = 6400;
    }
    gte_SetGeomScreen(s_geom_screen);
}

#if PS1_PROJECTION_DEBUG
static void draw_debug_projected_point(char** pri, const char* packet_end, uint32_t* ot,
                                       const SVECTOR* local, uint8_t r, uint8_t g, uint8_t b) {
    int32_t sxy;
    int32_t sz;
    int x, y;
    TILE* tile;

    gte_ldv0((SVECTOR*)local);
    gte_rtps();
    gte_stsz(&sz);
    if (sz <= 0)
        return;
    gte_stsxy(&sxy);
    x = (int16_t)(sxy & 0xffff);
    y = (int16_t)((sxy >> 16) & 0xffff);
    if (x < -8 || x > s_screen_w + 8 || y < -8 || y > s_screen_h + 8)
        return;
    if ((const char*)(*pri + sizeof(TILE)) > packet_end)
        return;
    tile = (TILE*)*pri;
    setTile(tile);
    setXY0(tile, x - 2, y - 2);
    setWH(tile, 5, 5);
    setRGB0(tile, r, g, b);
    addPrim(ot + (OT_LEN - 2), tile);
    *pri = (char*)(tile + 1);
}

static void draw_custom_projection_debug(char** pri, const char* packet_end, uint32_t* ot) {
    static const SVECTOR origin = { 0, 0, 0, 0 };
    static const SVECTOR plus_x = { 1024, 0, 0, 0 };
    static const SVECTOR minus_x = { -1024, 0, 0, 0 };
    draw_debug_projected_point(pri, packet_end, ot, &origin, 255, 0, 0);
    draw_debug_projected_point(pri, packet_end, ot, &plus_x, 0, 255, 0);
    draw_debug_projected_point(pri, packet_end, ot, &minus_x, 0, 96, 255);
}
#endif

static const ps1_rigid_anim_frame* entity_rigid_frame(const ps1_entity* ent) {
    uint32_t index;
    if (!ent || ent->rigid_anim_frame_count == 0 || ent->rigid_anim_fps == 0)
        return 0;
    if ((uint32_t)ent->rigid_anim_first_frame + (uint32_t)ent->rigid_anim_current_frame >=
        g_ps1_scene.rigid_anim_frame_count)
        return 0;
    index = (uint32_t)ent->rigid_anim_first_frame + (uint32_t)ent->rigid_anim_current_frame;
    return &g_ps1_scene.rigid_anim_frames[index];
}

static const ps1_rigid_anim_frame* entity_rigid_next_frame(const ps1_entity* ent) {
    uint32_t index;
    if (!ent || ent->rigid_anim_frame_count <= 1 || ent->rigid_anim_fps == 0)
        return 0;
    if ((uint32_t)ent->rigid_anim_first_frame + (uint32_t)ent->rigid_anim_next_frame >=
        g_ps1_scene.rigid_anim_frame_count)
        return 0;
    index = (uint32_t)ent->rigid_anim_first_frame + (uint32_t)ent->rigid_anim_next_frame;
    return &g_ps1_scene.rigid_anim_frames[index];
}

static fix16_t lerp_fix16_q8(fix16_t a, fix16_t b, uint8_t t) {
    return (fix16_t)((int32_t)a + (int32_t)(((int64_t)((int32_t)b - (int32_t)a) * (int64_t)t) >> 8));
}

static int16_t lerp_i16_q8(int16_t a, int16_t b, uint8_t t) {
    return (int16_t)((int32_t)a + (((int32_t)b - (int32_t)a) * (int32_t)t >> 8));
}

static int draw_entity(char** pri, const char* packet_end, uint32_t* ot, const ps1_entity* ent,
                       int view_model_soft) {
    SVECTOR rotvec;
    VECTOR  pos, scale;
    MATRIX  rmtx, omtx, lmtx, composed;
    const ps1_rigid_anim_frame* rigid_frame;
    int     fi, otz;
    uint8_t base_r, base_g, base_b;
    uint8_t rigid_light_colors[27][3];
    int     added = 0;

    rigid_frame = entity_rigid_frame(ent);
    if (rigid_frame) {
        const ps1_rigid_anim_frame* rigid_next = entity_rigid_next_frame(ent);
        fix16_t rigid_pos[3];
        const ps1_entity* rigid_root = 0;
        const ps1_entity* rigid_root_source = 0;
        uint8_t lerp = rigid_next ? ent->rigid_anim_lerp_q8 : 0u;
        if (lerp) {
            rigid_pos[0] = lerp_fix16_q8(rigid_frame->position[0], rigid_next->position[0], lerp);
            rigid_pos[1] = lerp_fix16_q8(rigid_frame->position[1], rigid_next->position[1], lerp);
            rigid_pos[2] = lerp_fix16_q8(rigid_frame->position[2], rigid_next->position[2], lerp);
        } else {
            rigid_pos[0] = rigid_frame->position[0];
            rigid_pos[1] = rigid_frame->position[1];
            rigid_pos[2] = rigid_frame->position[2];
        }
        rmtx.m[0][0] = lerp ? lerp_i16_q8(rigid_frame->matrix[0][0], rigid_next->matrix[0][0], lerp) : rigid_frame->matrix[0][0];
        rmtx.m[0][1] = lerp ? lerp_i16_q8(rigid_frame->matrix[0][1], rigid_next->matrix[0][1], lerp) : rigid_frame->matrix[0][1];
        rmtx.m[0][2] = lerp ? lerp_i16_q8(rigid_frame->matrix[0][2], rigid_next->matrix[0][2], lerp) : rigid_frame->matrix[0][2];
        rmtx.m[1][0] = lerp ? lerp_i16_q8(rigid_frame->matrix[1][0], rigid_next->matrix[1][0], lerp) : rigid_frame->matrix[1][0];
        rmtx.m[1][1] = lerp ? lerp_i16_q8(rigid_frame->matrix[1][1], rigid_next->matrix[1][1], lerp) : rigid_frame->matrix[1][1];
        rmtx.m[1][2] = lerp ? lerp_i16_q8(rigid_frame->matrix[1][2], rigid_next->matrix[1][2], lerp) : rigid_frame->matrix[1][2];
        rmtx.m[2][0] = lerp ? lerp_i16_q8(rigid_frame->matrix[2][0], rigid_next->matrix[2][0], lerp) : rigid_frame->matrix[2][0];
        rmtx.m[2][1] = lerp ? lerp_i16_q8(rigid_frame->matrix[2][1], rigid_next->matrix[2][1], lerp) : rigid_frame->matrix[2][1];
        rmtx.m[2][2] = lerp ? lerp_i16_q8(rigid_frame->matrix[2][2], rigid_next->matrix[2][2], lerp) : rigid_frame->matrix[2][2];

        if (ent->rigid_root_entity_index >= 0) {
            rigid_root = ps1_scene_entity((unsigned int)ent->rigid_root_entity_index);
            rigid_root_source = ps1_scene_source_entity((unsigned int)ent->rigid_root_entity_index);
        }
        if (rigid_root && rigid_root_source) {
            fix16_t sy;
            fix16_t cy;
            const fix16_t local_x = rigid_pos[0] - rigid_root_source->position[0];
            const fix16_t local_y = rigid_pos[1] - rigid_root_source->position[1];
            const fix16_t local_z = rigid_pos[2] - rigid_root_source->position[2];
            MATRIX rooted_mtx;

            if (!s_rigid_root_transform.valid ||
                s_rigid_root_transform.root_index != ent->rigid_root_entity_index) {
                const fix16_t delta_yaw =
                    rigid_root->rotation[1] - rigid_root_source->rotation[1];
                const fix16_t turn = fix16_div(delta_yaw, FIX16_FROM_INT(360));
                SVECTOR delta_rot = { 0, fix16_to_q12(turn), 0, 0 };
                s_rigid_root_transform.valid = 1;
                s_rigid_root_transform.root_index = ent->rigid_root_entity_index;
                s_rigid_root_transform.sin_yaw = fix16_sin(turn);
                s_rigid_root_transform.cos_yaw = fix16_cos(turn);
                RotMatrix(&delta_rot, &s_rigid_root_transform.matrix);
            }
            sy = s_rigid_root_transform.sin_yaw;
            cy = s_rigid_root_transform.cos_yaw;

            rigid_pos[0] = rigid_root->position[0] +
                fix16_add(fix16_mul(cy, local_x), fix16_mul(sy, local_z));
            rigid_pos[1] = rigid_root->position[1] + local_y;
            rigid_pos[2] = rigid_root->position[2] +
                fix16_sub(fix16_mul(cy, local_z), fix16_mul(sy, local_x));

            MulMatrix0(&s_rigid_root_transform.matrix, &rmtx, &rooted_mtx);
            rmtx = rooted_mtx;
        }

        unity_pos_to_psx(rigid_pos, &pos);
        rmtx.t[0] = pos.vx;
        rmtx.t[1] = pos.vy;
        rmtx.t[2] = pos.vz;
    } else {
        unity_euler_to_psx_object(ent->rotation[0], ent->rotation[1], ent->rotation[2], &rotvec);
        unity_pos_to_psx(ent->position, &pos);
        RotMatrix(&rotvec, &rmtx);
    }

    if (ent->mesh == PS1_MESH_PLANE) {
        /* Plane: avoid ScaleMatrix overflow by baking extents into vertices. */
        scale.vx = ONE;
        scale.vy = ONE;
        scale.vz = ONE;
    } else if (ent->mesh == PS1_MESH_CUSTOM) {
        /* Custom verts are normalized for GTE precision; mesh scale restores
         * the source FBX size, then entity scale is applied on top. */
        int mesh_scale = 128; /* Legacy normalized half-size: 1024 * 128 / 4096 = 32 PSX units. */
        if (ent->mesh_index > 0 && ent->mesh_index <= g_ps1_mesh_count)
            mesh_scale = g_ps1_meshes[ent->mesh_index - 1u].scale_q12;
        if (rigid_frame) {
            scale.vx = mesh_scale;
            scale.vy = mesh_scale;
            scale.vz = mesh_scale;
        } else {
            scale.vx = (int)((int64_t)ent->scale[0] * mesh_scale >> 16);
            scale.vy = (int)((int64_t)ent->scale[1] * mesh_scale >> 16);
            scale.vz = (int)((int64_t)ent->scale[2] * mesh_scale >> 16);
        }
        if (scale.vx == 0) scale.vx = 1;
        if (scale.vy == 0) scale.vy = 1;
        if (scale.vz == 0) scale.vz = 1;
    } else if (ent->mesh == PS1_MESH_SPHERE) {
        /* Keep primitive spheres spherical on PS1. Unity-side non-uniform
         * scale can project as a tall spike/needle with the low-poly fallback,
         * especially when Z becomes screen vertical under the active camera. */
        const int sphere_scale = primitive_axis_scale_psx(ent, 1);
        scale.vx = sphere_scale;
        scale.vy = sphere_scale;
        scale.vz = sphere_scale;
    } else {
        scale.vx = primitive_axis_scale_psx(ent, 0);
        scale.vy = primitive_axis_scale_psx(ent, 1);
        scale.vz = primitive_axis_scale_psx(ent, 2);
    }

    omtx = rmtx;
    /* IMPORTANT: ScaleMatrix scales an existing matrix in-place.
     * Using a separate, uninitialized matrix here will corrupt transforms. */
    if (ent->mesh != PS1_MESH_PLANE)
        ScaleMatrix(&omtx, &scale);
    TransMatrix(&omtx, &pos);
    /* Lighting should NOT be affected by non-uniform scale. */
    MulMatrix0(&s_light_mtx, &rmtx, &lmtx);
    CompMatrixLV(&s_cam_mtx, &omtx, &composed);

    gte_SetRotMatrix(&composed);
    gte_SetTransMatrix(&composed);
    gte_SetLightMatrix(&lmtx);

#if PS1_PROJECTION_DEBUG
    if (ent->mesh == PS1_MESH_CUSTOM)
        draw_custom_projection_debug(pri, packet_end, ot);
#endif

    base_r = ent->color_r;
    base_g = ent->color_g;
    base_b = ent->color_b;
    if (rigid_frame) {
        int light_bin;
        for (light_bin = 0; light_bin < 27; ++light_bin) {
            SVECTOR normal;
            rigid_light_bin_normal(light_bin, &normal);
            gte_shade_normal_vec(
                &normal, base_r, base_g, base_b,
                &rigid_light_colors[light_bin][0],
                &rigid_light_colors[light_bin][1],
                &rigid_light_colors[light_bin][2]);
        }
    }

    if (ent->mesh == PS1_MESH_CUSTOM && ent->mesh_index > 0) {
        const unsigned int idx = (unsigned int)(ent->mesh_index - 1);
        if (idx < g_ps1_mesh_count) {
            const ps1_mesh* m = &g_ps1_meshes[idx];
            const ps1_mesh* m_next = 0;
            const int use_vertex_lerp = entity_vertex_lerp_mesh(ent, m, &m_next);
            const int use_tex =
                ent->texture_index > 0 && m->uvs && ent->texture_index <= g_ps1_texture_count;
            const int is_terrain = (m->flags & PS1_MESH_FLAG_TERRAIN) != 0;
            const int is_smooth = (m->flags & PS1_MESH_FLAG_SMOOTH) != 0;
            int smooth_object_otz_floor = 0;
            const uint16_t tpage =
                use_tex ? ps1_texture_tpage((unsigned int)ent->texture_index) : 0;
            const int tex_uoff =
                use_tex ? (int)ps1_texture_u_offset((unsigned int)ent->texture_index) : 0;
            const int tex_voff =
                use_tex ? (int)ps1_texture_v_offset((unsigned int)ent->texture_index) : 0;
            if (is_smooth && !is_terrain && !rigid_frame) {
                static const SVECTOR origin = { 0, 0, 0, 0 };
                VECTOR center_camera;
                transform_point(&composed, &origin, &center_camera);
                smooth_object_otz_floor = (int)(center_camera.vz >> 3);
                if (smooth_object_otz_floor < PS1_WORLD_OT_MIN)
                    smooth_object_otz_floor = PS1_WORLD_OT_MIN;
                else if (smooth_object_otz_floor >= OT_LEN)
                    smooth_object_otz_floor = OT_LEN - 1;
            }
            const int viewmodel_cached =
                ent->view_model && m->vert_count <= PS1_VIEWMODEL_VERTEX_CACHE_MAX;
            const int rigid_cached =
                rigid_frame && m->position_refs &&
                m->vert_count <= PS1_RIGID_VERTEX_CACHE_MAX;
            const int static_cache_eligible =
                !ent->view_model && !rigid_frame &&
                m->vert_count <= PS1_STATIC_VERTEX_CACHE_MAX;
            ps1_static_entity_cache* static_cache = static_cache_eligible
                ? static_entity_cache_for(ent, m) : 0;
            const int static_cached = static_cache != 0;
            const uint16_t static_vertex_offset = static_cached
                ? static_cache->vertex_offset : 0u;
            const uint16_t static_triangle_offset = static_cached
                ? static_cache->triangle_offset : 0u;
            const int vm_near_z = ent->view_model ? viewmodel_near_z() : s_near_z;

            if (viewmodel_cached) {
                uint16_t vi;
                for (vi = 0; vi < m->vert_count; ++vi) {
                    transform_point(&composed, &m->verts[vi], &s_viewmodel_vertex_cache[vi]);
                    project_vector_vert(&s_viewmodel_vertex_cache[vi],
                                        &s_viewmodel_screen_x[vi],
                                        &s_viewmodel_screen_y[vi],
                                        vm_near_z);
                }
            }
            if (rigid_cached) {
                uint16_t vi;
                uint16_t batch_indices[3];
                int batch_count = 0;
                for (vi = 0; vi < m->vert_count; ++vi) {
                    if (m->position_refs[vi] != vi)
                        continue;
                    batch_indices[batch_count++] = vi;
                    if (batch_count == 3) {
                        int32_t sxy[3];
                        int32_t depth[3];
                        gte_ldv3(&m->verts[batch_indices[0]],
                                 &m->verts[batch_indices[1]],
                                 &m->verts[batch_indices[2]]);
                        gte_rtpt();
                        gte_stsxy0(&sxy[0]);
                        gte_stsxy1(&sxy[1]);
                        gte_stsxy2(&sxy[2]);
                        gte_stsz3(&depth[0], &depth[1], &depth[2]);
                        s_rigid_screen_cache[batch_indices[0]] = sxy[0];
                        s_rigid_screen_cache[batch_indices[1]] = sxy[1];
                        s_rigid_screen_cache[batch_indices[2]] = sxy[2];
                        s_rigid_depth_cache[batch_indices[0]] = depth[0];
                        s_rigid_depth_cache[batch_indices[1]] = depth[1];
                        s_rigid_depth_cache[batch_indices[2]] = depth[2];
                        batch_count = 0;
                    }
                }
                for (vi = 0; vi < (uint16_t)batch_count; ++vi) {
                    int32_t sxy;
                    int32_t depth;
                    const uint16_t index = batch_indices[vi];
                    gte_ldv0(&m->verts[index]);
                    gte_rtps();
                    gte_stsxy(&sxy);
                    gte_stsz(&depth);
                    s_rigid_screen_cache[index] = sxy;
                    s_rigid_depth_cache[index] = depth;
                }
            }
            if (static_cached) {
                uint16_t vi;
                const int projection_dirty =
                    !static_cache->projection_valid ||
                    static_cache->mesh != m ||
                    static_cache->next_mesh != m_next ||
                    static_cache->vertex_lerp_q8 != ent->vertex_anim_lerp_q8 ||
                    static_cache->geom_screen != s_geom_screen ||
                    !matrix_equal(&static_cache->projected_matrix, &composed);
                const int lighting_dirty =
                    !static_cache->lighting_valid ||
                    static_cache->mesh != m ||
                    static_cache->base_r != base_r ||
                    static_cache->base_g != base_g ||
                    static_cache->base_b != base_b ||
                    !matrix_equal(&static_cache->light_matrix, &lmtx);
                if (projection_dirty) {
                    for (vi = 0; vi + 2u < m->vert_count; vi += 3u) {
                    int32_t sxy0, sxy1, sxy2;
                    int32_t z0, z1, z2;
                    SVECTOR interpolated[3];
                    const SVECTOR* v0 = &m->verts[vi];
                    const SVECTOR* v1 = &m->verts[vi + 1u];
                    const SVECTOR* v2 = &m->verts[vi + 2u];
                    if (use_vertex_lerp) {
                        interpolated[0] = lerp_svector_q8(
                            v0, &m_next->verts[vi], ent->vertex_anim_lerp_q8);
                        interpolated[1] = lerp_svector_q8(
                            v1, &m_next->verts[vi + 1u], ent->vertex_anim_lerp_q8);
                        interpolated[2] = lerp_svector_q8(
                            v2, &m_next->verts[vi + 2u], ent->vertex_anim_lerp_q8);
                        v0 = &interpolated[0];
                        v1 = &interpolated[1];
                        v2 = &interpolated[2];
                    }
                    gte_ldv3(v0, v1, v2);
                    gte_rtpt();
                    gte_stsxy0(&sxy0);
                    gte_stsxy1(&sxy1);
                    gte_stsxy2(&sxy2);
                    gte_stsz3(&z0, &z1, &z2);
                    s_static_screen_cache[static_vertex_offset + vi] = sxy0;
                    s_static_screen_cache[static_vertex_offset + vi + 1u] = sxy1;
                    s_static_screen_cache[static_vertex_offset + vi + 2u] = sxy2;
                    s_static_depth_cache[static_vertex_offset + vi] = z0;
                    s_static_depth_cache[static_vertex_offset + vi + 1u] = z1;
                    s_static_depth_cache[static_vertex_offset + vi + 2u] = z2;
                    }
                    for (; vi < m->vert_count; ++vi) {
                    int32_t sxy;
                    int32_t depth;
                    SVECTOR interpolated;
                    const SVECTOR* vertex = &m->verts[vi];
                    if (use_vertex_lerp) {
                        interpolated = lerp_svector_q8(
                            vertex, &m_next->verts[vi], ent->vertex_anim_lerp_q8);
                        vertex = &interpolated;
                    }
                    gte_ldv0(vertex);
                    gte_rtps();
                    gte_stsxy(&sxy);
                    gte_stsz(&depth);
                    s_static_screen_cache[static_vertex_offset + vi] = sxy;
                    s_static_depth_cache[static_vertex_offset + vi] = depth;
                    }
                    static_cache->projected_matrix = composed;
                    static_cache->geom_screen = s_geom_screen;
                    static_cache->next_mesh = m_next;
                    static_cache->vertex_lerp_q8 = ent->vertex_anim_lerp_q8;
                    static_cache->projection_valid = 1;
                }
                if (lighting_dirty && is_smooth && !is_terrain && m->norms) {
                    for (vi = 0; vi < m->vert_count; ++vi) {
                        if (use_tex) {
                            gte_shade_textured_smooth_normal(
                                &m->norms[vi], base_r, base_g, base_b,
                                &s_static_light_cache[static_vertex_offset + vi][0],
                                &s_static_light_cache[static_vertex_offset + vi][1],
                                &s_static_light_cache[static_vertex_offset + vi][2]);
                        } else {
                            gte_shade_normal(
                                &m->norms[vi], base_r, base_g, base_b,
                                &s_static_light_cache[static_vertex_offset + vi][0],
                                &s_static_light_cache[static_vertex_offset + vi][1],
                                &s_static_light_cache[static_vertex_offset + vi][2]);
                        }
                    }
                }
                if (lighting_dirty && !is_smooth && !is_terrain) {
                    uint16_t ti;
                    for (ti = 0; ti < m->tri_count; ++ti) {
                        const ps1_mesh_tri* source = &m->tris[ti];
                        SVECTOR normal = { source->nx, source->ny, source->nz, 0 };
                        uint8_t cd[4] = { 128, 128, 128, 0 };
                        uint8_t r, g, b;
                        if (normal.vx == 0 && normal.vy == 0 && normal.vz == 0)
                            normal.vy = 3800;
                        gte_ldrgb(cd);
                        gte_ldv0(&normal);
                        gte_ncs();
                        gte_strgb(cd);
                        r = cd[0]; g = cd[1]; b = cd[2];
                        if (r < 64) r = 64; else r = (uint8_t)(((uint16_t)r * 3 + 80) >> 2);
                        if (g < 64) g = 64; else g = (uint8_t)(((uint16_t)g * 3 + 80) >> 2);
                        if (b < 64) b = 64; else b = (uint8_t)(((uint16_t)b * 3 + 80) >> 2);
                        s_static_face_light_cache[static_triangle_offset + ti][0] =
                            (uint8_t)(((uint16_t)r * base_r) >> 8);
                        s_static_face_light_cache[static_triangle_offset + ti][1] =
                            (uint8_t)(((uint16_t)g * base_g) >> 8);
                        s_static_face_light_cache[static_triangle_offset + ti][2] =
                            (uint8_t)(((uint16_t)b * base_b) >> 8);
                    }
                }
                if (lighting_dirty) {
                    static_cache->light_matrix = lmtx;
                    static_cache->base_r = base_r;
                    static_cache->base_g = base_g;
                    static_cache->base_b = base_b;
                    static_cache->lighting_valid = 1;
                }
                static_cache->mesh = m;
            }

            if (use_tex && tpage && (const char*)(*pri + sizeof(DR_TPAGE)) <= packet_end) {
                DR_TPAGE* dr = (DR_TPAGE*)*pri;
                setDrawTPage(dr, 0, 0, tpage);
                addPrim(ot + (OT_LEN - 2), dr);
                *pri = (char*)(dr + 1);
            }

            if (rigid_cached) {
                const int screen_right = s_screen_w + 64;
                const int screen_bottom = s_screen_h + 64;
                uint16_t ti;
                for (ti = 0; ti < m->tri_count; ++ti) {
                    const ps1_mesh_tri* t = &m->tris[ti];
                    const uint16_t p0 = m->position_refs[t->i0];
                    const uint16_t p1 = m->position_refs[t->i1];
                    const uint16_t p2 = m->position_refs[t->i2];
                    const int32_t sxy0 = s_rigid_screen_cache[p0];
                    const int32_t sxy1 = s_rigid_screen_cache[p1];
                    const int32_t sxy2 = s_rigid_screen_cache[p2];
                    const int32_t z0 = s_rigid_depth_cache[p0];
                    const int32_t z1 = s_rigid_depth_cache[p1];
                    const int32_t z2 = s_rigid_depth_cache[p2];
                    int16_t x0 = (int16_t)(sxy0 & 0xffff);
                    int16_t y0 = (int16_t)(sxy0 >> 16);
                    int16_t x1 = (int16_t)(sxy1 & 0xffff);
                    int16_t y1 = (int16_t)(sxy1 >> 16);
                    int16_t x2 = (int16_t)(sxy2 & 0xffff);
                    int16_t y2 = (int16_t)(sxy2 >> 16);
                    const int32_t area =
                        (int32_t)(x1 - x0) * (int32_t)(y2 - y0) -
                        (int32_t)(y1 - y0) * (int32_t)(x2 - x0);
                    const uint8_t light_bin = t->shade < 27 ? t->shade : 13;
                    int rigid_otz;

                    if (z0 <= 0 || z1 <= 0 || z2 <= 0 || area >= 0)
                        continue;
                    if ((x0 < -64 && x1 < -64 && x2 < -64) ||
                        (x0 > screen_right && x1 > screen_right && x2 > screen_right) ||
                        (y0 < -64 && y1 < -64 && y2 < -64) ||
                        (y0 > screen_bottom && y1 > screen_bottom && y2 > screen_bottom))
                        continue;

                    /* Rigid bones are separate meshes. Sorting an entire bone
                     * at its origin lets legs overwrite a skirt even when the
                     * skirt triangles are closer to the camera. Keep the fast
                     * transformed-vertex cache, but place every triangle in
                     * the OT using its actual average camera depth. */
                    rigid_otz = (int)((z0 + z1 + z2) / 3) >> 4;
                    if (rigid_otz < PS1_WORLD_OT_MIN)
                        rigid_otz = PS1_WORLD_OT_MIN;
                    else if (rigid_otz >= OT_LEN)
                        rigid_otz = OT_LEN - 1;

                    if (use_tex && tpage) {
                        uint8_t fog_r = rigid_light_colors[light_bin][0];
                        uint8_t fog_g = rigid_light_colors[light_bin][1];
                        uint8_t fog_b = rigid_light_colors[light_bin][2];
                        POLY_FT3* tri;
                        if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                            return added;
                        tri = (POLY_FT3*)*pri;
                        setPolyFT3(tri);
                        setXY3(tri, x0, y0, x1, y1, x2, y2);
                        apply_scene_fog((z0 + z1 + z2) / 3, &fog_r, &fog_g, &fog_b);
                        setRGB0(tri, fog_r, fog_g, fog_b);
                        tri->tpage = tpage;
                        tri->clut = 0;
                        setUV3(tri,
                               clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                               clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                               clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                        addPrim(ot + rigid_otz, tri);
                        *pri = (char*)(tri + 1);
                    } else {
                        uint8_t fog_r = rigid_light_colors[light_bin][0];
                        uint8_t fog_g = rigid_light_colors[light_bin][1];
                        uint8_t fog_b = rigid_light_colors[light_bin][2];
                        POLY_F3* tri;
                        if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                            return added;
                        tri = (POLY_F3*)*pri;
                        setPolyF3(tri);
                        setXY3(tri, x0, y0, x1, y1, x2, y2);
                        apply_scene_fog((z0 + z1 + z2) / 3, &fog_r, &fog_g, &fog_b);
                        setRGB0(tri, fog_r, fog_g, fog_b);
                        addPrim(ot + rigid_otz, tri);
                        *pri = (char*)(tri + 1);
                    }
                    ++added;
                }
                return added;
            }

            for (uint16_t ti = 0; ti < m->tri_count; ++ti) {
                const ps1_mesh_tri* t = &m->tris[ti];
                SVECTOR faceN;
                uint8_t r, g, b;
                int32_t p, nclip_val;
                int16_t tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2;
                if (t->i0 >= m->vert_count || t->i1 >= m->vert_count || t->i2 >= m->vert_count)
                    continue;

                {
                    SVECTOR lerp0, lerp1, lerp2;
                    const SVECTOR* v0 = &m->verts[t->i0];
                    const SVECTOR* v1 = &m->verts[t->i1];
                    const SVECTOR* v2 = &m->verts[t->i2];
                    if (use_vertex_lerp) {
                        lerp0 = lerp_svector_q8(v0, &m_next->verts[t->i0], ent->vertex_anim_lerp_q8);
                        lerp1 = lerp_svector_q8(v1, &m_next->verts[t->i1], ent->vertex_anim_lerp_q8);
                        lerp2 = lerp_svector_q8(v2, &m_next->verts[t->i2], ent->vertex_anim_lerp_q8);
                        v0 = &lerp0;
                        v1 = &lerp1;
                        v2 = &lerp2;
                    }

                if (ent->view_model && view_model_soft) {
                    ps1_clip_vert src[3];
                    if (viewmodel_cached) {
                        make_cached_clip_vert(&s_viewmodel_vertex_cache[t->i0],
                                              (use_tex && m->uvs) ? &m->uvs[t->i0] : 0, &src[0]);
                        make_cached_clip_vert(&s_viewmodel_vertex_cache[t->i1],
                                              (use_tex && m->uvs) ? &m->uvs[t->i1] : 0, &src[1]);
                        make_cached_clip_vert(&s_viewmodel_vertex_cache[t->i2],
                                              (use_tex && m->uvs) ? &m->uvs[t->i2] : 0, &src[2]);
                    } else {
                        make_clip_vert(&composed, &m->verts[t->i0],
                                       (use_tex && m->uvs) ? &m->uvs[t->i0] : 0, &src[0]);
                        make_clip_vert(&composed, &m->verts[t->i1],
                                       (use_tex && m->uvs) ? &m->uvs[t->i1] : 0, &src[1]);
                        make_clip_vert(&composed, &m->verts[t->i2],
                                       (use_tex && m->uvs) ? &m->uvs[t->i2] : 0, &src[2]);
                    }

                    if (src[0].p.vz >= vm_near_z &&
                        src[1].p.vz >= vm_near_z &&
                        src[2].p.vz >= vm_near_z &&
                        !camera_space_front_facing(src))
                        continue;

                    if (src[0].p.vz >= vm_near_z &&
                        src[1].p.vz >= vm_near_z &&
                        src[2].p.vz >= vm_near_z) {
                        int16_t x0, y0, x1, y1, x2, y2;
                        if (viewmodel_cached) {
                            x0 = s_viewmodel_screen_x[t->i0];
                            y0 = s_viewmodel_screen_y[t->i0];
                            x1 = s_viewmodel_screen_x[t->i1];
                            y1 = s_viewmodel_screen_y[t->i1];
                            x2 = s_viewmodel_screen_x[t->i2];
                            y2 = s_viewmodel_screen_y[t->i2];
                        } else {
                            project_clip_vert(&src[0], &x0, &y0, vm_near_z);
                            project_clip_vert(&src[1], &x1, &y1, vm_near_z);
                            project_clip_vert(&src[2], &x2, &y2, vm_near_z);
                        }
                        if (triangle_outside_screen(x0, y0, x1, y1, x2, y2))
                            continue;
                        if (!screen_front_facing(x0, y0, x1, y1, x2, y2))
                            continue;

                        p = (src[0].p.vz + src[1].p.vz + src[2].p.vz) / 3;
                        if (p <= 0)
                            continue;
                        otz = viewmodel_triangle_otz(p);

                        faceN.vx = t->nx;
                        faceN.vy = t->ny;
                        faceN.vz = t->nz;
                        faceN.pad = 0;
                        if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                            faceN.vy = 3800;

                        viewmodel_cpu_shade_normal(&lmtx, &faceN, base_r, base_g, base_b, &r, &g, &b);

                        if (use_tex && tpage) {
                            if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                                return added;
                            {
                                POLY_FT3* tri = (POLY_FT3*)*pri;
                                setPolyFT3(tri);
                                setXY3(tri, x0, y0, x1, y1, x2, y2);
                                setRGB0(tri, r, g, b);
                                tri->tpage = tpage;
                                tri->clut = 0;
                                setUV3(tri,
                                       clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                                       clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                                       clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                                addPrim(ot + otz, tri);
                                *pri = (char*)(tri + 1);
                                ++added;
                            }
                        } else {
                            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                return added;
                            {
                                POLY_F3* tri = (POLY_F3*)*pri;
                                setPolyF3(tri);
                                setXY3(tri, x0, y0, x1, y1, x2, y2);
                                setRGB0(tri, r, g, b);
                                addPrim(ot + otz, tri);
                                *pri = (char*)(tri + 1);
                                ++added;
                            }
                        }
                        continue;
                    }

                    {
                        ps1_clip_vert clipped[4];
                        int clipped_count = clip_triangle_near(src, clipped, vm_near_z);
                        int qi;
                        if (clipped_count < 3)
                            continue;

                        faceN.vx = t->nx;
                        faceN.vy = t->ny;
                        faceN.vz = t->nz;
                        faceN.pad = 0;
                        if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                            faceN.vy = 3800;
                        viewmodel_cpu_shade_normal(&lmtx, &faceN, base_r, base_g, base_b, &r, &g, &b);

                        for (qi = 0; qi < clipped_count - 2; ++qi) {
                            const ps1_clip_vert* c0 = &clipped[0];
                            const ps1_clip_vert* c1 = &clipped[qi + 1];
                            const ps1_clip_vert* c2 = &clipped[qi + 2];
                            int16_t x0, y0, x1, y1, x2, y2;
                            project_clip_vert(c0, &x0, &y0, vm_near_z);
                            project_clip_vert(c1, &x1, &y1, vm_near_z);
                            project_clip_vert(c2, &x2, &y2, vm_near_z);
                            if (triangle_outside_screen(x0, y0, x1, y1, x2, y2))
                                continue;
                            if (!screen_front_facing(x0, y0, x1, y1, x2, y2))
                                continue;

                            p = (c0->p.vz + c1->p.vz + c2->p.vz) / 3;
                            if (p <= 0)
                                continue;
                            otz = viewmodel_triangle_otz(p);

                            if (use_tex && tpage) {
                                if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                                    return added;
                                {
                                    POLY_FT3* tri = (POLY_FT3*)*pri;
                                    setPolyFT3(tri);
                                    setXY3(tri, x0, y0, x1, y1, x2, y2);
                                    setRGB0(tri, r, g, b);
                                    tri->tpage = tpage;
                                    tri->clut = 0;
                                    setUV3(tri,
                                           clamp_u8(tex_uoff + c0->u), clamp_u8(tex_voff + c0->v),
                                           clamp_u8(tex_uoff + c1->u), clamp_u8(tex_voff + c1->v),
                                           clamp_u8(tex_uoff + c2->u), clamp_u8(tex_voff + c2->v));
                                    addPrim(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                    ++added;
                                }
                            } else {
                                if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                    return added;
                                {
                                    POLY_F3* tri = (POLY_F3*)*pri;
                                    setPolyF3(tri);
                                    setXY3(tri, x0, y0, x1, y1, x2, y2);
                                    setRGB0(tri, r, g, b);
                                    addPrim(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                    ++added;
                                }
                            }
                        }
                        continue;
                    }
                }

                int is_backface = 0;
                if (static_cached) {
                    const int32_t sxy0 = s_static_screen_cache[static_vertex_offset + t->i0];
                    const int32_t sxy1 = s_static_screen_cache[static_vertex_offset + t->i1];
                    const int32_t sxy2 = s_static_screen_cache[static_vertex_offset + t->i2];
                    const int32_t z0 = s_static_depth_cache[static_vertex_offset + t->i0];
                    const int32_t z1 = s_static_depth_cache[static_vertex_offset + t->i1];
                    const int32_t z2 = s_static_depth_cache[static_vertex_offset + t->i2];
                    if (z0 < s_near_z || z1 < s_near_z || z2 < s_near_z)
                        continue;
                    tri_x0 = (int16_t)(sxy0 & 0xffff);
                    tri_y0 = (int16_t)((sxy0 >> 16) & 0xffff);
                    tri_x1 = (int16_t)(sxy1 & 0xffff);
                    tri_y1 = (int16_t)((sxy1 >> 16) & 0xffff);
                    tri_x2 = (int16_t)(sxy2 & 0xffff);
                    tri_y2 = (int16_t)((sxy2 >> 16) & 0xffff);
                    if (!screen_front_facing_strict(
                            tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2))
                        continue;
                    /* InitGeom's ZSF3 reduces the summed SZ values by 12.
                     * Computing the same average directly avoids one GTE
                     * command and three register loads per triangle. */
                    p = (z0 + z1 + z2) / 12;
                } else if (rigid_cached) {
                    const uint16_t p0 = m->position_refs[t->i0];
                    const uint16_t p1 = m->position_refs[t->i1];
                    const uint16_t p2 = m->position_refs[t->i2];
                    const int32_t sxy0 = s_rigid_screen_cache[p0];
                    const int32_t sxy1 = s_rigid_screen_cache[p1];
                    const int32_t sxy2 = s_rigid_screen_cache[p2];
                    const int32_t z0 = s_rigid_depth_cache[p0];
                    const int32_t z1 = s_rigid_depth_cache[p1];
                    const int32_t z2 = s_rigid_depth_cache[p2];
                    if (z0 <= 0 || z1 <= 0 || z2 <= 0)
                        continue;
                    tri_x0 = (int16_t)(sxy0 & 0xffff);
                    tri_y0 = (int16_t)((sxy0 >> 16) & 0xffff);
                    tri_x1 = (int16_t)(sxy1 & 0xffff);
                    tri_y1 = (int16_t)((sxy1 >> 16) & 0xffff);
                    tri_x2 = (int16_t)(sxy2 & 0xffff);
                    tri_y2 = (int16_t)((sxy2 >> 16) & 0xffff);
                    p = (z0 + z1 + z2) / 12;
                } else {
                    int32_t sxy0, sxy1, sxy2;
                    int32_t z0, z1, z2;
                    gte_ldv3(v0, v1, v2);
                    gte_rtpt();
                    gte_stsz3(&z0, &z1, &z2);
                    if (!ent->view_model && !rigid_frame &&
                        (z0 < s_near_z || z1 < s_near_z || z2 < s_near_z))
                        continue;
                    gte_nclip();
                    gte_stopz(&nclip_val);
                    if (nclip_val >= 0)
                        continue;
                    gte_avsz3();
                    gte_stotz(&p);
                    gte_stsxy0(&sxy0);
                    gte_stsxy1(&sxy1);
                    gte_stsxy2(&sxy2);
                    tri_x0 = (int16_t)(sxy0 & 0xffff);
                    tri_y0 = (int16_t)((sxy0 >> 16) & 0xffff);
                    tri_x1 = (int16_t)(sxy1 & 0xffff);
                    tri_y1 = (int16_t)((sxy1 >> 16) & 0xffff);
                    tri_x2 = (int16_t)(sxy2 & 0xffff);
                    tri_y2 = (int16_t)((sxy2 >> 16) & 0xffff);
                }
                if (p <= 0)
                    continue;
                if (triangle_outside_screen(
                        tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2))
                    continue;
                if (rigid_frame) {
                    int32_t area =
                        (int32_t)(tri_x1 - tri_x0) * (int32_t)(tri_y2 - tri_y0) -
                        (int32_t)(tri_y1 - tri_y0) * (int32_t)(tri_x2 - tri_x0);
                    /* Preserve a one-pixel front face at long range, but skip
                     * zero-area triangles which cannot rasterize and can
                     * otherwise flood the GPU packet. */
                    if (area >= 0)
                        continue;
                }
                otz = (is_smooth && !is_terrain && !rigid_frame)
                    ? mesh_triangle_otz_fine(p)
                    : mesh_triangle_otz(p);
                if (ent->view_model)
                    otz = viewmodel_triangle_otz(p);
                if (smooth_object_otz_floor > 0 && otz < smooth_object_otz_floor)
                    otz = smooth_object_otz_floor;

                /* Per-face GTE lighting using the exported face normal.
                 * For reversed-winding copies (back faces), the cross product
                 * in the exporter produces an inward-pointing normal. GTE
                 * will light it correctly facing the interior light source. */
                faceN.vx = t->nx;
                faceN.vy = t->ny;
                faceN.vz = t->nz;
                faceN.pad = 0;
                if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                    faceN.vy = 3800;


                if (use_tex && tpage) {
                    if (!rigid_frame && !is_terrain && is_smooth && m->norms) {
                        uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
                        POLY_GT3* tri;
                        if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                            return added;
                        tri = (POLY_GT3*)*pri;
                        setPolyGT3(tri);
                        setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                        if (static_cached) {
                            r0 = s_static_light_cache[static_vertex_offset + t->i0][0];
                            g0 = s_static_light_cache[static_vertex_offset + t->i0][1];
                            b0 = s_static_light_cache[static_vertex_offset + t->i0][2];
                            r1 = s_static_light_cache[static_vertex_offset + t->i1][0];
                            g1 = s_static_light_cache[static_vertex_offset + t->i1][1];
                            b1 = s_static_light_cache[static_vertex_offset + t->i1][2];
                            r2 = s_static_light_cache[static_vertex_offset + t->i2][0];
                            g2 = s_static_light_cache[static_vertex_offset + t->i2][1];
                            b2 = s_static_light_cache[static_vertex_offset + t->i2][2];
                        } else {
                            gte_shade_textured_smooth_normal(&m->norms[t->i0], base_r, base_g, base_b, &r0, &g0, &b0);
                            gte_shade_textured_smooth_normal(&m->norms[t->i1], base_r, base_g, base_b, &r1, &g1, &b1);
                            gte_shade_textured_smooth_normal(&m->norms[t->i2], base_r, base_g, base_b, &r2, &g2, &b2);
                        }
                        apply_scene_fog(p << 2, &r0, &g0, &b0);
                        apply_scene_fog(p << 2, &r1, &g1, &b1);
                        apply_scene_fog(p << 2, &r2, &g2, &b2);
                        tri->r0 = r0; tri->g0 = g0; tri->b0 = b0;
                        tri->r1 = r1; tri->g1 = g1; tri->b1 = b1;
                        tri->r2 = r2; tri->g2 = g2; tri->b2 = b2;
                        tri->tpage = tpage;
                        tri->clut = 0;
                        setUV3(tri,
                               clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                               clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                               clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                        addPrim(ot + otz, tri);
                        *pri = (char*)(tri + 1);
                        ++added;
                        continue;
                    }
                    if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                        return added;
                    {
                        POLY_FT3* tri = (POLY_FT3*)*pri;
                        setPolyFT3(tri);
                        setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);

                        if (static_cached && !is_terrain && !is_smooth) {
                            r = s_static_face_light_cache[static_triangle_offset + ti][0];
                            g = s_static_face_light_cache[static_triangle_offset + ti][1];
                            b = s_static_face_light_cache[static_triangle_offset + ti][2];
                        } else {
                            if (rigid_frame) {
                                const uint8_t light_bin = t->shade < 27 ? t->shade : 13;
                                r = rigid_light_colors[light_bin][0];
                                g = rigid_light_colors[light_bin][1];
                                b = rigid_light_colors[light_bin][2];
                            } else {
                                /* Inline NCS: load face normal, compute lighting via GTE. */
                                uint8_t cd[4];
                                cd[0] = 128; cd[1] = 128; cd[2] = 128; cd[3] = 0;
                                gte_ldrgb(cd);
                                gte_ldv0(&faceN);
                                gte_ncs();
                                gte_strgb(cd);
                                r = cd[0]; g = cd[1]; b = cd[2];
                            }
                            if (is_terrain) {
                                r = (uint8_t)(((uint16_t)t->shade * t->color_r) / 255u);
                                g = (uint8_t)(((uint16_t)t->shade * t->color_g) / 255u);
                                b = (uint8_t)(((uint16_t)t->shade * t->color_b) / 255u);
                            } else if (!rigid_frame) {
                                /* Lift shadows (half-Lambert) and modulate with base color. */
                                if (r < 64) r = 64; else r = (uint8_t)(((uint16_t)r * 3 + 80) >> 2);
                                if (g < 64) g = 64; else g = (uint8_t)(((uint16_t)g * 3 + 80) >> 2);
                                if (b < 64) b = 64; else b = (uint8_t)(((uint16_t)b * 3 + 80) >> 2);
                            }
                            if (!rigid_frame) {
                                r = (uint8_t)(((uint16_t)r * base_r) >> 8);
                                g = (uint8_t)(((uint16_t)g * base_g) >> 8);
                                b = (uint8_t)(((uint16_t)b * base_b) >> 8);
                            }
                        }
                        apply_scene_fog(p << 2, &r, &g, &b);
                        setRGB0(tri, r, g, b);

                        tri->tpage = tpage;
                        tri->clut = 0;
                        setUV3(tri,
                               clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                               clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                               clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                        addPrim(ot + otz, tri);
                        *pri = (char*)(tri + 1);
                        ++added;
                    }
                } else {
                    if (!rigid_frame && !is_terrain && is_smooth && m->norms) {
                        uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
                        POLY_G3* tri;
                        if ((const char*)(*pri + sizeof(POLY_G3)) > packet_end)
                            return added;
                        tri = (POLY_G3*)*pri;
                        setPolyG3(tri);
                        setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                        if (static_cached) {
                            r0 = s_static_light_cache[static_vertex_offset + t->i0][0];
                            g0 = s_static_light_cache[static_vertex_offset + t->i0][1];
                            b0 = s_static_light_cache[static_vertex_offset + t->i0][2];
                            r1 = s_static_light_cache[static_vertex_offset + t->i1][0];
                            g1 = s_static_light_cache[static_vertex_offset + t->i1][1];
                            b1 = s_static_light_cache[static_vertex_offset + t->i1][2];
                            r2 = s_static_light_cache[static_vertex_offset + t->i2][0];
                            g2 = s_static_light_cache[static_vertex_offset + t->i2][1];
                            b2 = s_static_light_cache[static_vertex_offset + t->i2][2];
                        } else {
                            gte_shade_normal(&m->norms[t->i0], base_r, base_g, base_b, &r0, &g0, &b0);
                            gte_shade_normal(&m->norms[t->i1], base_r, base_g, base_b, &r1, &g1, &b1);
                            gte_shade_normal(&m->norms[t->i2], base_r, base_g, base_b, &r2, &g2, &b2);
                        }
                        apply_scene_fog(p << 2, &r0, &g0, &b0);
                        apply_scene_fog(p << 2, &r1, &g1, &b1);
                        apply_scene_fog(p << 2, &r2, &g2, &b2);
                        tri->r0 = r0; tri->g0 = g0; tri->b0 = b0;
                        tri->r1 = r1; tri->g1 = g1; tri->b1 = b1;
                        tri->r2 = r2; tri->g2 = g2; tri->b2 = b2;
                        addPrim(ot + otz, tri);
                        *pri = (char*)(tri + 1);
                        ++added;
                        continue;
                    }
                    if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                        return added;
                    {
                        POLY_F3* tri = (POLY_F3*)*pri;
                        setPolyF3(tri);
                        setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);

                        if (static_cached && !is_terrain && !is_smooth) {
                            r = s_static_face_light_cache[static_triangle_offset + ti][0];
                            g = s_static_face_light_cache[static_triangle_offset + ti][1];
                            b = s_static_face_light_cache[static_triangle_offset + ti][2];
                        } else {
                            if (rigid_frame) {
                                const uint8_t light_bin = t->shade < 27 ? t->shade : 13;
                                r = rigid_light_colors[light_bin][0];
                                g = rigid_light_colors[light_bin][1];
                                b = rigid_light_colors[light_bin][2];
                            } else {
                                /* Flat-shaded: inline NCS + modulate with entity base color. */
                                uint8_t cd[4];
                                cd[0] = 128; cd[1] = 128; cd[2] = 128; cd[3] = 0;
                                gte_ldrgb(cd);
                                gte_ldv0(&faceN);
                                gte_ncs();
                                gte_strgb(cd);
                                r = cd[0]; g = cd[1]; b = cd[2];
                            }
                            if (is_terrain) {
                                r = (uint8_t)(((uint16_t)t->shade * t->color_r) / 255u);
                                g = (uint8_t)(((uint16_t)t->shade * t->color_g) / 255u);
                                b = (uint8_t)(((uint16_t)t->shade * t->color_b) / 255u);
                            } else if (!rigid_frame) {
                                if (r < 64) r = 64; else r = (uint8_t)(((uint16_t)r * 3 + 80) >> 2);
                                if (g < 64) g = 64; else g = (uint8_t)(((uint16_t)g * 3 + 80) >> 2);
                                if (b < 64) b = 64; else b = (uint8_t)(((uint16_t)b * 3 + 80) >> 2);
                            }
                            if (!rigid_frame) {
                                r = (uint8_t)(((uint16_t)r * base_r) >> 8);
                                g = (uint8_t)(((uint16_t)g * base_g) >> 8);
                                b = (uint8_t)(((uint16_t)b * base_b) >> 8);
                            }
                        }

                        apply_scene_fog(p << 2, &r, &g, &b);
                        setRGB0(tri, r, g, b);
                        addPrim(ot + otz, tri);
                        *pri = (char*)(tri + 1);
                        ++added;
                    }
                }
                }
            }
        }
        return added;
    }

    if (ent->mesh == PS1_MESH_PLANE) {
        int32_t p;
        uint8_t r, g, b;
        const int use_texture =
            ent->texture_index > 0 && ent->texture_index <= g_ps1_texture_count &&
            ps1_texture_width((unsigned int)ent->texture_index) > 0 &&
            ps1_texture_height((unsigned int)ent->texture_index) > 0;
        const uint16_t texture_tpage = use_texture
            ? ps1_texture_tpage((unsigned int)ent->texture_index) : 0;
        const int texture_u0 = use_texture
            ? (int)ps1_texture_u_offset((unsigned int)ent->texture_index) : 0;
        const int texture_v0 = use_texture
            ? (int)ps1_texture_v_offset((unsigned int)ent->texture_index) : 0;
        const int texture_u1 = use_texture
            ? clamp_u8(texture_u0 +
                (int)ps1_texture_width((unsigned int)ent->texture_index) - 1) : 0;
        const int texture_v1 = use_texture
            ? clamp_u8(texture_v0 +
                (int)ps1_texture_height((unsigned int)ent->texture_index) - 1) : 0;
        int hx = world_to_psx(fix16_mul(ent->mesh_size, ent->scale[0])) / 2;
        int hz = world_to_psx(fix16_mul(ent->mesh_size, ent->scale[2])) / 2;
        if (hx < 64) hx = 64;
        if (hz < 64) hz = 64;
        if (hx > 32000) hx = 32000;
        if (hz > 32000) hz = 32000;

        if (use_texture)
            gte_shade_textured_smooth_normal(
                &s_plane_norm_up, base_r, base_g, base_b, &r, &g, &b);
        else
            gte_shade_normal(&s_plane_norm_up, base_r, base_g, base_b, &r, &g, &b);

        /* Geometry is subdivided only to avoid PS1 screen-coordinate overflow.
         * UVs stay continuous across those subdivisions and are transformed by
         * the bound Material's Tiling/Offset values. */
        const int tiles = 4;
        const int stepX = (2 * hx) / tiles;
        const int stepZ = (2 * hz) / tiles;

        for (int tz = 0; tz < tiles; ++tz) {
            for (int tx = 0; tx < tiles; ++tx) {
                SVECTOR pv0, pv1, pv2, pv3;
                const int x0 = -hx + tx * stepX;
                const int x1 = x0 + stepX;
                const int z0 = -hz + tz * stepZ;
                const int z1 = z0 + stepZ;
                const int tile_u0 = use_texture
                    ? primitive_material_uv(texture_u0, texture_u1 - texture_u0,
                        ent->texture_tiling_u_q8, ent->texture_offset_u_q8,
                        tx, tiles, 0) : 0;
                const int tile_u1 = use_texture
                    ? primitive_material_uv(texture_u0, texture_u1 - texture_u0,
                        ent->texture_tiling_u_q8, ent->texture_offset_u_q8,
                        tx + 1, tiles, 1) : 0;
                const int tile_v0 = use_texture
                    ? primitive_material_uv(texture_v0, texture_v1 - texture_v0,
                        ent->texture_tiling_v_q8, ent->texture_offset_v_q8,
                        tz, tiles, 0) : 0;
                const int tile_v1 = use_texture
                    ? primitive_material_uv(texture_v0, texture_v1 - texture_v0,
                        ent->texture_tiling_v_q8, ent->texture_offset_v_q8,
                        tz + 1, tiles, 1) : 0;

                pv0.vx = (short)x0; pv0.vy = 0; pv0.vz = (short)z0; pv0.pad = 0;
                pv1.vx = (short)x1; pv1.vy = 0; pv1.vz = (short)z0; pv1.pad = 0;
                pv2.vx = (short)x1; pv2.vy = 0; pv2.vz = (short)z1; pv2.pad = 0;
                pv3.vx = (short)x0; pv3.vy = 0; pv3.vz = (short)z1; pv3.pad = 0;

                /* Tri 1: 0-1-2 */
                {
                    int32_t sxy0, sxy1, sxy2;
                    gte_ldv3(&pv0, &pv1, &pv2);
                    gte_rtpt();
                    gte_stsxy0(&sxy0);
                    gte_stsxy1(&sxy1);
                    gte_stsxy2(&sxy2);
                    gte_avsz3();
                    gte_stotz(&p);
                    /* Use the same OT scale as Cube/Sphere primitives.
                     * The previous p>>2 put Plane in buckets half as deep,
                     * causing it to paint over geometry physically above it. */
                    otz = mesh_triangle_otz_fine(p);
                    if (otz < OT_LEN) {
                        uint8_t fog_r = r, fog_g = g, fog_b = b;
                        apply_scene_fog(p << 2, &fog_r, &fog_g, &fog_b);
                        if (use_texture) {
                            POLY_FT3* tri;
                            if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                                return added;
                            tri = (POLY_FT3*)*pri;
                            setPolyFT3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setUV3(tri, tile_u0, tile_v0,
                                   tile_u1, tile_v0,
                                   tile_u1, tile_v1);
                            setRGB0(tri, fog_r, fog_g, fog_b);
                            tri->tpage = texture_tpage;
                            tri->clut = 0;
                            addPrim(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        } else {
                            POLY_F3* tri;
                            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                return added;
                            tri = (POLY_F3*)*pri;
                            setPolyF3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setRGB0(tri, fog_r, fog_g, fog_b);
                            addPrim(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        }
                        ++added;
                    }
                }
                /* Tri 2: 0-2-3 */
                {
                    int32_t sxy0, sxy1, sxy2;
                    gte_ldv3(&pv0, &pv2, &pv3);
                    gte_rtpt();
                    gte_stsxy0(&sxy0);
                    gte_stsxy1(&sxy1);
                    gte_stsxy2(&sxy2);
                    gte_avsz3();
                    gte_stotz(&p);
                    otz = mesh_triangle_otz_fine(p);
                    if (otz < OT_LEN) {
                        uint8_t fog_r = r, fog_g = g, fog_b = b;
                        apply_scene_fog(p << 2, &fog_r, &fog_g, &fog_b);
                        if (use_texture) {
                            POLY_FT3* tri;
                            if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                                return added;
                            tri = (POLY_FT3*)*pri;
                            setPolyFT3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setUV3(tri, tile_u0, tile_v0,
                                   tile_u1, tile_v1,
                                   tile_u0, tile_v1);
                            setRGB0(tri, fog_r, fog_g, fog_b);
                            tri->tpage = texture_tpage;
                            tri->clut = 0;
                            addPrim(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        } else {
                            POLY_F3* tri;
                            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                return added;
                            tri = (POLY_F3*)*pri;
                            setPolyF3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setRGB0(tri, fog_r, fog_g, fog_b);
                            addPrim(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        }
                        ++added;
                    }
                }

                /* (debug overlay removed) */
            }
        }
        return added;
    }

    if (ent->mesh == PS1_MESH_SPHERE) {
        int ti;
        for (ti = 0; ti < (int)(sizeof(s_sphere_tris) / sizeof(s_sphere_tris[0])); ++ti) {
            int32_t p;
            int32_t sxy0, sxy1, sxy2;
            int16_t x0, y0, x1, y1, x2, y2;
            uint8_t r, g, b;
            VECTOR cv0, cv1, cv2;
            const tri_index* tri_idx = &s_sphere_tris[ti];
            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                return added;

            transform_point(&composed, &s_sphere_verts[tri_idx->v0], &cv0);
            transform_point(&composed, &s_sphere_verts[tri_idx->v1], &cv1);
            transform_point(&composed, &s_sphere_verts[tri_idx->v2], &cv2);
            if (cv0.vz < s_near_z || cv1.vz < s_near_z || cv2.vz < s_near_z)
                continue;

            gte_ldv3(
                &s_sphere_verts[tri_idx->v0],
                &s_sphere_verts[tri_idx->v1],
                &s_sphere_verts[tri_idx->v2]
            );
            gte_rtpt();
            gte_avsz3();
            gte_stotz(&p);
            otz = mesh_triangle_otz_fine(p);
            if (otz >= OT_LEN)
                continue;

            gte_stsxy0(&sxy0);
            gte_stsxy1(&sxy1);
            gte_stsxy2(&sxy2);
            x0 = (int16_t)(sxy0 & 0xffff);
            y0 = (int16_t)((sxy0 >> 16) & 0xffff);
            x1 = (int16_t)(sxy1 & 0xffff);
            y1 = (int16_t)((sxy1 >> 16) & 0xffff);
            x2 = (int16_t)(sxy2 & 0xffff);
            y2 = (int16_t)((sxy2 >> 16) & 0xffff);
            if (triangle_outside_screen(x0, y0, x1, y1, x2, y2))
                continue;
            if (!screen_front_facing_strict(x0, y0, x1, y1, x2, y2))
                continue;

            {
                const int shade = 168 + ((ti % 6) * 8);
                POLY_F3* poly = (POLY_F3*)*pri;
                setPolyF3(poly);
                setXY3(poly, x0, y0, x1, y1, x2, y2);
                r = (uint8_t)(((uint16_t)shade * base_r) >> 8);
                g = (uint8_t)(((uint16_t)shade * base_g) >> 8);
                b = (uint8_t)(((uint16_t)shade * base_b) >> 8);
                apply_scene_fog(p << 2, &r, &g, &b);
                setRGB0(poly, r, g, b);
                addPrim(ot + otz, poly);
                *pri = (char*)(poly + 1);
                ++added;
            }
        }
        return added;
    }

    {
        int vi;
        int32_t cube_max_z = -0x3fffffff;
        int cube_object_otz_floor;
        for (vi = 0; vi < (int)(sizeof(s_cube_verts) / sizeof(s_cube_verts[0])); ++vi) {
            VECTOR camera_vertex;
            transform_point(&composed, &s_cube_verts[vi], &camera_vertex);
            if (camera_vertex.vz > cube_max_z)
                cube_max_z = camera_vertex.vz;
        }
        cube_object_otz_floor = (int)(cube_max_z >> 3);
        if (cube_object_otz_floor < PS1_WORLD_OT_MIN)
            cube_object_otz_floor = PS1_WORLD_OT_MIN;
        else if (cube_object_otz_floor >= OT_LEN)
            cube_object_otz_floor = OT_LEN - 1;

        for (fi = 0; fi < 6; ++fi) {
            int32_t p;
            uint8_t r, g, b;

        gte_ldv3(
            &s_cube_verts[s_cube_faces[fi].v0],
            &s_cube_verts[s_cube_faces[fi].v1],
            &s_cube_verts[s_cube_faces[fi].v2]
        );
        gte_rtpt();
        /* Cube face indices account for the camera-space Y flip, so their
         * first triangle can be used for the backface test below. */

        gte_avsz3();
        gte_stotz(&p);
        otz = mesh_triangle_otz_fine(p);
        if (otz >= OT_LEN)
            continue;

        if ((const char*)(*pri + sizeof(POLY_F4)) > packet_end)
            return added;
        POLY_F4* poly = (POLY_F4*)*pri;
        setPolyF4(poly);
        /* Ensure gte_ldrgb reads a stable primitive color. */
        setRGB0(poly, 128, 128, 128);

        gte_stsxy0(&poly->x0);
        gte_stsxy1(&poly->x1);
        gte_stsxy2(&poly->x2);

        gte_ldv0(&s_cube_verts[s_cube_faces[fi].v3]);
        gte_rtps();
        gte_stsxy(&poly->x3);

        /* Built-in cube vertices now use outward winding. Reject the rear
         * faces instead of submitting both sides into the same OT buckets;
         * otherwise the far wall of the cube can overwrite its front wall
         * and make the primitive appear inside-out. */
        if (!screen_front_facing_strict(
                poly->x0, poly->y0, poly->x1, poly->y1,
                poly->x2, poly->y2))
            continue;

        gte_ldrgb(&poly->r0);
        gte_ldv0(&s_cube_norms[fi]);
        gte_ncs();
        gte_strgb(&poly->r0);

        r = (uint8_t)(((uint16_t)poly->r0 * base_r) >> 8);
        g = (uint8_t)(((uint16_t)poly->g0 * base_g) >> 8);
        b = (uint8_t)(((uint16_t)poly->b0 * base_b) >> 8);
        apply_scene_fog(p << 2, &r, &g, &b);
        setRGB0(poly, r, g, b);

        gte_avsz4();
        gte_stotz(&p);
        otz = mesh_triangle_otz_fine(p);
        if (otz >= OT_LEN)
            continue;
        /* Cubes are often used as large, simple child/background blocks.
         * With no Z-buffer, sorting each face purely by its near/front side can
         * let a cube behind another mesh punch through it. Clamp cube faces to
         * at least the object's farthest-vertex bucket so background/child
         * cubes cannot leak in front of smooth geometry that should hide them. */
            if (otz < cube_object_otz_floor)
                otz = cube_object_otz_floor;

            addPrim(ot + otz, poly);
            *pri = (char*)(poly + 1);
            ++added;
        }
    }
    return added;
}

static int draw_prerender_occluder(char** pri, const char* packet_end, uint32_t* ot,
                                   const ps1_entity* ent, unsigned int entity_index,
                                   unsigned int cache_index, unsigned int background_index,
                                   const occluder_actor_mask* actor_mask,
                                   int* tri_budget) {
    SVECTOR rotvec;
    VECTOR pos, scale;
    MATRIX rmtx, omtx, composed;
    const ps1_mesh* mesh;
    const int background_width = (int)ps1_texture_width(background_index);
    const int background_height = (int)ps1_texture_height(background_index);
    const int background_x = (int)ps1_texture_vram_x(background_index);
    const int background_y = (int)ps1_texture_vram_y(background_index);
    const int background_uoff = (int)ps1_texture_u_offset(background_index);
    const int background_voff = (int)ps1_texture_v_offset(background_index);
    int mesh_scale = 128;
    int added = 0;
    uint16_t triangle_index;
    int bounding_radius;
    int actor_left_x2;
    int actor_right_x2;
    const occluder_screen_vert* projected_vertices = 0;
    const occluder_screen_bounds* projected_bounds = 0;
#if PS1_OCCLUDER_DEBUG_VISUALIZE
    const occluder_actor_mask debug_mask = {
        1, 0, 0, s_screen_w - 1, s_screen_h - 1, 0x3fffffff, 0
    };
    actor_mask = &debug_mask;
#endif

    if (!actor_mask || !actor_mask->valid ||
        !ent || ent->mesh != PS1_MESH_CUSTOM || ent->mesh_index == 0 ||
        ent->mesh_index > g_ps1_mesh_count || background_width <= 0 ||
        background_height <= 0)
        return 0;

    actor_left_x2 = actor_mask->left >> 1;
    actor_right_x2 = (actor_mask->right + 1) >> 1;
    mesh = &g_ps1_meshes[ent->mesh_index - 1u];
    if (cache_index < PS1_OCCLUDER_CACHE_MAX &&
        s_occluder_transform_cache[cache_index].valid &&
        s_occluder_transform_cache[cache_index].entity_index == entity_index) {
        omtx = s_occluder_transform_cache[cache_index].world_matrix;
        bounding_radius = s_occluder_transform_cache[cache_index].bounding_radius;
    } else {
        mesh_scale = mesh->scale_q12;
        unity_euler_to_psx_object(
            ent->rotation[0], ent->rotation[1], ent->rotation[2], &rotvec);
        unity_pos_to_psx(ent->position, &pos);
        RotMatrix(&rotvec, &rmtx);
        scale.vx = (int)((int64_t)ent->scale[0] * mesh_scale >> 16);
        scale.vy = (int)((int64_t)ent->scale[1] * mesh_scale >> 16);
        scale.vz = (int)((int64_t)ent->scale[2] * mesh_scale >> 16);
        if (scale.vx == 0) scale.vx = 1;
        if (scale.vy == 0) scale.vy = 1;
        if (scale.vz == 0) scale.vz = 1;
        {
            int max_scale = scale.vx < 0 ? -scale.vx : scale.vx;
            const int abs_y = scale.vy < 0 ? -scale.vy : scale.vy;
            const int abs_z = scale.vz < 0 ? -scale.vz : scale.vz;
            if (abs_y > max_scale) max_scale = abs_y;
            if (abs_z > max_scale) max_scale = abs_z;
            bounding_radius = (int)(((int64_t)max_scale * 2048) >> 12);
            if (bounding_radius < 8)
                bounding_radius = 8;
        }
        omtx = rmtx;
        ScaleMatrix(&omtx, &scale);
        TransMatrix(&omtx, &pos);
        if (cache_index < PS1_OCCLUDER_CACHE_MAX) {
            s_occluder_transform_cache[cache_index].valid = 1;
            s_occluder_transform_cache[cache_index].entity_index = (uint16_t)entity_index;
            s_occluder_transform_cache[cache_index].world_matrix = omtx;
            s_occluder_transform_cache[cache_index].bounding_radius = bounding_radius;
        }
    }
    CompMatrixLV(&s_cam_mtx, &omtx, &composed);

    /* Coarse sphere/frustum culling prevents every triangle of every room
     * occluder from reaching the GTE. */
    if (composed.t[2] + bounding_radius < s_near_z)
        return 0;
    if (composed.t[2] - bounding_radius > s_far_z)
        return 0;
    if (s_geom_screen > 0 && composed.t[2] > 0) {
        const int depth = composed.t[2] > s_near_z ? composed.t[2] : s_near_z;
        const int x_limit =
            (int)(((int64_t)depth * (s_screen_w / 2 + 24)) / s_geom_screen) +
            bounding_radius;
        const int y_limit =
            (int)(((int64_t)depth * (s_screen_h / 2 + 24)) / s_geom_screen) +
            bounding_radius;
        const int abs_x = composed.t[0] < 0 ? -composed.t[0] : composed.t[0];
        const int abs_y = composed.t[1] < 0 ? -composed.t[1] : composed.t[1];
        if (abs_x > x_limit || abs_y > y_limit)
            return 0;

        {
            const int screen_x = (s_screen_w / 2) +
                (int)(((int64_t)s_geom_screen * composed.t[0]) / depth);
            const int screen_y = (s_screen_h / 2) +
                (int)(((int64_t)s_geom_screen * composed.t[1]) / depth);
            const int screen_radius =
                (int)(((int64_t)s_geom_screen * bounding_radius) / depth) + 3;
            if (screen_x + screen_radius < actor_mask->left ||
                screen_x - screen_radius > actor_mask->right ||
                screen_y + screen_radius < actor_mask->top ||
                screen_y - screen_radius > actor_mask->bottom)
                return 0;
        }
    }
    if (composed.t[2] - bounding_radius + actor_mask->depth_margin >=
        actor_mask->depth)
        return 0;

    if (cache_index < PS1_OCCLUDER_CACHE_MAX) {
        occluder_transform_cache* cache =
            &s_occluder_transform_cache[cache_index];
        if ((cache->projected_camera_index !=
                 s_occluder_projection_camera_index ||
             cache->projected_count != mesh->vert_count) &&
            (uint32_t)s_occluder_projected_vertex_count +
                    mesh->vert_count <=
                PS1_OCCLUDER_PROJECTED_VERTEX_MAX) {
            cache->projected_offset = s_occluder_projected_vertex_count;
            cache->projected_count = mesh->vert_count;
            cache->projected_built_count = 0;
            cache->projected_camera_index =
                s_occluder_projection_camera_index;
            s_occluder_projected_vertex_count =
                (uint16_t)(s_occluder_projected_vertex_count +
                           mesh->vert_count);
        }

        if (cache->projected_camera_index ==
                s_occluder_projection_camera_index &&
            cache->projected_count == mesh->vert_count &&
            (uint32_t)cache->projected_offset + cache->projected_count <=
                PS1_OCCLUDER_PROJECTED_VERTEX_MAX &&
            cache->projected_built_count < cache->projected_count) {
            uint16_t vertex_index;
            uint16_t build_end;
            if (s_occluder_vertex_build_budget > 0) {
                build_end =
                    (uint16_t)(cache->projected_built_count +
                               s_occluder_vertex_build_budget);
                if (build_end > cache->projected_count ||
                    build_end < cache->projected_built_count)
                    build_end = cache->projected_count;
                for (vertex_index = cache->projected_built_count;
                     vertex_index < build_end;
                     ++vertex_index) {
                    VECTOR camera_vertex;
                    occluder_screen_vert* cached =
                        &s_occluder_projected_vertices[
                             cache->projected_offset + vertex_index];
                    transform_point(&composed, &mesh->verts[vertex_index],
                                    &camera_vertex);
                    project_vector_vert(&camera_vertex, &cached->x, &cached->y,
                                        s_near_z);
                    cached->z = camera_vertex.vz;
                }
                s_occluder_vertex_build_budget -=
                    (int)(build_end - cache->projected_built_count);
                cache->projected_built_count = build_end;
            }
        }

        if (cache->projected_camera_index ==
                s_occluder_projection_camera_index &&
            cache->projected_count == mesh->vert_count &&
            cache->projected_built_count == cache->projected_count &&
            (uint32_t)cache->projected_offset + cache->projected_count <=
                PS1_OCCLUDER_PROJECTED_VERTEX_MAX) {
            projected_vertices =
                &s_occluder_projected_vertices[cache->projected_offset];
        }

        if ((cache->bounds_camera_index !=
                 s_occluder_projection_camera_index ||
             cache->bounds_count != mesh->tri_count) &&
            (uint32_t)s_occluder_projected_tri_count +
                    mesh->tri_count <=
                PS1_OCCLUDER_PROJECTED_TRI_MAX) {
            cache->bounds_offset = s_occluder_projected_tri_count;
            cache->bounds_count = mesh->tri_count;
            cache->bounds_built_count = 0;
            cache->bounds_camera_index =
                s_occluder_projection_camera_index;
            s_occluder_projected_tri_count =
                (uint16_t)(s_occluder_projected_tri_count +
                           mesh->tri_count);
        }

        if (projected_vertices &&
            cache->bounds_camera_index ==
                s_occluder_projection_camera_index &&
            cache->bounds_count == mesh->tri_count &&
            (uint32_t)cache->bounds_offset + cache->bounds_count <=
                PS1_OCCLUDER_PROJECTED_TRI_MAX &&
            cache->bounds_built_count < cache->bounds_count &&
            s_occluder_bounds_build_budget > 0) {
            uint16_t bounds_index;
            uint16_t build_end =
                (uint16_t)(cache->bounds_built_count +
                           s_occluder_bounds_build_budget);
            if (build_end > cache->bounds_count ||
                build_end < cache->bounds_built_count)
                build_end = cache->bounds_count;
            for (bounds_index = cache->bounds_built_count;
                 bounds_index < build_end;
                 ++bounds_index) {
                const ps1_mesh_tri* triangle = &mesh->tris[bounds_index];
                occluder_screen_bounds* bounds =
                    &s_occluder_projected_tri_bounds[
                        cache->bounds_offset + bounds_index];
                int x0, y0, x1, y1, x2, y2;
                int32_t z0, z1, z2;
                int min_x, max_x, min_y, max_y;
                if (triangle->i0 >= mesh->vert_count ||
                    triangle->i1 >= mesh->vert_count ||
                    triangle->i2 >= mesh->vert_count) {
                    bounds->min_x2 = 255;
                    bounds->max_x2 = 0;
                    bounds->min_y = 255;
                    bounds->max_y = 0;
                    continue;
                }
                {
                    const occluder_screen_vert* v0 =
                        &projected_vertices[triangle->i0];
                    const occluder_screen_vert* v1 =
                        &projected_vertices[triangle->i1];
                    const occluder_screen_vert* v2 =
                        &projected_vertices[triangle->i2];
                    x0 = v0->x; y0 = v0->y; z0 = v0->z;
                    x1 = v1->x; y1 = v1->y; z1 = v1->z;
                    x2 = v2->x; y2 = v2->y; z2 = v2->z;
                }
                if (z0 < s_near_z || z1 < s_near_z || z2 < s_near_z) {
                    bounds->min_x2 = 255;
                    bounds->max_x2 = 0;
                    bounds->min_y = 255;
                    bounds->max_y = 0;
                    continue;
                }
                min_x = x0;
                if (x1 < min_x) min_x = x1;
                if (x2 < min_x) min_x = x2;
                max_x = x0;
                if (x1 > max_x) max_x = x1;
                if (x2 > max_x) max_x = x2;
                min_y = y0;
                if (y1 < min_y) min_y = y1;
                if (y2 < min_y) min_y = y2;
                max_y = y0;
                if (y1 > max_y) max_y = y1;
                if (y2 > max_y) max_y = y2;
                if (max_x < 0 || min_x >= s_screen_w ||
                    max_y < 0 || min_y >= s_screen_h) {
                    bounds->min_x2 = 255;
                    bounds->max_x2 = 0;
                    bounds->min_y = 255;
                    bounds->max_y = 0;
                    continue;
                }
                if (min_x < 0) min_x = 0;
                if (max_x >= s_screen_w) max_x = s_screen_w - 1;
                if (min_y < 0) min_y = 0;
                if (max_y >= s_screen_h) max_y = s_screen_h - 1;
                bounds->min_x2 = (uint8_t)(min_x >> 1);
                bounds->max_x2 = (uint8_t)((max_x + 1) >> 1);
                bounds->min_y = (uint8_t)min_y;
                bounds->max_y = (uint8_t)max_y;
            }
            s_occluder_bounds_build_budget -=
                (int)(build_end - cache->bounds_built_count);
            cache->bounds_built_count = build_end;
        }

        if (cache->bounds_camera_index ==
                s_occluder_projection_camera_index &&
            cache->bounds_count == mesh->tri_count &&
            cache->bounds_built_count == cache->bounds_count &&
            (uint32_t)cache->bounds_offset + cache->bounds_count <=
                PS1_OCCLUDER_PROJECTED_TRI_MAX) {
            projected_bounds =
                &s_occluder_projected_tri_bounds[cache->bounds_offset];
        }

        /* An unfinished cache is only an optimization miss. Keep rendering
         * through the direct projection path so occlusion never disappears
         * while a camera's cache is warming up. */
    }

    for (triangle_index = 0; triangle_index < mesh->tri_count; ++triangle_index) {
        if (*tri_budget <= 0)
            break;
        const ps1_mesh_tri* source_triangle = &mesh->tris[triangle_index];
        VECTOR camera_vertex[3];
        occluder_screen_vert source[3];
        int min_x, max_x;
        int first_strip, last_strip, strip;
        int32_t sz0, sz1, sz2;

        if (projected_bounds) {
            const occluder_screen_bounds* bounds =
                &projected_bounds[triangle_index];
            if (bounds->min_x2 > bounds->max_x2 ||
                bounds->max_x2 < actor_left_x2 ||
                bounds->min_x2 > actor_right_x2 ||
                bounds->max_y < actor_mask->top ||
                bounds->min_y > actor_mask->bottom)
                continue;
        }

        if (source_triangle->i0 >= mesh->vert_count ||
            source_triangle->i1 >= mesh->vert_count ||
            source_triangle->i2 >= mesh->vert_count)
            continue;

        if (projected_vertices) {
            source[0] = projected_vertices[source_triangle->i0];
            source[1] = projected_vertices[source_triangle->i1];
            source[2] = projected_vertices[source_triangle->i2];
            sz0 = source[0].z;
            sz1 = source[1].z;
            sz2 = source[2].z;
        } else {
            transform_point(&composed, &mesh->verts[source_triangle->i0],
                            &camera_vertex[0]);
            transform_point(&composed, &mesh->verts[source_triangle->i1],
                            &camera_vertex[1]);
            transform_point(&composed, &mesh->verts[source_triangle->i2],
                            &camera_vertex[2]);
            sz0 = camera_vertex[0].vz;
            sz1 = camera_vertex[1].vz;
            sz2 = camera_vertex[2].vz;
            project_vector_vert(&camera_vertex[0], &source[0].x, &source[0].y,
                                s_near_z);
            project_vector_vert(&camera_vertex[1], &source[1].x, &source[1].y,
                                s_near_z);
            project_vector_vert(&camera_vertex[2], &source[2].x, &source[2].y,
                                s_near_z);
            source[0].z = sz0;
            source[1].z = sz1;
            source[2].z = sz2;
        }
        if (sz0 < s_near_z || sz1 < s_near_z || sz2 < s_near_z)
            continue;

        if (triangle_outside_screen(
                (int16_t)source[0].x, (int16_t)source[0].y,
                (int16_t)source[1].x, (int16_t)source[1].y,
                (int16_t)source[2].x, (int16_t)source[2].y))
            continue;
        if ((source[0].x < actor_mask->left && source[1].x < actor_mask->left &&
             source[2].x < actor_mask->left) ||
            (source[0].x > actor_mask->right && source[1].x > actor_mask->right &&
             source[2].x > actor_mask->right) ||
            (source[0].y < actor_mask->top && source[1].y < actor_mask->top &&
             source[2].y < actor_mask->top) ||
            (source[0].y > actor_mask->bottom && source[1].y > actor_mask->bottom &&
             source[2].y > actor_mask->bottom))
            continue;

        min_x = (int)source[0].x;
        if (source[1].x < min_x) min_x = (int)source[1].x;
        if (source[2].x < min_x) min_x = (int)source[2].x;
        max_x = (int)source[0].x;
        if (source[1].x > max_x) max_x = (int)source[1].x;
        if (source[2].x > max_x) max_x = (int)source[2].x;
        if (min_x < 0) min_x = 0;
        if (max_x >= background_width) max_x = background_width - 1;
        /* UV coordinates cover 256 texels even though a 16-bpp tpage base is
         * selected in 64-pixel increments. Split only at the UV wrap point. */
        first_strip = min_x / 256;
        last_strip = max_x / 256;

        for (strip = first_strip; strip <= last_strip; ++strip) {
            const int strip_left = strip * 256;
            int strip_right = strip_left + 255;
            int clip_left;
            int clip_right;
            occluder_screen_vert clipped[32];
            int clipped_count;
            int fan;
            uint16_t tpage;
            if (strip_right >= background_width)
                strip_right = background_width - 1;
            clip_left = strip_left > actor_mask->left ? strip_left : actor_mask->left;
            clip_right = strip_right < actor_mask->right ? strip_right : actor_mask->right;
            if (clip_left > clip_right)
                continue;
            clipped_count = clip_occluder_to_rect(
                source, clipped, clip_left, actor_mask->top,
                clip_right, actor_mask->bottom);
            if (clipped_count < 3)
                continue;
            if (*tri_budget <= 0)
                break;
            (*tri_budget)--;
            tpage = getTPage(2, 0, background_x + strip_left, background_y);

            for (fan = 0; fan < clipped_count - 2; ++fan) {
                const occluder_screen_vert* a = &clipped[0];
                const occluder_screen_vert* b = &clipped[fan + 1];
                const occluder_screen_vert* c = &clipped[fan + 2];
                const int32_t local_z = (a->z + b->z + c->z) / 3;
                /* InitGeom configures AVSZ3 so gte_stotz() is approximately
                 * the average camera Z divided by four. Normal mesh rendering
                 * then applies mesh_triangle_otz() (another divide by four).
                 * Occluders use clipped CPU-side Z values, so reproduce the
                 * AVSZ3 scale here before assigning their OT bucket. */
                int otz = mesh_triangle_otz(local_z >> 2);
#if PS1_OCCLUDER_DEBUG_VISUALIZE
                POLY_F3* triangle;
#else
                POLY_FT3* triangle;
#endif
                if (local_z <= 0)
                    continue;
                if (local_z + actor_mask->depth_margin >= actor_mask->depth)
                    continue;
#if PS1_OCCLUDER_DEBUG_VISUALIZE
                otz = 1;
                if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                    return added;
                triangle = (POLY_F3*)*pri;
                setPolyF3(triangle);
                setXY3(triangle, a->x, a->y, b->x, b->y, c->x, c->y);
                setRGB0(triangle, 255, 0, 255);
                addPrim(ot + otz, triangle);
                *pri = (char*)(triangle + 1);
#else
#if PS1_OCCLUDER_DEBUG_FORCE_FRONT
                otz = 1;
#else
                /* Occluders are submitted before actors, so equal-depth
                 * buckets already restore the background after the actor.
                 * Do not bias them nearer or surfaces in front can be eaten. */
#endif
                if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                    return added;
                triangle = (POLY_FT3*)*pri;
                setPolyFT3(triangle);
                /* Restore the pre-rendered pixel exactly. Use standard modulated
                 * textured triangle with RGB(128,128,128) to ensure maximum
                 * emulator and hardware compatibility. */
                setXY3(triangle, a->x, a->y, b->x, b->y, c->x, c->y);
                setRGB0(triangle, 128, 128, 128);
                triangle->tpage = tpage;
                triangle->clut = 0;
                setUV3(triangle,
                       clamp_u8(background_uoff + a->x - strip_left),
                       clamp_u8(background_voff + a->y),
                       clamp_u8(background_uoff + b->x - strip_left),
                       clamp_u8(background_voff + b->y),
                       clamp_u8(background_uoff + c->x - strip_left),
                       clamp_u8(background_voff + c->y));
                addPrim(ot + otz, triangle);
                *pri = (char*)(triangle + 1);
#endif
                ++added;
            }
        }
    }
    return added;
}

void ps1_render_init(int screen_w, int screen_h) {
    s_screen_w = screen_w;
    s_screen_h = screen_h;
    InitGeom();
    gte_SetGeomOffset(screen_w / 2, screen_h / 2);
    gte_SetGeomScreen(screen_w / 2);
    /* Defaults; can be overridden per-scene in apply_scene_lighting(). */
    gte_SetBackColor(48, 48, 48);
    s_light_mtx.t[0] = s_light_mtx.t[1] = s_light_mtx.t[2] = 0;
    apply_scene_lighting();
    s_initialized = 1;
}

void ps1_render_frame(uint32_t* ot, char** nextpri, const char* packet_end) {
    char* pri;
    unsigned int i, count;
    const ps1_entity* cam_ent = 0;
    int cam_idx;
    int total_added = 0;
    int any_overflow = 0;
    unsigned int background_index = 0;
    occluder_actor_mask actor_mask;
    const char* world_packet_end;

    if (!s_initialized)
        ps1_render_init(s_screen_w, s_screen_h);
    if (!ot || !nextpri || !*nextpri || !packet_end)
        return;
    if (*nextpri >= packet_end)
        return;

    world_packet_end = packet_end;
    if (packet_end - *nextpri > PS1_UI_PACKET_RESERVE)
        world_packet_end = packet_end - PS1_UI_PACKET_RESERVE;

    cam_idx = ps1_scene_camera_index();
    if (cam_idx >= 0)
        cam_ent = ps1_scene_entity((unsigned int)cam_idx);
    set_camera(cam_ent);
    if (s_occluder_projection_camera_index != (int16_t)cam_idx) {
        unsigned int cache_index;
        s_occluder_projection_camera_index = (int16_t)cam_idx;
        s_occluder_projected_vertex_count = 0;
        s_occluder_projected_tri_count = 0;
        /* The direct projection path is already correct while the cache is
         * cold. Do not suppress occlusion for several frames after a fixed
         * camera cut, otherwise foreground walls appear to slide into place. */
        s_occluder_camera_cooldown = 0;
        for (cache_index = 0; cache_index < PS1_OCCLUDER_CACHE_MAX;
             ++cache_index) {
            s_occluder_transform_cache[cache_index].projected_camera_index = -1;
            s_occluder_transform_cache[cache_index].projected_count = 0;
            s_occluder_transform_cache[cache_index].projected_built_count = 0;
            s_occluder_transform_cache[cache_index].bounds_camera_index = -1;
            s_occluder_transform_cache[cache_index].bounds_count = 0;
            s_occluder_transform_cache[cache_index].bounds_built_count = 0;
        }
    }
    s_occluder_vertex_build_budget =
        s_occluder_camera_cooldown == 0 ? 128 : 0;
    s_occluder_bounds_build_budget =
        s_occluder_camera_cooldown == 0 ? 128 : 0;
    apply_scene_lighting();
    s_rigid_root_transform.valid = 0;

    pri = *nextpri;

    /* Use the full scene entity count so entities beyond the 128-slot runtime
     * cap (e.g. character bones exported at the end of the list) are rendered.
     * ps1_scene_entity() falls back to read-only ROM data for those indices. */
    count = g_ps1_scene.entity_count;
    if (s_indexed_entity_count != count)
        rebuild_render_entity_lists();
    actor_mask = build_occluder_actor_mask();
    background_index = active_background_texture_index();
    if (background_index != 0) {
        const uint16_t bw = ps1_texture_width(background_index);
        const uint16_t bh = ps1_texture_height(background_index);
        const int background_x = (int)ps1_texture_vram_x(background_index);
        const int background_y = (int)ps1_texture_vram_y(background_index);
        const uint8_t is_baked_background =
            ps1_texture_is_background(background_index);
        uint16_t sample_height = bh;
        uint16_t source_x = 0;
        int background_ok = bw > 0 && bh > 0;

        if (!is_baked_background) {
            /* HDRI/equirectangular skyboxes use the upper half as the cheap
             * runtime sky band. Pre-rendered backgrounds use all 320x240. */
            sample_height = (uint16_t)(bh / 2u);
            if (sample_height == 0)
                sample_height = bh;
        }

        /* A 16-bpp PS1 texture page exposes at most 256 horizontal texels.
         * Pre-rendered backgrounds are 320x240, so drawing one FT4 cropped
         * them to 256px and stretched that crop to 320px. The occluder and
         * collider projection remained 320px-wide, producing the visible
         * seam and large post-switch alignment error. Split at tpage/UV wrap
         * boundaries and preserve the source-to-screen mapping exactly. */
        while (background_ok && source_x < bw) {
            const int absolute_x = background_x + (int)source_x;
            const int u_start = absolute_x & 63;
            const int page_capacity = 256 - u_start;
            uint16_t strip_width = (uint16_t)page_capacity;
            int screen_x0;
            int screen_x1;
            uint8_t u0;
            uint8_t u1;
            uint8_t v0;
            uint8_t v1;
            uint16_t tpage;
            POLY_FT4* bg;

            if ((uint32_t)source_x + strip_width > bw)
                strip_width = (uint16_t)(bw - source_x);
            if (strip_width == 0 ||
                (const char*)(pri + sizeof(POLY_FT4)) > world_packet_end) {
                background_ok = 0;
                break;
            }

            screen_x0 = (int)(((int32_t)source_x * s_screen_w) / bw);
            screen_x1 = (int)(((int32_t)(source_x + strip_width) * s_screen_w) / bw);
            u0 = (uint8_t)u_start;
            u1 = (uint8_t)(u_start + strip_width - 1u);
            v0 = (uint8_t)(background_y & 255);
            v1 = clamp_u8((background_y & 255) + (int)sample_height - 1);
            tpage = getTPage(2, 0, absolute_x, background_y);

            bg = (POLY_FT4*)pri;
            setPolyFT4(bg);
            setXY4(bg, screen_x0, 0, screen_x1, 0,
                   screen_x0, s_screen_h, screen_x1, s_screen_h);
            if (is_baked_background)
                setUV4(bg, u0, v0, u1, v0, u0, v1, u1, v1);
            else
                setUV4(bg, u0, v1, u1, v1, u0, v0, u1, v0);
            setRGB0(bg, 128, 128, 128);
            bg->tpage = tpage;
            bg->clut = 0;
            addPrim(ot + (OT_LEN - 1), bg);
            pri = (char*)(bg + 1);
            source_x = (uint16_t)(source_x + strip_width);
        }
        if (!background_ok) {
            any_overflow = 1;
        }
    }

    /* addPrim prepends primitives within an OT bucket. Submit background
     * restoration before actors so an actor in the same bucket is drawn
     * first and the occluder restores the background over it afterward.
     * Keep half of the packet available for animated actors and normal
     * geometry so large occluder sets cannot make the character disappear. */
    if (background_index != 0 && !any_overflow &&
        s_occluder_camera_cooldown == 0) {
        const int packet_reserve = 65536;
        const char* occluder_packet_end = world_packet_end;
#if PS1_OCCLUDER_DEBUG_VISUALIZE
        int tri_budget = 512;
#else
        int tri_budget = 128;
#endif
        if (world_packet_end - pri > packet_reserve)
            occluder_packet_end = world_packet_end - packet_reserve;
        for (i = 0; i < s_occluder_count; ++i) {
            const ps1_entity* ent = ps1_scene_entity(s_occluder_entities[i]);
            if (!ent || !ent->mesh_enabled || !ent->prerender_occluder)
                continue;
            if (tri_budget <= 0 || (const char*)pri >= occluder_packet_end)
                break;
            total_added += draw_prerender_occluder(
                &pri, occluder_packet_end, ot, ent, s_occluder_entities[i], i,
                background_index,
                &actor_mask, &tri_budget);
        }
    }

    /* Animated character meshes get first claim on the reserved packet area.
     * Rigid-bone characters are commonly appended after hundreds of collider
     * entities, so entity order must never decide whether the actor exists. */
    for (i = 0; i < s_animated_count; ++i) {
        const ps1_entity* ent = ps1_scene_entity(s_animated_entities[i]);
        if (!ent || !ent->mesh_enabled)
            continue;
        total_added += draw_entity(&pri, world_packet_end, ot, ent, 0);
        if ((const char*)pri >= world_packet_end) {
            any_overflow = 1;
            break;
        }
    }

    /* Remaining file meshes follow after animated actors. */
    for (i = 0; i < s_custom_count; ++i) {
        const ps1_entity* ent = ps1_scene_entity(s_custom_entities[i]);
        if (!ent || !ent->mesh_enabled)
            continue;
        const int added = draw_entity(&pri, world_packet_end, ot, ent, 0);
        total_added += added;
        if ((const char*)(pri) >= world_packet_end) {
            any_overflow = 1;
            break;
        }
    }
    for (i = 0; i < s_primitive_count; ++i) {
        const ps1_entity* ent = ps1_scene_entity(s_primitive_entities[i]);
        if (!ent || !ent->mesh_enabled)
            continue;
        const int added = draw_entity(&pri, world_packet_end, ot, ent, 0);
        total_added += added;
        if ((const char*)(pri) >= world_packet_end) {
            any_overflow = 1;
            break;
        }
    }
    for (i = 0; i < s_view_model_count; ++i) {
        const ps1_entity* ent = ps1_scene_entity(s_view_model_entities[i]);
        if (!ent || !ent->mesh_enabled)
            continue;
        const int added = draw_entity(&pri, world_packet_end, ot, ent, 1);
        total_added += added;
        if ((const char*)(pri) >= world_packet_end) {
            any_overflow = 1;
            break;
        }
    }

    ps1_ui_render(ot, &pri, packet_end);
    *nextpri = pri;
    if (s_occluder_camera_cooldown > 0)
        --s_occluder_camera_cooldown;

    /* One-shot on-screen hint if nothing was drawn or we overflowed the packet buffer. */
    {
        static int s_logged = 0;
        if (!s_logged && (total_added == 0 || any_overflow)) {
            /* host_log is in host.c, but render.c doesn't include it; use FntPrint directly. */
            FntPrint(-1, "[PS1] tris=%d overflow=%d\n", total_added, any_overflow);
            s_logged = 1;
        }
    }
}
