#include "render.h"
#include "scene.h"
#include "textures.h"
#include "fixedp.h"
#include "lookat.h"
#include "ui.h"
#include "lighting.h"
#include "color_grading.h"
#include "plane_tiles.h"
#include "viewmodel_projection.h"
#include "clip_math.h"
#include "projection_safety.h"
#include "watertight_projection.h"
#include "affine_subdivision.h"

#include <stdint.h>
#include <string.h>
#include <psxapi.h>
#include <psxgpu.h>
#include "mipsync_gte.h"
#if defined(__has_include)
#if __has_include("../generated/render_limits.h")
#include "../generated/render_limits.h"
#endif
#endif

#define OT_LEN 2048
#define PS1_WORLD_OT_MIN 2


/* Lighting/fog calculations use the editor's 0..255 colour scale. Convert
 * only when submitting a modulated texture: the GPU's neutral multiplier is
 * 128, whereas an untextured polygon writes RGB directly. UI owns its own
 * conversion in ui.c; raw background textures bypass modulation entirely. */
static void add_world_primitive(uint32_t* bucket, void* primitive) {
    uint8_t* bytes = (uint8_t*)primitive;
    const uint8_t code = bytes[7];
    if ((code & 0xe5u) == 0x24u) {
        int vertex;
        const int vertices = (code & 0x10u) ? ((code & 8u) ? 4 : 3) : 1;
        for (vertex = 0; vertex < vertices; ++vertex) {
            uint8_t* color = bytes + 4 + vertex * 12;
            color[0] = (uint8_t)((color[0] + 1u) >> 1);
            color[1] = (uint8_t)((color[1] + 1u) >> 1);
            color[2] = (uint8_t)((color[2] + 1u) >> 1);
        }
    }
    addPrim(bucket, primitive);
}

/* FBX winding is preserved at export (no runtime backface pass). */
/* 1 editor world unit -> 64 PSX/GTE units (matches typical demo scales). */
#define PSX_UNITS_PER_WORLD 64
/* PSX fpscam default forward is +Z; Unity cameras look down -Z. */
#define PSX_YAW_UNITY_OFFSET 2048
#define PS1_VIEWMODEL_VERTEX_CACHE_MAX 2048
#define PS1_VIEWMODEL_TRI_CACHE_MAX 4096
#define PS1_RIGID_VERTEX_CACHE_MAX 2048
#define PS1_STATIC_VERTEX_CACHE_MAX 4096
/* New exports size these pools for the scene's instances and largest clip
 * frames. Keep the full capacity for older generated data without limits. */
#ifndef PS1_STATIC_VERTEX_CACHE_TOTAL
#define PS1_STATIC_VERTEX_CACHE_TOTAL 16384
#endif
#ifndef PS1_STATIC_TRI_CACHE_TOTAL
#define PS1_STATIC_TRI_CACHE_TOTAL 16384
#endif
#define PS1_STATIC_ENTITY_CACHE_MAX 64
#define PS1_STATIC_TOPOLOGY_CACHE_MAX 32
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

#define SEAM_FILL_EXPAND4(x0, y0, x1, y1, x2, y2, x3, y3) \
    do { \
        int _cx = ((int)(x0) + (int)(x1) + (int)(x2) + (int)(x3)) >> 2; \
        int _cy = ((int)(y0) + (int)(y1) + (int)(y2) + (int)(y3)) >> 2; \
        if ((x0) < _cx) (x0)--; else if ((x0) > _cx) (x0)++; \
        if ((y0) < _cy) (y0)--; else if ((y0) > _cy) (y0)++; \
        if ((x1) < _cx) (x1)--; else if ((x1) > _cx) (x1)++; \
        if ((y1) < _cy) (y1)--; else if ((y1) > _cy) (y1)++; \
        if ((x2) < _cx) (x2)--; else if ((x2) > _cx) (x2)++; \
        if ((y2) < _cy) (y2)--; else if ((y2) > _cy) (y2)++; \
        if ((x3) < _cx) (x3)--; else if ((x3) > _cx) (x3)++; \
        if ((y3) < _cy) (y3)--; else if ((y3) > _cy) (y3)++; \
    } while(0)

static int mesh_triangle_otz_fine(int32_t avsz) {
    int otz;
    otz = (int)(avsz >> 1);
    if (otz < PS1_WORLD_OT_MIN)
        return PS1_WORLD_OT_MIN;
    if (otz >= OT_LEN)
        return OT_LEN - 1;
    return otz;
}

static int floor_surface_otz(const ps1_entity* ent, const ps1_mesh_tri* triangle,
                             int current_otz, int32_t z0, int32_t z1,
                             int32_t z2, int32_t z3) {
    int32_t farthest_z;
    int farthest_otz;
    int horizontal_world_surface;
    if (!ent || !triangle)
        return current_otz;

    horizontal_world_surface =
        (ent->render_preset == PS1_RENDER_PRESET_PROP ||
         ent->render_preset == PS1_RENDER_PRESET_CORRIDOR) &&
        (triangle->ny >= 3584 || triangle->ny <= -3584);
    if (ent->render_preset != PS1_RENDER_PRESET_FLOOR && !horizontal_world_surface)
        return current_otz;

    /* A large ground triangle can span from the camera to the horizon. Its
     * average depth may therefore land in front of a character standing on
     * it, which is impossible to resolve with the PS1's painter-style OT.
     * Submit floor-like surfaces at their farthest vertex depth: the ground
     * is painted first and actors/props then remain in front without forcing
     * render-only subdivision. */
    farthest_z = z0;
    if (z1 > farthest_z) farthest_z = z1;
    if (z2 > farthest_z) farthest_z = z2;
    if (z3 > farthest_z) farthest_z = z3;
    farthest_otz = (int)(farthest_z >> 3);
    if (farthest_otz < PS1_WORLD_OT_MIN)
        farthest_otz = PS1_WORLD_OT_MIN;
    else if (farthest_otz >= OT_LEN)
        farthest_otz = OT_LEN - 1;
    return farthest_otz > current_otz ? farthest_otz : current_otz;
}

static int ascii_contains_folded(const char* text, const char* needle) {
    int i;
    if (!text || !needle || !needle[0])
        return 0;
    for (; *text; ++text) {
        for (i = 0; needle[i]; ++i) {
            char a = text[i];
            char b = needle[i];
            if (!a)
                return 0;
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b)
                break;
        }
        if (!needle[i])
            return 1;
    }
    return 0;
}

static int character_shell_ot_bias(const ps1_entity* ent) {
    if (!ent || !ent->name)
        return 0;
    /* PS1 has no Z-buffer. Use only a tie-breaking offset for headwear: a
     * larger whole-mesh offset also drags the hat's inner/back surfaces in
     * front of the face even when their real camera depth is farther away. */
    if (ascii_contains_folded(ent->name, "hat") ||
        ascii_contains_folded(ent->name, "helmet") ||
        ascii_contains_folded(ent->name, "headwear") ||
        ascii_contains_folded(ent->name, "headdress"))
        return 1;
    if (ascii_contains_folded(ent->name, "hair"))
        return 3;
    return 0;
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
static int32_t s_normal_scale_q12[3] = { ONE, ONE, ONE };
static ps1_lighting_light s_scene_lights[PS1_LIGHT_LIMIT];
static unsigned int s_scene_light_count;
static uint32_t s_scene_light_revision;
static MATRIX s_shading_world, s_shading_rotation;
static const ps1_mesh* s_shading_mesh;
static const ps1_mesh* s_shading_next_mesh;
static uint8_t s_shading_lerp_q8;
static uint8_t s_shading_use_probe;
static ps1_lighting_light s_shading_probe_lights[PS1_LIGHT_LIMIT];
static unsigned int s_shading_probe_count;
static const ps1_mesh* s_shading_probe_source;
static void transform_point(const MATRIX* m, const SVECTOR* in, VECTOR* out);
static void unity_euler_to_psx_object(fix16_t pitch, fix16_t yaw, fix16_t roll, SVECTOR* out);

static void shade_scene_normal(const SVECTOR* normal, const SVECTOR* vertex,
    uint8_t base_r, uint8_t base_g, uint8_t base_b,
    uint8_t* out_r, uint8_t* out_g, uint8_t* out_b);

static MATRIX s_cam_mtx;
static const ps1_entity* s_render_camera_source = 0;
static int     s_initialized = 0;
static int     s_screen_w = 320;
static int     s_screen_h = 240;
static int     s_geom_screen = 160;
static int     s_near_z = 16;
static int     s_far_z = 6400;
static int     s_distance_cull_z = 0;
static int     s_frustum_cull_margin = 0;
static uint8_t s_distance_cull_enabled = 0;
static uint8_t s_distance_cull_meshes = 1;
static uint8_t s_distance_cull_skinned = 1;
static uint8_t s_frustum_cull_enabled = 0;
static uint8_t s_frustum_cull_meshes = 1;
static uint8_t s_frustum_cull_skinned = 1;
static int     s_fog_start_z = 0;
static int     s_fog_end_z = 1;
static int     s_fog_solid_z = 1;
static int     s_fog_scale_q12 = 0;
static uint8_t s_discard_solid_fog_polygons = 0;
static uint16_t s_occluder_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_animated_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_character_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_corridor_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_custom_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_primitive_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_view_model_entities[PS1_RENDER_ENTITY_LIST_MAX];
static uint16_t s_occluder_count;
static uint16_t s_animated_count;
static uint16_t s_character_count;
static uint16_t s_corridor_count;
static uint16_t s_custom_count;
static uint16_t s_primitive_count;
static uint16_t s_view_model_count;
static unsigned int s_indexed_entity_count;

/* Baked vertex-animation characters are exported as several material parts.
 * Every part carries the same root position/rotation, so rebuilding its Euler
 * and light matrices twelve times per frame is pure duplicate CPU work. */
typedef struct {
    int valid;
    int16_t root_index;
    fix16_t position[3];
    fix16_t rotation[3];
    VECTOR psx_position;
    MATRIX rotation_matrix;
    MATRIX light_matrix;
    int light_matrix_valid;
} ps1_shared_character_transform;

static ps1_shared_character_transform s_shared_character_transform;

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

static int corridor_near_z(void) {
    int z = s_near_z >> 1;
    if (z < 4)
        z = 4;
    return z;
}

static int entity_is_viewmodel(const ps1_entity* ent) {
    return ent && ent->render_preset == PS1_RENDER_PRESET_VIEWMODEL;
}

static const ps1_entity* source_entity_by_id(uint32_t id) {
    unsigned int i;
    for (i = 0; i < g_ps1_scene.entity_count; ++i) {
        const ps1_entity* source = ps1_scene_source_entity(i);
        if (source && source->id == id)
            return source;
    }
    return 0;
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
    s_character_count = 0;
    s_corridor_count = 0;
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
        if (entity_is_viewmodel(ent)) {
            s_view_model_entities[s_view_model_count++] = (uint16_t)i;
            continue;
        }
        if (ent->render_preset == PS1_RENDER_PRESET_CHARACTER) {
            s_character_entities[s_character_count++] = (uint16_t)i;
            continue;
        }
        if (ent->render_preset == PS1_RENDER_PRESET_CORRIDOR) {
            s_corridor_entities[s_corridor_count++] = (uint16_t)i;
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
    int r;
    int g;
    int b;
} ps1_clip_vert;

/* Keep the original software camera-space projection for viewmodels.  Unlike
 * world geometry it clamps against the much closer viewmodel near plane and
 * preserves the authored camera-relative position correction.  The result is
 * still cached once per vertex, while triangle lighting and redundant vertex
 * animation work remain removed from the hot loop. */
static VECTOR s_viewmodel_vertex_cache[PS1_VIEWMODEL_VERTEX_CACHE_MAX];
static int32_t s_viewmodel_screen_cache[PS1_VIEWMODEL_VERTEX_CACHE_MAX];
static int32_t s_viewmodel_depth_cache[PS1_VIEWMODEL_VERTEX_CACHE_MAX];
static uint8_t s_viewmodel_face_light_cache[PS1_VIEWMODEL_TRI_CACHE_MAX][3];
static const ps1_entity* s_viewmodel_light_entity = 0;
static const ps1_mesh_tri* s_viewmodel_light_tris = 0;
static MATRIX s_viewmodel_light_matrix;
static MATRIX s_viewmodel_lighting_world;
static int s_shading_unlit;
static uint32_t s_viewmodel_light_revision;
static const SVECTOR* s_viewmodel_lighting_vertices;
static int32_t s_viewmodel_normal_scale_q12[3];
static uint8_t s_viewmodel_light_base_r = 0;
static uint8_t s_viewmodel_light_base_g = 0;
static uint8_t s_viewmodel_light_base_b = 0;
static uint8_t s_viewmodel_light_valid = 0;
static int32_t s_rigid_screen_cache[PS1_RIGID_VERTEX_CACHE_MAX];
static int32_t s_rigid_depth_cache[PS1_RIGID_VERTEX_CACHE_MAX];
static int32_t s_static_screen_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
static int32_t s_static_depth_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
/* Unclamped camera-space coordinates are shared by near-plane clipping and
 * conservative frustum rejection. GTE screen/depth output alone loses signed
 * Z and saturates XY for the large triangles adjacent to the camera. */
static VECTOR s_static_camera_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
static uint8_t s_static_outcode_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
static uint8_t s_static_projection_flags[PS1_STATIC_VERTEX_CACHE_TOTAL];
/* Fog is Q8 in the inclusive 0..256 range. uint8_t would wrap fully opaque
 * fog (256) back to zero and make distant cached meshes appear unfogged. */
static uint16_t s_static_fog_cache[PS1_STATIC_VERTEX_CACHE_TOTAL];
static uint8_t s_static_light_cache[PS1_STATIC_VERTEX_CACHE_TOTAL][3];
static uint8_t s_static_face_light_cache[PS1_STATIC_TRI_CACHE_TOTAL][3];
static uint8_t s_static_quad_valid[PS1_STATIC_TRI_CACHE_TOTAL];
static uint16_t s_static_visible_triangles[PS1_STATIC_TRI_CACHE_TOTAL];
static uint8_t s_static_safe_clip_cache[PS1_STATIC_TRI_CACHE_TOTAL];
#define PS1_VISIBILITY_BATCH_TRIANGLES 32
#define PS1_VISIBILITY_BATCH_MAX ((PS1_STATIC_TRI_CACHE_TOTAL + 31) / 32 + PS1_STATIC_TOPOLOGY_CACHE_MAX)
typedef struct {
    uint32_t vertex_offset;
    uint16_t vertex_count, triangle_begin, triangle_end;
    SVECTOR center;
    uint16_t extent[3];
    int8_t plane_winding;
} ps1_visibility_batch;
typedef struct {
    const ps1_mesh* mesh;
    uint16_t batch_offset, batch_count;
} ps1_visibility_topology;
static ps1_visibility_topology s_visibility_topology[PS1_STATIC_TOPOLOGY_CACHE_MAX];
static ps1_visibility_batch s_visibility_batches[PS1_VISIBILITY_BATCH_MAX];
static uint16_t s_visibility_vertices[PS1_STATIC_TRI_CACHE_TOTAL * 3];
static uint32_t s_visibility_vertex_used;
static uint16_t s_visibility_batch_used;
static uint8_t s_visibility_batch_outside[PS1_VISIBILITY_BATCH_MAX];
static uint8_t s_static_active_vertex[PS1_STATIC_VERTEX_CACHE_TOTAL];

typedef struct {
    const ps1_mesh* mesh;
    uint16_t triangle_offset;
    uint16_t triangle_count;
} ps1_static_topology_cache;

static ps1_static_topology_cache
    s_static_topology_caches[PS1_STATIC_TOPOLOGY_CACHE_MAX];
static uint16_t s_static_topology_triangle_used;

typedef struct {
    const ps1_entity* entity;
    const ps1_mesh* mesh;
    const ps1_mesh* next_mesh;
    const SVECTOR* lighting_norms;
    const SVECTOR* lighting_face_norms;
    const ps1_mesh_tri* lighting_tris;
    MATRIX projected_matrix;
    MATRIX light_matrix;
    int32_t normal_scale_q12[3];
    MATRIX lighting_world;
    uint32_t light_revision;
    const SVECTOR* lighting_vertices;
    const ps1_mesh* lighting_next_mesh;
    uint8_t lighting_lerp_q8;
    const ps1_mesh* lighting_probe_source;
    uint16_t vertex_offset;
    uint16_t vertex_capacity;
    uint16_t triangle_offset;
    uint16_t triangle_capacity;
    int geom_screen;
    int safe_clip_near_z;
    int safe_clip_far_z;
    uint8_t vertex_lerp_q8;
    uint8_t base_r, base_g, base_b;
    uint8_t needs_safe_clip;
    uint8_t visible_list_valid;
    uint16_t visible_triangle_count;
    uint8_t projection_valid;
    uint8_t lighting_valid;
    uint8_t lighting_unlit;
} ps1_static_entity_cache;

static ps1_static_entity_cache s_static_entity_caches[PS1_STATIC_ENTITY_CACHE_MAX];
static uint16_t s_static_vertex_cache_used;
static uint16_t s_static_triangle_cache_used;

static void reset_static_entity_caches(void) {
    memset(s_visibility_topology, 0, sizeof(s_visibility_topology));
    s_visibility_vertex_used = 0;
    s_visibility_batch_used = 0;
    int i;
    s_static_vertex_cache_used = 0;
    s_static_triangle_cache_used = 0;
    s_static_topology_triangle_used = 0;
    for (i = 0; i < PS1_STATIC_ENTITY_CACHE_MAX; ++i) {
        s_static_entity_caches[i].entity = 0;
        s_static_entity_caches[i].projection_valid = 0;
        s_static_entity_caches[i].lighting_valid = 0;
    }
    for (i = 0; i < PS1_STATIC_TOPOLOGY_CACHE_MAX; ++i)
        s_static_topology_caches[i].mesh = 0;
}

/* Source topology is shared by instances. Group nearby exported faces and
 * retain their unique position indices. An entire group can be rejected only
 * when every vertex is outside the same frustum plane. */
static const ps1_visibility_topology* visibility_topology_for(const ps1_mesh* mesh) {
    ps1_visibility_topology* free_entry = 0;
    const unsigned batches = (mesh->tri_count + PS1_VISIBILITY_BATCH_TRIANGLES - 1) / PS1_VISIBILITY_BATCH_TRIANGLES;
    unsigned i;
    for(i=0;i<PS1_STATIC_TOPOLOGY_CACHE_MAX;++i) {
        if(s_visibility_topology[i].mesh == mesh) return &s_visibility_topology[i];
        if(!s_visibility_topology[i].mesh && !free_entry) free_entry=&s_visibility_topology[i];
    }
    if(!free_entry || s_visibility_batch_used+batches>PS1_VISIBILITY_BATCH_MAX ||
       s_visibility_vertex_used+(uint32_t)mesh->tri_count*3>PS1_STATIC_TRI_CACHE_TOTAL*3)
        return 0;
    free_entry->mesh=mesh;free_entry->batch_offset=s_visibility_batch_used;free_entry->batch_count=batches;
    for(i=0;i<batches;++i) {
        ps1_visibility_batch* batch=&s_visibility_batches[s_visibility_batch_used++];
        unsigned end=(i+1)*PS1_VISIBILITY_BATCH_TRIANGLES, ti;
        if(end>mesh->tri_count)end=mesh->tri_count;
        batch->triangle_begin=i*PS1_VISIBILITY_BATCH_TRIANGLES;batch->triangle_end=end;
        batch->vertex_offset=s_visibility_vertex_used;batch->vertex_count=0;
        for(ti=batch->triangle_begin;ti<end;++ti) {
            const ps1_mesh_tri* face=&mesh->tris[ti];
            const uint16_t indices[3]={face->i0,face->i1,face->i2};
            unsigned corner;
            for(corner=0;corner<3;++corner) {
                uint16_t p=indices[corner],j;
                if(p>=mesh->vert_count)continue;
                if(mesh->position_refs && mesh->position_refs[p]<mesh->vert_count)p=mesh->position_refs[p];
                for(j=0;j<batch->vertex_count;++j)
                    if(s_visibility_vertices[batch->vertex_offset+j]==p)break;
                if(j==batch->vertex_count) {
                    s_visibility_vertices[s_visibility_vertex_used++]=p;
                    ++batch->vertex_count;
                }
            }
        }
        int minimum[3] = {32767,32767,32767}, maximum[3] = {-32768,-32768,-32768};
        for (unsigned vi = 0; vi < batch->vertex_count; ++vi) {
            const SVECTOR* vertex = &mesh->verts[s_visibility_vertices[batch->vertex_offset+vi]];
            const int coordinates[3] = {vertex->vx,vertex->vy,vertex->vz};
            for (unsigned axis = 0; axis < 3; ++axis) {
                if (coordinates[axis] < minimum[axis]) minimum[axis] = coordinates[axis];
                if (coordinates[axis] > maximum[axis]) maximum[axis] = coordinates[axis];
            }
        }
        int center[3];
        for (unsigned axis = 0; axis < 3; ++axis) {
            center[axis] = (minimum[axis]+maximum[axis])/2;
            const int a = center[axis]-minimum[axis], b = maximum[axis]-center[axis];
            batch->extent[axis] = a > b ? a : b;
        }
        batch->center = (SVECTOR){center[0],center[1],center[2],0};
        batch->plane_winding = 0;
        if (!batch->extent[1] && batch->vertex_count) {
            int sign = 0;
            for (unsigned t = batch->triangle_begin; t < batch->triangle_end; ++t) {
                const ps1_mesh_tri* face = &mesh->tris[t];
                if (face->i0 >= mesh->vert_count || face->i1 >= mesh->vert_count || face->i2 >= mesh->vert_count) {sign=0;break;}
                const SVECTOR* a=&mesh->verts[face->i0];
                const SVECTOR* b=&mesh->verts[face->i1];
                const SVECTOR* c=&mesh->verts[face->i2];
                const int64_t ny=(int64_t)(b->vz-a->vz)*(c->vx-a->vx)-(int64_t)(b->vx-a->vx)*(c->vz-a->vz);
                const int s=ny<0 ? -1 : ny>0 ? 1 : 0;
                if (!s || (sign && s!=sign)) {sign=0;break;}
                sign=s;
            }
            batch->plane_winding=sign;
        }
    }
    return free_entry;
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

static void include_vertex_anim_cache_range(uint16_t first, uint16_t count,
                                            uint16_t* max_vertices,
                                            uint16_t* max_triangles) {
    uint32_t frame;
    if (first == 0 || count == 0 || !max_vertices || !max_triangles)
        return;
    for (frame = 0; frame < count; ++frame) {
        const uint32_t index = (uint32_t)first - 1u + frame;
        const ps1_mesh* candidate;
        if (index >= g_ps1_mesh_count)
            break;
        candidate = &g_ps1_meshes[index];
        if (candidate->vert_count > *max_vertices)
            *max_vertices = candidate->vert_count;
        if (candidate->tri_count > *max_triangles)
            *max_triangles = candidate->tri_count;
    }
}

/* Selective backface preservation can give Idle/Run/Jump a slightly
 * different triangle count. Reserve one cache region large enough for every
 * clip up front; otherwise switching to Run permanently drops that character
 * part onto the legacy per-triangle transform path. */
static void static_entity_cache_capacity(const ps1_entity* entity,
                                         const ps1_mesh* mesh,
                                         uint16_t* max_vertices,
                                         uint16_t* max_triangles) {
    *max_vertices = mesh->vert_count;
    *max_triangles = mesh->tri_count;
    if (!entity || entity->vertex_anim_frame_count == 0)
        return;
    include_vertex_anim_cache_range(
        entity->vertex_idle_first_mesh_index, entity->vertex_idle_frame_count,
        max_vertices, max_triangles);
    include_vertex_anim_cache_range(
        entity->vertex_walk_first_mesh_index, entity->vertex_walk_frame_count,
        max_vertices, max_triangles);
    include_vertex_anim_cache_range(
        entity->vertex_aim_first_mesh_index, entity->vertex_aim_frame_count,
        max_vertices, max_triangles);
    include_vertex_anim_cache_range(
        entity->vertex_trigger_first_mesh_index, entity->vertex_trigger_frame_count,
        max_vertices, max_triangles);
}

static ps1_static_entity_cache* static_entity_cache_for(
        const ps1_entity* entity, const ps1_mesh* mesh) {
    int i;
    ps1_static_entity_cache* free_entry = 0;
    uint16_t vertex_capacity;
    uint16_t triangle_capacity;
    for (i = 0; i < PS1_STATIC_ENTITY_CACHE_MAX; ++i) {
        ps1_static_entity_cache* entry = &s_static_entity_caches[i];
        if (entry->entity == entity) {
            if (mesh->vert_count <= entry->vertex_capacity &&
                mesh->tri_count <= entry->triangle_capacity)
                return entry;
            return 0;
        }
        if (!entry->entity && !free_entry)
            free_entry = entry;
    }
    static_entity_cache_capacity(
        entity, mesh, &vertex_capacity, &triangle_capacity);
    if (!free_entry ||
        s_static_vertex_cache_used + vertex_capacity > PS1_STATIC_VERTEX_CACHE_TOTAL ||
        s_static_triangle_cache_used + triangle_capacity > PS1_STATIC_TRI_CACHE_TOTAL)
        return 0;
    free_entry->entity = entity;
    free_entry->mesh = 0;
    free_entry->next_mesh = 0;
    free_entry->lighting_norms = 0;
    free_entry->lighting_face_norms = 0;
    free_entry->lighting_tris = 0;
    free_entry->vertex_offset = s_static_vertex_cache_used;
    free_entry->vertex_capacity = vertex_capacity;
    free_entry->triangle_offset = s_static_triangle_cache_used;
    free_entry->triangle_capacity = triangle_capacity;
    free_entry->safe_clip_near_z = 0;
    free_entry->needs_safe_clip = 0;
    free_entry->visible_list_valid = 0;
    free_entry->visible_triangle_count = 0;
    free_entry->projection_valid = 0;
    free_entry->lighting_valid = 0;
    s_static_vertex_cache_used += vertex_capacity;
    s_static_triangle_cache_used += triangle_capacity;
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

static int16_t clamp_svector_i16(int32_t value) {
    if (value < -32768)
        return -32768;
    if (value > 32767)
        return 32767;
    return (int16_t)value;
}

static SVECTOR lerp_mesh_svector_q8(const SVECTOR* a, const ps1_mesh* current,
                                    const SVECTOR* b, const ps1_mesh* next,
                                    uint8_t t) {
    SVECTOR out;
    int32_t bx = b->vx;
    int32_t by = b->vy;
    int32_t bz = b->vz;
    const int32_t current_scale = current && current->scale_q12 != 0
        ? current->scale_q12 : 1;
    const int32_t next_scale = next && next->scale_q12 != 0
        ? next->scale_q12 : current_scale;
    /* The draw matrix uses the current mesh's normalization scale. Convert
     * the target vertex into that coordinate space before interpolation so
     * clips exported with different per-sequence scales retain world size. */
    if (next_scale != current_scale) {
        bx = (int32_t)(((int64_t)bx * next_scale) / current_scale);
        by = (int32_t)(((int64_t)by * next_scale) / current_scale);
        bz = (int32_t)(((int64_t)bz * next_scale) / current_scale);
    }
    out.vx = clamp_svector_i16(((int32_t)a->vx * (256 - t) + bx * t) >> 8);
    out.vy = clamp_svector_i16(((int32_t)a->vy * (256 - t) + by * t) >> 8);
    out.vz = clamp_svector_i16(((int32_t)a->vz * (256 - t) + bz * t) >> 8);
    out.pad = 0;
    return out;
}

/* The editor uses inverse-transpose(model) for normals. A rotation-only
 * light matrix needs the reciprocal scale and renormalization first. */
static SVECTOR scaled_shading_normal(const SVECTOR* normal) {
    SVECTOR result = *normal;
    if (s_normal_scale_q12[0] != ONE || s_normal_scale_q12[1] != ONE ||
        s_normal_scale_q12[2] != ONE) {
        VECTOR corrected;
        corrected.vx = ((int32_t)normal->vx * s_normal_scale_q12[0]) >> 12;
        corrected.vy = ((int32_t)normal->vy * s_normal_scale_q12[1]) >> 12;
        corrected.vz = ((int32_t)normal->vz * s_normal_scale_q12[2]) >> 12;
        VectorNormalS(&corrected, &result);
    }
    if (result.vx == 0 && result.vy == 0 && result.vz == 0)
        result.vy = ONE;
    return result;
}

static void shade_scene_normal(const SVECTOR* normal, const SVECTOR* vertex,
    uint8_t base_r, uint8_t base_g, uint8_t base_b,
    uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    if (s_shading_unlit) { *out_r=base_r; *out_g=base_g; *out_b=base_b; return; }
    SVECTOR corrected = scaled_shading_normal(normal);
    int16_t world_normal[3];
    int32_t world_position[3];
    uint8_t base[3] = { base_r, base_g, base_b }, result[3];
    VECTOR position;
    SVECTOR interpolated;
    int axis;
    if (!s_shading_use_probe && vertex && s_shading_mesh && s_shading_next_mesh && s_shading_lerp_q8 &&
        (uintptr_t)vertex >= (uintptr_t)s_shading_mesh->verts &&
        (uintptr_t)vertex < (uintptr_t)(s_shading_mesh->verts + s_shading_mesh->vert_count)) {
        unsigned int index = (unsigned int)(vertex - s_shading_mesh->verts);
        interpolated = lerp_mesh_svector_q8(vertex, s_shading_mesh,
            &s_shading_next_mesh->verts[index], s_shading_next_mesh, s_shading_lerp_q8);
        vertex = &interpolated;
    }
    if (s_shading_use_probe) {
        position.vx = position.vy = position.vz = 0;
    } else if (vertex) transform_point(&s_shading_world, vertex, &position);
    else {
        position.vx = s_shading_world.t[0];
        position.vy = s_shading_world.t[1];
        position.vz = s_shading_world.t[2];
    }
    world_position[0] = position.vx;
    world_position[1] = position.vy;
    world_position[2] = position.vz;
    for (axis = 0; axis < 3; ++axis)
        world_normal[axis] = (int16_t)((
            s_shading_rotation.m[axis][0] * (int32_t)corrected.vx +
            s_shading_rotation.m[axis][1] * (int32_t)corrected.vy +
            s_shading_rotation.m[axis][2] * (int32_t)corrected.vz) >> 12);
    ps1_lighting_evaluate(s_shading_use_probe ? s_shading_probe_lights : s_scene_lights,
        s_shading_use_probe ? s_shading_probe_count : s_scene_light_count,
        world_position, world_normal, base, result);
    *out_r = result[0]; *out_g = result[1]; *out_b = result[2];
}

/* Shade a mesh-local normal (12.4) with the current GTE light matrices. */
static void gte_shade_normal_vec(SVECTOR* nSafe, uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                 uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    if (s_shading_unlit) { *out_r=base_r; *out_g=base_g; *out_b=base_b; return; }
    uint8_t lit[4];
    if (s_scene_light_count) {
        /* Rigid animation keeps its existing 27 normal bins. Evaluate point
         * lights at the small rigid part's origin, not the character root. */
        shade_scene_normal(nSafe, 0, base_r, base_g, base_b, out_r, out_g, out_b);
        return;
    }

    const SVECTOR corrected = scaled_shading_normal(nSafe);

    lit[0] = 128;
    lit[1] = 128;
    lit[2] = 128;
    lit[3] = 0;
    gte_ldrgb(lit);
    gte_ldv0(&corrected);
    gte_ncs();
    gte_strgb(lit);

    {
        int r = (int)(((uint16_t)lit[0] * (uint16_t)base_r) >> 8);
        int g = (int)(((uint16_t)lit[1] * (uint16_t)base_g) >> 8);
        int b = (int)(((uint16_t)lit[2] * (uint16_t)base_b) >> 8);
        *out_r = (uint8_t)r;
        *out_g = (uint8_t)g;
        *out_b = (uint8_t)b;
    }
}

static void gte_shade_normal(const SVECTOR* n, const SVECTOR* vertex, uint8_t base_r, uint8_t base_g, uint8_t base_b,
                             uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    if (s_shading_unlit) { *out_r=base_r; *out_g=base_g; *out_b=base_b; return; }
    if (s_scene_light_count) {
        shade_scene_normal(n, vertex, base_r, base_g, base_b, out_r, out_g, out_b);
        return;
    }
    SVECTOR nSafe = *n;
    gte_shade_normal_vec(&nSafe, base_r, base_g, base_b, out_r, out_g, out_b);
}

static void gte_shade_textured_smooth_normal(const SVECTOR* n, const SVECTOR* vertex,
                                             uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                             uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    gte_shade_normal(n, vertex, base_r, base_g, base_b, out_r, out_g, out_b);
}

static SVECTOR mesh_face_center(const ps1_mesh* mesh, const ps1_mesh_tri* face) {
    SVECTOR center = { 0, 0, 0, 0 };
    /* Every caller uses this position only for lighting. Unlit does not
     * need triangle-centre divisions or animated-position interpolation. */
    if (s_shading_unlit)
        return center;
    if (face->i0 < mesh->vert_count && face->i1 < mesh->vert_count && face->i2 < mesh->vert_count) {
        const SVECTOR* a = &mesh->verts[face->i0];
        const SVECTOR* b = &mesh->verts[face->i1];
        const SVECTOR* c = &mesh->verts[face->i2];
        SVECTOR vertices[3];
        if (s_scene_light_count && !s_shading_use_probe && mesh == s_shading_mesh && s_shading_next_mesh && s_shading_lerp_q8) {
            vertices[0] = lerp_mesh_svector_q8(a, mesh, &s_shading_next_mesh->verts[face->i0], s_shading_next_mesh, s_shading_lerp_q8);
            vertices[1] = lerp_mesh_svector_q8(b, mesh, &s_shading_next_mesh->verts[face->i1], s_shading_next_mesh, s_shading_lerp_q8);
            vertices[2] = lerp_mesh_svector_q8(c, mesh, &s_shading_next_mesh->verts[face->i2], s_shading_next_mesh, s_shading_lerp_q8);
            a = &vertices[0]; b = &vertices[1]; c = &vertices[2];
        }
        center.vx = ((int32_t)a->vx + b->vx + c->vx) / 3;
        center.vy = ((int32_t)a->vy + b->vy + c->vy) / 3;
        center.vz = ((int32_t)a->vz + b->vz + c->vz) / 3;
    }
    return center;
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

static void viewmodel_cpu_shade_normal(const MATRIX* light_mtx, const SVECTOR* n, const SVECTOR* vertex,
                                       uint8_t base_r, uint8_t base_g, uint8_t base_b,
                                       uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
    if (s_shading_unlit) { *out_r=base_r; *out_g=base_g; *out_b=base_b; return; }
    const ps1_light* light = &g_ps1_scene.light;
    int32_t dot;
    int intensity;
    int r, g, b;
    const SVECTOR corrected = scaled_shading_normal(n);
    if (s_scene_light_count) {
        shade_scene_normal(n, vertex, base_r, base_g, base_b, out_r, out_g, out_b);
        return;
    }

    if (!light->enabled) {
        /* Same legacy directional preview as Renderer::LightData. */
        dot = (int32_t)corrected.vx * light_mtx->m[0][0] +
              (int32_t)corrected.vy * light_mtx->m[0][1] +
              (int32_t)corrected.vz * light_mtx->m[0][2];
        intensity = (int)(dot >> 12);
        if (intensity < 0) intensity = 0;
        if (intensity > ONE) intensity = ONE;
        r = 38 + (255 * intensity) / ONE;
        g = 36 + (242 * intensity) / ONE;
        b = 33 + (217 * intensity) / ONE;
    } else {
        dot = (int32_t)corrected.vx * light_mtx->m[0][0] +
              (int32_t)corrected.vy * light_mtx->m[0][1] +
              (int32_t)corrected.vz * light_mtx->m[0][2];
        intensity = (int)(dot >> 12);
        if (intensity < 0) intensity = 0;
        if (intensity > ONE) intensity = ONE;
        r = light->ambient_r + (int)(((int32_t)light->color_r * intensity) / ONE);
        g = light->ambient_g + (int)(((int32_t)light->color_g * intensity) / ONE);
        b = light->ambient_b + (int)(((int32_t)light->color_b * intensity) / ONE);
    }

    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;

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

static uint8_t __attribute__((noinline)) camera_outcode(const VECTOR* p, int near_z) {
    if ((uint32_t)p->vx + 1048575u <= 2097150u &&
        (uint32_t)p->vy + 1048575u <= 2097150u &&
        (uint32_t)p->vz + 1048575u <= 2097150u &&
        (unsigned)s_geom_screen <= 1023u &&
        (unsigned)s_screen_w <= 1024u && (unsigned)s_screen_h <= 1024u) {
        const int32_t x = s_geom_screen * p->vx;
        const int32_t y = s_geom_screen * p->vy;
        const int32_t z = p->vz;
        return (z < near_z ? 1 : 0) | (z > s_far_z ? 2 : 0) |
            (x < -(s_screen_w / 2 + 2) * z ? 4 : 0) |
            (x > (s_screen_w - s_screen_w / 2 + 2) * z ? 8 : 0) |
            (y < -(s_screen_h / 2 + 2) * z ? 16 : 0) |
            (y > (s_screen_h - s_screen_h / 2 + 2) * z ? 32 : 0);
    }
    const int64_t x = (int64_t)s_geom_screen * p->vx;
    const int64_t y = (int64_t)s_geom_screen * p->vy;
    const int64_t z = p->vz;
    uint8_t result = 0;
    if (z < near_z) result |= 1;
    if (z > s_far_z) result |= 2;
    /* Two pixels retain GTE rounding and the one-pixel seam expansion. */
    if (x < -(s_screen_w / 2 + 2) * z) result |= 4;
    if (x > (s_screen_w - s_screen_w / 2 + 2) * z) result |= 8;
    if (y < -(s_screen_h / 2 + 2) * z) result |= 16;
    if (y > (s_screen_h - s_screen_h / 2 + 2) * z) result |= 32;
    return result;
}

static void update_fog_solid_distance(void) {
    const int span = s_fog_end_z - s_fog_start_z;
    /* Keep the authored fade range unchanged. Texture removal happens half a
     * fade span farther away, where the already-completed fade hides the
     * unavoidable textured-to-solid transition on PS1 hardware. */
    s_fog_solid_z = s_fog_end_z + (span > 1 ? (span >> 1) : 1);
    if (s_fog_solid_z > s_far_z)
        s_fog_solid_z = s_far_z;
    s_fog_scale_q12 = span > 0
        ? (int)(((256 << 12) + (span >> 1)) / span) : 0;
}

static void apply_scene_lighting(void) {
    const ps1_light* l = &g_ps1_scene.light;
    unsigned int light_index;
    unsigned int light_count = g_ps1_scene.light_count;
    if (light_count > PS1_LIGHT_LIMIT) light_count = PS1_LIGHT_LIMIT;
    if (light_count != s_scene_light_count) ++s_scene_light_revision;
    s_scene_light_count = light_count;
    for (light_index = 0; light_index < light_count; ++light_index) {
        const ps1_scene_light* source = &g_ps1_scene.lights[light_index];
        const ps1_entity* entity = source->entity_index >= 0
            ? ps1_scene_entity((unsigned int)source->entity_index) : 0;
        const ps1_entity* authored = source->entity_index >= 0
            ? ps1_scene_source_entity((unsigned int)source->entity_index) : 0;
        ps1_lighting_light light = { 0 };
        int axis;
        light.type = source->type;
        light.range = world_to_psx(source->range);
        if (source->range > 0 && light.range <= 0) light.range = 1;
        light.cos_outer = source->cos_outer;
        light.cos_inner = source->cos_inner;
        for (axis = 0; axis < 3; ++axis) {
            light.position[axis] = world_to_psx(entity ? entity->position[axis] : source->position[axis]);
            light.direction[axis] = source->direction[axis];
            light.color_q12[axis] = source->color_q12[axis];
        }
        if (entity && authored && memcmp(entity->rotation, authored->rotation, sizeof(entity->rotation))) {
            MATRIX rotation;
            SVECTOR euler;
            unity_euler_to_psx_object(entity->rotation[0], entity->rotation[1], entity->rotation[2], &euler);
            RotMatrix(&euler, &rotation);
            for (axis = 0; axis < 3; ++axis) light.direction[axis] = rotation.m[axis][2];
        }
        if (memcmp(&s_scene_lights[light_index], &light, sizeof(light))) {
            s_scene_lights[light_index] = light;
            ++s_scene_light_revision;
        }
    }
    s_fog_start_z = world_to_psx(g_ps1_scene.fog_start);
    s_fog_end_z = world_to_psx(g_ps1_scene.fog_end);
    if (s_fog_end_z <= s_fog_start_z)
        s_fog_end_z = s_fog_start_z + 1;
    update_fog_solid_distance();
    if (l->enabled) {
        /* Ambient */
        gte_SetBackColor(l->ambient_r, l->ambient_g, l->ambient_b);

        /* Color matrix: column 0 = light color (12.4, ONE=4096). */
        const int32_t scale = ONE;
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
        /* Renderer::LightData fallback: colour (1,.95,.85), ambient .15,
         * surface-to-light direction normalize(.5,1,.3). */
        gte_SetBackColor(38, 36, 33);
        s_scene_color_mtx.m[0][0] = ONE;
        s_scene_color_mtx.m[1][0] = 3891;
        s_scene_color_mtx.m[2][0] = 3482;
        s_scene_color_mtx.m[0][1] = 0;   s_scene_color_mtx.m[1][1] = 0;   s_scene_color_mtx.m[2][1] = 0;
        s_scene_color_mtx.m[0][2] = 0;   s_scene_color_mtx.m[1][2] = 0;   s_scene_color_mtx.m[2][2] = 0;
        gte_SetColorMatrix(&s_scene_color_mtx);
        s_light_mtx.m[0][0] = 1769; s_light_mtx.m[0][1] = 3538; s_light_mtx.m[0][2] = 1061;
        s_light_mtx.m[1][0] = 0; s_light_mtx.m[1][1] = 0; s_light_mtx.m[1][2] = 0;
        s_light_mtx.m[2][0] = 0; s_light_mtx.m[2][1] = 0; s_light_mtx.m[2][2] = 0;
    }
}

static int deg_to_gte(fix16_t deg) {
    return (int)((int64_t)deg * 4096 / (360 * 65536));
}

static int world_to_psx(fix16_t v) {
    /* PSX_UNITS_PER_WORLD is 64, so Q16.16 -> PSX units is an exact shift.
     * Avoid the software 64-bit multiply in this hot conversion. */
    return (int)(v >> 10);
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

static int scene_fog_amount_q8(int32_t camera_z) {
    int amount;
    if (!g_ps1_scene.fog_enabled || camera_z <= s_fog_start_z)
        return 0;
    if (camera_z >= s_fog_end_z)
        return 256;
    amount = (int)(((camera_z - s_fog_start_z) * s_fog_scale_q12 + 2048) >> 12);
    if (amount < 0)
        return 0;
    if (amount > 256)
        return 256;
    return amount;
}

static int apply_scene_fog_amount_ret(int amount_q8,
                                       uint8_t* r, uint8_t* g, uint8_t* b) {
    if (amount_q8 <= 0 || !r || !g || !b)
        return amount_q8;
    if (amount_q8 >= 256) {
        *r = g_ps1_scene.fog_r;
        *g = g_ps1_scene.fog_g;
        *b = g_ps1_scene.fog_b;
        return amount_q8;
    }
    *r = (uint8_t)(((int)*r * (256 - amount_q8) +
                    (int)g_ps1_scene.fog_r * amount_q8 + 128) >> 8);
    *g = (uint8_t)(((int)*g * (256 - amount_q8) +
                    (int)g_ps1_scene.fog_g * amount_q8 + 128) >> 8);
    *b = (uint8_t)(((int)*b * (256 - amount_q8) +
                    (int)g_ps1_scene.fog_b * amount_q8 + 128) >> 8);
    return amount_q8;
}

static void apply_scene_fog_amount(int amount_q8,
                                   uint8_t* r, uint8_t* g, uint8_t* b) {
    apply_scene_fog_amount_ret(amount_q8, r, g, b);
}

static int is_triangle_fully_fogged(int32_t camera_z) {
    return g_ps1_scene.fog_enabled && camera_z >= s_fog_solid_z;
}

static int discard_solid_fog_triangle(int32_t z0, int32_t z1, int32_t z2) {
    return s_discard_solid_fog_polygons &&
           z0 >= s_fog_end_z && z1 >= s_fog_end_z && z2 >= s_fog_end_z;
}

static int discard_solid_fog_quad(int32_t z0, int32_t z1,
                                  int32_t z2, int32_t z3) {
    return s_discard_solid_fog_polygons &&
           z0 >= s_fog_end_z && z1 >= s_fog_end_z &&
           z2 >= s_fog_end_z && z3 >= s_fog_end_z;
}

static void apply_scene_fog(int32_t camera_z, uint8_t* r, uint8_t* g, uint8_t* b) {
    apply_scene_fog_amount(scene_fog_amount_q8(camera_z), r, g, b);
}

static void apply_scene_fog3(int32_t camera_z,
                             uint8_t* r0, uint8_t* g0, uint8_t* b0,
                             uint8_t* r1, uint8_t* g1, uint8_t* b1,
                             uint8_t* r2, uint8_t* g2, uint8_t* b2) {
    const int amount_q8 = scene_fog_amount_q8(camera_z);
    apply_scene_fog_amount(amount_q8, r0, g0, b0);
    apply_scene_fog_amount(amount_q8, r1, g1, b1);
    apply_scene_fog_amount(amount_q8, r2, g2, b2);
}

static void apply_scene_fog_vertex3(int32_t z0, int32_t z1, int32_t z2,
                                    uint8_t* r0, uint8_t* g0, uint8_t* b0,
                                    uint8_t* r1, uint8_t* g1, uint8_t* b1,
                                    uint8_t* r2, uint8_t* g2, uint8_t* b2) {
    if (!g_ps1_scene.fog_enabled)
        return;
    apply_scene_fog(z0, r0, g0, b0);
    apply_scene_fog(z1, r1, g1, b1);
    apply_scene_fog(z2, r2, g2, b2);
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
    out->r = 0;
    out->g = 0;
    out->b = 0;
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
    out->r = 0;
    out->g = 0;
    out->b = 0;
}

static ps1_clip_vert lerp_clip_vert(const ps1_clip_vert* a, const ps1_clip_vert* b, int near_z) {
    ps1_clip_vert out;
    const int dz = b->p.vz - a->p.vz;
    const int az = near_z - a->p.vz;
    if (dz == 0)
        return *a;
    out.p.vx = ps1_clip_lerp(a->p.vx, b->p.vx, az, dz);
    out.p.vy = ps1_clip_lerp(a->p.vy, b->p.vy, az, dz);
    out.p.vz = near_z;
    out.u = ps1_clip_lerp(a->u, b->u, az, dz);
    out.v = ps1_clip_lerp(a->v, b->v, az, dz);
    out.r = ps1_clip_lerp(a->r, b->r, az, dz);
    out.g = ps1_clip_lerp(a->g, b->g, az, dz);
    out.b = ps1_clip_lerp(a->b, b->b, az, dz);
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

static int clip_world_depth(const ps1_clip_vert in[3], ps1_clip_vert out[6],
                            int near_z, int far_z) {
    if (in[0].p.vz >= near_z && in[0].p.vz <= far_z &&
        in[1].p.vz >= near_z && in[1].p.vz <= far_z &&
        in[2].p.vz >= near_z && in[2].p.vz <= far_z) {
        /* Preserve the two original clipping passes' cyclic vertex order. */
        out[0] = in[2]; out[1] = in[0]; out[2] = in[1];
        return 3;
    }
    ps1_clip_vert near_clipped[4];
    const int count = clip_triangle_near(in, near_clipped, near_z);
    int output_count = 0;
    for (int i = 0; i < count; ++i) {
        const ps1_clip_vert* a = &near_clipped[i];
        const ps1_clip_vert* b = &near_clipped[(i + 1) % count];
        const int a_inside = a->p.vz <= far_z;
        const int b_inside = b->p.vz <= far_z;
        if (a_inside != b_inside) out[output_count++] = lerp_clip_vert(a, b, far_z);
        if (b_inside) out[output_count++] = *b;
    }
    return output_count;
}

static void project_clip_vert_wide(const ps1_clip_vert* in,
                                   ps1_screen_clip_vert* out, int min_z) {
    int z = in->p.vz;
    int64_t sx;
    int64_t sy;
    if (z < min_z)
        z = min_z;
    const uint32_t factor = ps1_projection_factor(s_geom_screen, z);
    sx = ps1_projection_axis(in->p.vx, factor, s_screen_w / 2);
    sy = ps1_projection_axis(in->p.vy, factor, s_screen_h / 2);
    /* Values farther away than this are equivalent for viewport clipping.
     * Bounding them keeps the following edge interpolation comfortably in
     * 64-bit range even for extreme authored scales. */
    if (sx < -1048576) sx = -1048576;
    if (sx >  1048576) sx =  1048576;
    if (sy < -1048576) sy = -1048576;
    if (sy >  1048576) sy =  1048576;
    out->x = (int32_t)sx;
    out->y = (int32_t)sy;
    out->z = in->p.vz;
    out->u = in->u;
    out->v = in->v;
    out->r = in->r;
    out->g = in->g;
    out->b = in->b;
}

static ps1_screen_clip_vert lerp_screen_clip_vert(
        const ps1_screen_clip_vert* a, const ps1_screen_clip_vert* b,
        int axis, int boundary) {
    ps1_screen_clip_vert out;
    /* Canonical edge direction also keeps perspective attributes identical. */
    if ((axis == 0 ? a->x : a->y) > (axis == 0 ? b->x : b->y)) {
        const ps1_screen_clip_vert* swap = a; a = b; b = swap;
    }
    const int32_t av = axis == 0 ? a->x : a->y;
    const int32_t bv = axis == 0 ? b->x : b->y;
    const int32_t denominator = bv - av;
    const int32_t numerator = boundary - av;
    if (denominator == 0)
        return *a;
    out.x = a->x + (int32_t)ps1_clip_mul_div(b->x - a->x, numerator, denominator);
    out.y = a->y + (int32_t)ps1_clip_mul_div(b->y - a->y, numerator, denominator);
    const ps1_clip_weight weight = ps1_clip_make_weight(a->z,b->z,numerator,denominator);
    out.z = ps1_clip_weight_attribute(a->z, b->z, &weight);
    out.u = ps1_clip_weight_attribute(a->u, b->u, &weight);
    out.v = ps1_clip_weight_attribute(a->v, b->v, &weight);
    out.r = ps1_clip_weight_attribute(a->r, b->r, &weight);
    out.g = ps1_clip_weight_attribute(a->g, b->g, &weight);
    out.b = ps1_clip_weight_attribute(a->b, b->b, &weight);
    if (axis == 0)
        out.x = boundary;
    else
        out.y = boundary;
    return out;
}

static int clip_screen_edge(const ps1_screen_clip_vert* input, int input_count,
                            ps1_screen_clip_vert* output, int max_output,
                            int axis, int boundary, int keep_greater) {
    int output_count = 0;
    int i;
    const ps1_screen_clip_vert* previous;
    if (input_count <= 0)
        return 0;
    previous = &input[input_count - 1];
    for (i = 0; i < input_count; ++i) {
        const ps1_screen_clip_vert* current = &input[i];
        const int32_t current_axis = axis == 0 ? current->x : current->y;
        const int32_t previous_axis = axis == 0 ? previous->x : previous->y;
        const int current_inside =
            keep_greater ? current_axis >= boundary : current_axis <= boundary;
        const int previous_inside =
            keep_greater ? previous_axis >= boundary : previous_axis <= boundary;
        if (current_inside != previous_inside && output_count < max_output) {
            output[output_count++] = lerp_screen_clip_vert(
                previous, current, axis, boundary);
        }
        if (current_inside && output_count < max_output)
            output[output_count++] = *current;
        previous = current;
    }
    return output_count;
}

static int clip_polygon_to_safe_screen(const ps1_screen_clip_vert* input,
                                       int input_count,
                                       ps1_screen_clip_vert output[12]) {
    ps1_screen_clip_vert a[12];
    ps1_screen_clip_vert b[12];
    int count = input_count;
    int i;
    unsigned planes = 0;
    if (count > 12)
        count = 12;
    for (i = 0; i < count; ++i) {
        if (input[i].x < -2) planes |= 1;
        if (input[i].x > s_screen_w + 1) planes |= 2;
        if (input[i].y < -2) planes |= 4;
        if (input[i].y > s_screen_h + 1) planes |= 8;
    }
    /* Clip to the visible viewport, with a two-pixel seam margin. Keeping the
     * GPU's entire coordinate range left giant offscreen UV spans intact. */
    const ps1_screen_clip_vert* current = input;
    ps1_screen_clip_vert* scratch = a;
    for (unsigned plane = 0; plane < 4 && count; ++plane) {
        if (!(planes & (1u << plane))) continue;
        const int boundary = plane == 0 || plane == 2 ? -2
            : plane == 1 ? s_screen_w + 1 : s_screen_h + 1;
        count = clip_screen_edge(current, count, scratch, 12,
            plane >> 1, boundary, !(plane & 1));
        current = scratch;
        scratch = scratch == a ? b : a;
    }
    for (i = 0; i < count; ++i) output[i] = current[i];
    return count;
}

static int authored_floor_face(const ps1_entity* ent, const ps1_mesh_tri* face) {
    return ent->render_preset == PS1_RENDER_PRESET_FLOOR ||
        (ent->render_preset == PS1_RENDER_PRESET_CORRIDOR &&
         (face->ny >= 3584 || face->ny <= -3584));
}

static int camera_triangle_needs_safe_clip(const ps1_clip_vert vert[3], int min_z,
                                          int affine_correction) {
    int i;
    for (i = 0; i < 3; ++i) {
        const int z = vert[i].p.vz;
        if (z < min_z || z > s_far_z || (int64_t)z * 2 <= s_geom_screen)
            return 1;
        const uint32_t factor = ps1_projection_factor(s_geom_screen, z);
        const int64_t x = ps1_projection_axis(vert[i].p.vx, factor, s_screen_w / 2);
        const int64_t y = ps1_projection_axis(vert[i].p.vy, factor, s_screen_h / 2);
        if (x <= -1024 || x >= 1023 || y <= -1024 || y >= 1023) return 1;
        if (x < -2 || x > s_screen_w + 1 || y < -2 || y > s_screen_h + 1) return 1;
    }
    for (i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        int64_t dx = (int64_t)vert[i].p.vx * vert[j].p.vz -
                     (int64_t)vert[j].p.vx * vert[i].p.vz;
        int64_t dy = (int64_t)vert[i].p.vy * vert[j].p.vz -
                     (int64_t)vert[j].p.vy * vert[i].p.vz;
        const int64_t zz = (int64_t)vert[i].p.vz * vert[j].p.vz;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if ((int64_t)s_geom_screen * dx > 1020 * zz ||
            (int64_t)s_geom_screen * dy > 508 * zz)
            return 1;
        if (affine_correction && ((int64_t)s_geom_screen * dx > 64 * zz ||
             (int64_t)s_geom_screen * dy > 64 * zz) &&
            ((int64_t)vert[i].p.vz * 8 > (int64_t)vert[j].p.vz * 9 ||
             (int64_t)vert[j].p.vz * 8 > (int64_t)vert[i].p.vz * 9))
            return 1;
    }
    return 0;
}

static int cached_triangle_needs_safe_clip(
        int32_t sxy0, int32_t sxy1, int32_t sxy2,
        int32_t z0, int32_t z1, int32_t z2, int min_z, int affine_correction) {
    const int x0 = (int16_t)(sxy0 & 0xffff);
    const int y0 = (int16_t)(sxy0 >> 16);
    const int x1 = (int16_t)(sxy1 & 0xffff);
    const int y1 = (int16_t)(sxy1 >> 16);
    const int x2 = (int16_t)(sxy2 & 0xffff);
    const int y2 = (int16_t)(sxy2 >> 16);
    int min_x = x0;
    int max_x = x0;
    int min_y = y0;
    int max_y = y0;
    if (ps1_projection_vertex_needs_clip(sxy0, z0, min_z, s_far_z, s_geom_screen) ||
        ps1_projection_vertex_needs_clip(sxy1, z1, min_z, s_far_z, s_geom_screen) ||
        ps1_projection_vertex_needs_clip(sxy2, z2, min_z, s_far_z, s_geom_screen))
        return 1;
    if (x1 < min_x) min_x = x1;
    if (x1 > max_x) max_x = x1;
    if (x2 < min_x) min_x = x2;
    if (x2 > max_x) max_x = x2;
    if (y1 < min_y) min_y = y1;
    if (y1 > max_y) max_y = y1;
    if (y2 < min_y) min_y = y2;
    if (y2 > max_y) max_y = y2;
    if (max_x - min_x > 1020 || max_y - min_y > 508) return 1;
    /* Both triangles sharing an offscreen edge must clip it. Clipping only
     * the one exceeding GPU limits rounds a new boundary endpoint while its
     * neighbour retains the original long edge, opening a diagonal crack. */
    if (min_x < -2 || max_x > s_screen_w + 1 ||
        min_y < -2 || max_y > s_screen_h + 1) return 1;
    /* Authored floor grids already carry their subdivision. Keep all real
     * projection/near-plane clipping above, without routing them through the
     * wall's extra affine-correction path. */
    if (!affine_correction) return 0;
    int min_depth = z0 < z1 ? z0 : z1;
    int max_depth = z0 > z1 ? z0 : z1;
    if (z2 < min_depth) min_depth = z2;
    if (z2 > max_depth) max_depth = z2;
    return (max_x - min_x > 64 || max_y - min_y > 64) &&
           max_depth - min_depth > min_depth / 8;
}

static void project_clip_vert(const ps1_clip_vert* in, int16_t* x, int16_t* y, int min_z) {
    int z = in->p.vz;
    int sx, sy;
    if (z < min_z)
        z = min_z;
    const uint32_t factor = ps1_projection_factor(s_geom_screen, z);
    sx = ps1_projection_axis(in->p.vx, factor, s_screen_w / 2);
    sy = ps1_projection_axis(in->p.vy, factor, s_screen_h / 2);
    *x = (int16_t)clamp_i16(sx);
    *y = (int16_t)clamp_i16(sy);
}

static void project_vector_vert(const VECTOR* in, int16_t* x, int16_t* y, int min_z) {
    *x = ps1_viewmodel_project_axis(in->vx, in->vz, s_geom_screen, s_screen_w / 2, min_z);
    *y = ps1_viewmodel_project_axis(in->vy, in->vz, s_geom_screen, s_screen_h / 2, min_z);
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

static int camera_space_quad_front_facing(const VECTOR* v0,
                                          const VECTOR* v1,
                                          const VECTOR* v2) {
    const int64_t ab_x = (int64_t)v1->vx - v0->vx;
    const int64_t ab_y = (int64_t)v1->vy - v0->vy;
    const int64_t ab_z = (int64_t)v1->vz - v0->vz;
    const int64_t ac_x = (int64_t)v2->vx - v0->vx;
    const int64_t ac_y = (int64_t)v2->vy - v0->vy;
    const int64_t ac_z = (int64_t)v2->vz - v0->vz;
    const int64_t nx = ab_y * ac_z - ab_z * ac_y;
    const int64_t ny = ab_z * ac_x - ab_x * ac_z;
    const int64_t nz = ab_x * ac_y - ab_y * ac_x;

    /* Cube faces use outward object-space winding.  In camera space the
     * camera is the origin, so an outside face is visible when its plane
     * normal points back towards the origin.  This avoids the projection-sign
     * ambiguity that made tall/non-uniform cubes select their rear faces. */
    return nx * v0->vx + ny * v0->vy + nz * v0->vz < 0;
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
    VECTOR eye, at;
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
        s_distance_cull_enabled = cam->camera_distance_cull_enabled;
        s_distance_cull_z = world_to_psx(
            cam->camera_distance_cull_distance);
        s_distance_cull_meshes = cam->camera_distance_cull_meshes;
        s_distance_cull_skinned = cam->camera_distance_cull_skinned;
        s_frustum_cull_enabled = cam->camera_frustum_cull_enabled;
        s_frustum_cull_margin = world_to_psx(
            cam->camera_frustum_cull_margin);
        s_frustum_cull_meshes = cam->camera_frustum_cull_meshes;
        s_frustum_cull_skinned = cam->camera_frustum_cull_skinned;
    } else {
        s_geom_screen = s_screen_w / 2;
        s_near_z = 16;
        s_far_z = 6400;
        s_distance_cull_enabled = 0;
        s_distance_cull_z = 0;
        s_frustum_cull_enabled = 0;
        s_frustum_cull_margin = 0;
    }
    if (g_ps1_scene.fog_enabled) {
        if (s_fog_end_z > s_far_z) {
            s_fog_end_z = s_far_z;
            if (s_fog_end_z <= s_fog_start_z)
                s_fog_start_z = s_fog_end_z > 128 ? s_fog_end_z / 2 : 0;
        }
        update_fog_solid_distance();
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
    add_world_primitive(ot + (OT_LEN - 2), tile);
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
    if (!ent || ent->rigid_anim_fps == 0)
        return 0;
    if (ent->rigid_anim_blend_target_frame_plus_one > 0) {
        index = (uint32_t)ent->rigid_anim_blend_target_frame_plus_one - 1u;
        return index < g_ps1_scene.rigid_anim_frame_count
            ? &g_ps1_scene.rigid_anim_frames[index]
            : 0;
    }
    if (ent->rigid_anim_frame_count <= 1)
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

typedef struct {
    char** pri;
    const char* packet_end;
    uint32_t* ot;
    const ps1_entity* ent;
    const ps1_mesh* mesh;
    uint16_t vertex_offset;
    uint16_t triangle_offset;
    int32_t near_z;
    int shell_ot_bias;
    int fog_enabled;
    int use_tex;
    uint16_t tpage;
    int uoff;
    int voff;
} ps1_cached_flat_quad_context;

static int __attribute__((noinline)) cached_flat_pair_indices(const ps1_mesh* mesh,
                                                               const ps1_mesh_tri* a,
                                                               const ps1_mesh_tri* b,
                                                               uint16_t quad[4]) {
    const uint16_t ai[3] = { a->i0, a->i1, a->i2 };
    const uint16_t bi[3] = { b->i0, b->i1, b->i2 };
    int ae, be;
    if (a->nx != b->nx || a->ny != b->ny || a->nz != b->nz ||
        a->shade != b->shade || a->color_r != b->color_r ||
        a->color_g != b->color_g || a->color_b != b->color_b ||
        a->texture_index != b->texture_index)
        return 0;
    for (ae = 0; ae < 3; ++ae) {
        const uint16_t edge0 = ai[ae];
        const uint16_t edge1 = ai[(ae + 1) % 3];
        for (be = 0; be < 3; ++be) {
            if (bi[be] != edge1 || bi[(be + 1) % 3] != edge0)
                continue;
            quad[0] = ai[(ae + 2) % 3];
            quad[1] = edge0;
            quad[2] = edge1;
            quad[3] = bi[(be + 2) % 3];
            if (quad[0] == quad[3])
                return 0;
            if (quad[0] >= mesh->vert_count || quad[1] >= mesh->vert_count ||
                quad[2] >= mesh->vert_count || quad[3] >= mesh->vert_count)
                return 0;
            /* Switching from two triangles to a native PS1 quad is visually
             * equivalent only when the UVs form a parallelogram. */
            if (mesh->uvs) {
                const int du = (int)mesh->uvs[quad[0]].u + mesh->uvs[quad[3]].u -
                               mesh->uvs[quad[1]].u - mesh->uvs[quad[2]].u;
                const int dv = (int)mesh->uvs[quad[0]].v + mesh->uvs[quad[3]].v -
                               mesh->uvs[quad[1]].v - mesh->uvs[quad[2]].v;
                if (du < -1 || du > 1 || dv < -1 || dv > 1)
                    return 0;
            }
            return 1;
        }
    }
    return 0;
}

/* Triangle pairing depends only on immutable mesh topology, UVs and face
 * material data. Cache it once per unique mesh instead of re-running the
 * edge search for every instance on every frame. ProModeler copies share a
 * ps1_mesh descriptor, so repeated floors build and consume one table. */
static const ps1_static_topology_cache* static_topology_cache_for(
        const ps1_mesh* mesh) {
    ps1_static_topology_cache* free_entry = 0;
    uint16_t ti;
    int i;

    if (!mesh || mesh->tri_count == 0)
        return 0;
    for (i = 0; i < PS1_STATIC_TOPOLOGY_CACHE_MAX; ++i) {
        ps1_static_topology_cache* entry = &s_static_topology_caches[i];
        if (entry->mesh == mesh)
            return entry;
        if (!entry->mesh && !free_entry)
            free_entry = entry;
    }
    if (!free_entry ||
        s_static_topology_triangle_used + mesh->tri_count >
            PS1_STATIC_TRI_CACHE_TOTAL)
        return 0;

    free_entry->mesh = mesh;
    free_entry->triangle_offset = s_static_topology_triangle_used;
    free_entry->triangle_count = mesh->tri_count;
    s_static_topology_triangle_used += mesh->tri_count;

    for (ti = 0; ti < mesh->tri_count; ++ti) {
        const uint16_t cache_index = free_entry->triangle_offset + ti;
        uint16_t quad[4];
        s_static_quad_valid[cache_index] = 0;
        if (ti + 1u < mesh->tri_count &&
            mesh->tris[ti + 1u].i0 == mesh->tris[ti].i2 &&
            mesh->tris[ti + 1u].i1 == mesh->tris[ti].i1 &&
            cached_flat_pair_indices(mesh, &mesh->tris[ti],
                                     &mesh->tris[ti + 1u],
                                     quad)) {
            s_static_quad_valid[cache_index] = 1;
            ++ti;
            s_static_quad_valid[free_entry->triangle_offset + ti] = 0;
        }
    }
    return free_entry;
}

/* Returns -1 on packet overflow, 0 when the pair is culled, 1 when submitted
 * and 2 when the two original triangles must be handled separately. */
static int __attribute__((noinline)) draw_cached_flat_quad(
    const ps1_cached_flat_quad_context* ctx, const uint16_t q[4],
    uint16_t triangle_index) {
    int32_t sxy[4], z[4];
    int16_t x[4], y[4];
    int fog[4] = { 0, 0, 0, 0 };
    uint8_t r[4], g[4], b[4];
    int gouraud_fog = 0;
    int opaque_fog_solid = 0;
    int32_t avsz;
    int otz;
    int i;

    for (i = 0; i < 4; ++i) {
        uint16_t projection_index;
        if (q[i] >= ctx->mesh->vert_count)
            return 2;
        projection_index = ctx->mesh->position_refs
            ? ctx->mesh->position_refs[q[i]] : q[i];
        if (projection_index >= ctx->mesh->vert_count)
            projection_index = q[i];
        sxy[i] = s_static_screen_cache[ctx->vertex_offset + projection_index];
        z[i] = s_static_depth_cache[ctx->vertex_offset + projection_index];
        if (z[i] < ctx->near_z)
            return 2;
        if (z[i] > s_far_z)
            return 2;
        x[i] = (int16_t)(sxy[i] & 0xffff);
        y[i] = (int16_t)(sxy[i] >> 16);
    }
    if (!screen_front_facing_strict(x[0], y[0], x[1], y[1], x[2], y[2]) ||
        !screen_front_facing_strict(x[2], y[2], x[1], y[1], x[3], y[3]))
        return 2;
    if ((x[0] < -64 && x[1] < -64 && x[2] < -64 && x[3] < -64) ||
        (x[0] > s_screen_w + 64 && x[1] > s_screen_w + 64 &&
         x[2] > s_screen_w + 64 && x[3] > s_screen_w + 64) ||
        (y[0] < -64 && y[1] < -64 && y[2] < -64 && y[3] < -64) ||
        (y[0] > s_screen_h + 64 && y[1] > s_screen_h + 64 &&
         y[2] > s_screen_h + 64 && y[3] > s_screen_h + 64))
        return 0;

    if (discard_solid_fog_quad(z[0], z[1], z[2], z[3]))
        return 0;

    if (ctx->fog_enabled && !ps1_texture_has_cutout(ctx->mesh->tris[triangle_index].texture_index ? ctx->mesh->tris[triangle_index].texture_index : ctx->ent->texture_index) && z[0] >= s_fog_solid_z && z[1] >= s_fog_solid_z &&
        z[2] >= s_fog_solid_z && z[3] >= s_fog_solid_z) {
        opaque_fog_solid = 1;
    }

    if (!authored_floor_face(ctx->ent, &ctx->mesh->tris[triangle_index]))
        SEAM_FILL_EXPAND4(x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
    avsz = (z[0] + z[1] + z[2] + z[3]) / 16;
    otz = mesh_triangle_otz_fine(avsz);
    otz = floor_surface_otz(ctx->ent,
                            &ctx->mesh->tris[triangle_index], otz,
                            z[0], z[1], z[2], z[3]);
    if (ctx->shell_ot_bias > 0) {
        otz -= ctx->shell_ot_bias * 2;
        if (otz < PS1_WORLD_OT_MIN)
            otz = PS1_WORLD_OT_MIN;
    }

    for (i = 0; i < 4; ++i) {
        r[i] = s_static_face_light_cache[ctx->triangle_offset + triangle_index][0];
        g[i] = s_static_face_light_cache[ctx->triangle_offset + triangle_index][1];
        b[i] = s_static_face_light_cache[ctx->triangle_offset + triangle_index][2];
    }
    if (opaque_fog_solid) {
        for (i = 0; i < 4; ++i) {
            r[i] = g_ps1_scene.fog_r;
            g[i] = g_ps1_scene.fog_g;
            b[i] = g_ps1_scene.fog_b;
        }
    } else if (ctx->fog_enabled) {
        for (i = 0; i < 4; ++i) {
            uint16_t projection_index = ctx->mesh->position_refs
                ? ctx->mesh->position_refs[q[i]] : q[i];
            if (projection_index >= ctx->mesh->vert_count)
                projection_index = q[i];
            fog[i] = s_static_fog_cache[
                ctx->vertex_offset + projection_index];
        }
        if ((fog[0] | fog[1] | fog[2] | fog[3]) != 0) {
            for (i = 0; i < 4; ++i)
                apply_scene_fog_amount(fog[i], &r[i], &g[i], &b[i]);
            gouraud_fog =
                fog[0] != fog[1] || fog[0] != fog[2] || fog[0] != fog[3];
        }
    }

    if (ctx->use_tex && !opaque_fog_solid) {
        if (gouraud_fog) {
            POLY_GT4* poly;
            if ((const char*)(*ctx->pri + sizeof(POLY_GT4)) > ctx->packet_end)
                return -1;
            poly = (POLY_GT4*)*ctx->pri;
            setPolyGT4(poly);
            setXY4(poly, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
            poly->r0 = r[0]; poly->g0 = g[0]; poly->b0 = b[0];
            poly->r1 = r[1]; poly->g1 = g[1]; poly->b1 = b[1];
            poly->r2 = r[2]; poly->g2 = g[2]; poly->b2 = b[2];
            poly->r3 = r[3]; poly->g3 = g[3]; poly->b3 = b[3];
            poly->tpage = ctx->tpage;
            poly->clut = 0;
            setUV4(poly,
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[0]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[0]].v),
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[1]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[1]].v),
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[2]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[2]].v),
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[3]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[3]].v));
            add_world_primitive(ctx->ot + otz, poly);
            *ctx->pri = (char*)(poly + 1);
        } else {
            POLY_FT4* poly;
            if ((const char*)(*ctx->pri + sizeof(POLY_FT4)) > ctx->packet_end)
                return -1;
            poly = (POLY_FT4*)*ctx->pri;
            setPolyFT4(poly);
            setXY4(poly, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
            setRGB0(poly, r[0], g[0], b[0]);
            poly->tpage = ctx->tpage;
            poly->clut = 0;
            setUV4(poly,
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[0]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[0]].v),
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[1]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[1]].v),
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[2]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[2]].v),
                   clamp_u8(ctx->uoff + ctx->mesh->uvs[q[3]].u), clamp_u8(ctx->voff + ctx->mesh->uvs[q[3]].v));
            add_world_primitive(ctx->ot + otz, poly);
            *ctx->pri = (char*)(poly + 1);
        }
    } else if (gouraud_fog) {
        POLY_G4* poly;
        if ((const char*)(*ctx->pri + sizeof(POLY_G4)) > ctx->packet_end)
            return -1;
        poly = (POLY_G4*)*ctx->pri;
        setPolyG4(poly);
        setXY4(poly, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
        poly->r0 = r[0]; poly->g0 = g[0]; poly->b0 = b[0];
        poly->r1 = r[1]; poly->g1 = g[1]; poly->b1 = b[1];
        poly->r2 = r[2]; poly->g2 = g[2]; poly->b2 = b[2];
        poly->r3 = r[3]; poly->g3 = g[3]; poly->b3 = b[3];
        add_world_primitive(ctx->ot + otz, poly);
        *ctx->pri = (char*)(poly + 1);
    } else {
        POLY_F4* poly;
        if ((const char*)(*ctx->pri + sizeof(POLY_F4)) > ctx->packet_end)
            return -1;
        poly = (POLY_F4*)*ctx->pri;
        setPolyF4(poly);
        setXY4(poly, x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]);
        setRGB0(poly, r[0], g[0], b[0]);
        add_world_primitive(ctx->ot + otz, poly);
        *ctx->pri = (char*)(poly + 1);
    }
    return 1;
}

typedef struct {
    char** pri;
    const char* packet_end;
    uint32_t* ot;
    const ps1_mesh* mesh;
    const ps1_mesh_tri* face;
    int16_t x[3];
    int16_t y[3];
    uint8_t r[3];
    uint8_t g[3];
    uint8_t b[3];
    int otz;
    int use_tex;
    uint16_t tpage;
    int uoff;
    int voff;
} ps1_cached_fog_triangle_context;

/* The Gouraud fog case is uncommon but packet-heavy. Keep it out of the
 * ordinary FT3/F3 loop so that loop remains inside the PS1 instruction cache. */
static int __attribute__((noinline)) draw_cached_flat_fog_triangle(
    const ps1_cached_fog_triangle_context* ctx) {
    if (ctx->use_tex) {
        POLY_GT3* poly;
        if ((const char*)(*ctx->pri + sizeof(POLY_GT3)) > ctx->packet_end)
            return 0;
        poly = (POLY_GT3*)*ctx->pri;
        setPolyGT3(poly);
        setXY3(poly, ctx->x[0], ctx->y[0], ctx->x[1], ctx->y[1],
               ctx->x[2], ctx->y[2]);
        poly->r0 = ctx->r[0]; poly->g0 = ctx->g[0]; poly->b0 = ctx->b[0];
        poly->r1 = ctx->r[1]; poly->g1 = ctx->g[1]; poly->b1 = ctx->b[1];
        poly->r2 = ctx->r[2]; poly->g2 = ctx->g[2]; poly->b2 = ctx->b[2];
        poly->tpage = ctx->tpage;
        poly->clut = 0;
        setUV3(poly,
               clamp_u8(ctx->uoff + ctx->mesh->uvs[ctx->face->i0].u),
               clamp_u8(ctx->voff + ctx->mesh->uvs[ctx->face->i0].v),
               clamp_u8(ctx->uoff + ctx->mesh->uvs[ctx->face->i1].u),
               clamp_u8(ctx->voff + ctx->mesh->uvs[ctx->face->i1].v),
               clamp_u8(ctx->uoff + ctx->mesh->uvs[ctx->face->i2].u),
               clamp_u8(ctx->voff + ctx->mesh->uvs[ctx->face->i2].v));
        add_world_primitive(ctx->ot + ctx->otz, poly);
        *ctx->pri = (char*)(poly + 1);
    } else {
        POLY_G3* poly;
        if ((const char*)(*ctx->pri + sizeof(POLY_G3)) > ctx->packet_end)
            return 0;
        poly = (POLY_G3*)*ctx->pri;
        setPolyG3(poly);
        setXY3(poly, ctx->x[0], ctx->y[0], ctx->x[1], ctx->y[1],
               ctx->x[2], ctx->y[2]);
        poly->r0 = ctx->r[0]; poly->g0 = ctx->g[0]; poly->b0 = ctx->b[0];
        poly->r1 = ctx->r[1]; poly->g1 = ctx->g[1]; poly->b1 = ctx->b[1];
        poly->r2 = ctx->r[2]; poly->g2 = ctx->g[2]; poly->b2 = ctx->b[2];
        add_world_primitive(ctx->ot + ctx->otz, poly);
        *ctx->pri = (char*)(poly + 1);
    }
    return 1;
}

/* Compact hot path for ordinary cached meshes. Keeping this outside the
 * feature-heavy draw_entity loop matters on the PS1's small instruction
 * cache: world props and vertex-animated characters do not need to execute
 * the view-model, near-clip, terrain and smooth-shading decision tree for
 * every triangle. */
static int draw_cached_flat_mesh(char** pri, const char* packet_end, uint32_t* ot,
                                 const ps1_entity* ent, const ps1_mesh* mesh,
                                 uint16_t vertex_offset, uint16_t triangle_offset,
                                 int32_t near_z, int shell_ot_bias,
                                 int default_use_tex, uint16_t default_tpage,
                                 int default_uoff, int default_voff, int skip_unsafe,
                                 const uint16_t* visible_triangles, uint16_t visible_count) {
    unsigned int cached_texture_index = 0xffffffffu;
    int cached_use_tex = 0;
    uint16_t cached_tpage = 0;
    int cached_uoff = 0;
    int cached_voff = 0;
    const int fog_enabled = g_ps1_scene.fog_enabled;
    const ps1_static_topology_cache* topology =
        ent->vertex_anim_frame_count == 0
            ? static_topology_cache_for(mesh) : 0;
    ps1_cached_flat_quad_context quad_ctx;
    int added = 0;
    uint16_t ti, draw_index;

    quad_ctx.pri = pri;
    quad_ctx.packet_end = packet_end;
    quad_ctx.ot = ot;
    quad_ctx.ent = ent;
    quad_ctx.mesh = mesh;
    quad_ctx.vertex_offset = vertex_offset;
    quad_ctx.triangle_offset = triangle_offset;
    quad_ctx.near_z = near_z;
    quad_ctx.shell_ot_bias = shell_ot_bias;
    quad_ctx.fog_enabled = fog_enabled;

    for (draw_index = 0; draw_index < (visible_triangles ? visible_count : mesh->tri_count); ++draw_index) {
        ti = visible_triangles ? visible_triangles[draw_index] : draw_index;
        const ps1_mesh_tri* face = &mesh->tris[ti];
        int32_t sxy0, sxy1, sxy2;
        int32_t z0, z1, z2;
        int16_t x0, y0, x1, y1, x2, y2;
        int32_t avsz;
        int otz;
        int use_tex;
        uint16_t tpage;
        int uoff, voff;
        uint8_t r, g, b;
        uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
        int fog0 = 0, fog1 = 0, fog2 = 0;
        int gouraud_fog = 0;
        int opaque_fog_solid = 0;
        uint16_t p0, p1, p2;

        if (topology && ti + 1u < mesh->tri_count &&
            (!skip_unsafe || (visible_triangles &&
                !s_static_safe_clip_cache[triangle_offset + ti] &&
                !s_static_safe_clip_cache[triangle_offset + ti + 1u])) &&
            (!visible_triangles || (draw_index + 1u < visible_count && visible_triangles[draw_index + 1u] == ti + 1u)) &&
            s_static_quad_valid[topology->triangle_offset + ti]) {
            const uint16_t quad[4] = {
                face->i0, face->i1, face->i2, mesh->tris[ti + 1u].i2
            };
                int pair_result;
                if (face->texture_index == 0) {
                    use_tex = default_use_tex;
                    tpage = default_tpage;
                    uoff = default_uoff;
                    voff = default_voff;
                } else {
                    const unsigned int texture_index = (unsigned int)face->texture_index;
                    if (cached_texture_index != texture_index) {
                        cached_texture_index = texture_index;
                        cached_use_tex =
                            texture_index <= g_ps1_texture_count && mesh->uvs &&
                            ps1_texture_width(texture_index) > 0 &&
                            ps1_texture_height(texture_index) > 0;
                        cached_tpage = cached_use_tex ? ps1_texture_tpage(texture_index) : 0;
                        cached_uoff = cached_use_tex ? (int)ps1_texture_u_offset(texture_index) : 0;
                        cached_voff = cached_use_tex ? (int)ps1_texture_v_offset(texture_index) : 0;
                    }
                    use_tex = cached_use_tex;
                    tpage = cached_tpage;
                    uoff = cached_uoff;
                    voff = cached_voff;
                }
                quad_ctx.use_tex = use_tex;
                quad_ctx.tpage = tpage;
                quad_ctx.uoff = uoff;
                quad_ctx.voff = voff;
                pair_result = draw_cached_flat_quad(&quad_ctx, quad, ti);
                if (pair_result != 2) {
                    ++draw_index;
                    if (pair_result < 0)
                        return added;
                    if (pair_result > 0)
                        ++added;
                    continue;
                }
        }

        if (face->i0 >= mesh->vert_count || face->i1 >= mesh->vert_count ||
            face->i2 >= mesh->vert_count)
            continue;
        p0 = mesh->position_refs ? mesh->position_refs[face->i0] : face->i0;
        p1 = mesh->position_refs ? mesh->position_refs[face->i1] : face->i1;
        p2 = mesh->position_refs ? mesh->position_refs[face->i2] : face->i2;
        if (p0 >= mesh->vert_count) p0 = face->i0;
        if (p1 >= mesh->vert_count) p1 = face->i1;
        if (p2 >= mesh->vert_count) p2 = face->i2;
        sxy0 = s_static_screen_cache[vertex_offset + p0];
        sxy1 = s_static_screen_cache[vertex_offset + p1];
        sxy2 = s_static_screen_cache[vertex_offset + p2];
        z0 = s_static_depth_cache[vertex_offset + p0];
        z1 = s_static_depth_cache[vertex_offset + p1];
        z2 = s_static_depth_cache[vertex_offset + p2];
        if (skip_unsafe && (visible_triangles ? s_static_safe_clip_cache[triangle_offset + ti] :
            cached_triangle_needs_safe_clip(sxy0, sxy1, sxy2, z0, z1, z2, near_z, !authored_floor_face(ent, face))))
            continue;
        if (z0 < near_z || z1 < near_z || z2 < near_z)
            continue;
        if (discard_solid_fog_triangle(z0, z1, z2))
            continue;
        if (fog_enabled && !ps1_texture_has_cutout(face->texture_index ? face->texture_index : ent->texture_index) && z0 >= s_fog_solid_z && z1 >= s_fog_solid_z &&
            z2 >= s_fog_solid_z) {
            opaque_fog_solid = 1;
        }
        if (z0 > s_far_z || z1 > s_far_z || z2 > s_far_z)
            continue;

        x0 = (int16_t)(sxy0 & 0xffff); y0 = (int16_t)(sxy0 >> 16);
        x1 = (int16_t)(sxy1 & 0xffff); y1 = (int16_t)(sxy1 >> 16);
        x2 = (int16_t)(sxy2 & 0xffff); y2 = (int16_t)(sxy2 >> 16);
        if (!screen_front_facing_strict(x0, y0, x1, y1, x2, y2) ||
            triangle_outside_screen(x0, y0, x1, y1, x2, y2))
            continue;

        if (ent->vertex_anim_frame_count == 0 && !authored_floor_face(ent, face))
            SEAM_FILL_EXPAND(x0, y0, x1, y1, x2, y2);

        avsz = (z0 + z1 + z2) / 12;
        otz = mesh_triangle_otz_fine(avsz);
        otz = floor_surface_otz(ent, face, otz, z0, z1, z2, z2);
        if (shell_ot_bias > 0) {
            otz -= shell_ot_bias * 2;
            if (otz < PS1_WORLD_OT_MIN)
                otz = PS1_WORLD_OT_MIN;
        }

        /* Resolve materials only after rejection. Most meshes stay entirely
         * on the default descriptor; ProModeler face overrides reuse the last
         * descriptor instead of calling five accessors per polygon. */
        if (opaque_fog_solid) {
            use_tex = 0;
            tpage = 0;
            uoff = 0;
            voff = 0;
        } else if (face->texture_index == 0) {
            use_tex = default_use_tex;
            tpage = default_tpage;
            uoff = default_uoff;
            voff = default_voff;
        } else {
            const unsigned int texture_index = (unsigned int)face->texture_index;
            if (cached_texture_index != texture_index) {
                cached_texture_index = texture_index;
                cached_use_tex =
                    texture_index <= g_ps1_texture_count && mesh->uvs &&
                    ps1_texture_width(texture_index) > 0 &&
                    ps1_texture_height(texture_index) > 0;
                cached_tpage = cached_use_tex ? ps1_texture_tpage(texture_index) : 0;
                cached_uoff = cached_use_tex ? (int)ps1_texture_u_offset(texture_index) : 0;
                cached_voff = cached_use_tex ? (int)ps1_texture_v_offset(texture_index) : 0;
            }
            use_tex = cached_use_tex;
            tpage = cached_tpage;
            uoff = cached_uoff;
            voff = cached_voff;
        }

        r = s_static_face_light_cache[triangle_offset + ti][0];
        g = s_static_face_light_cache[triangle_offset + ti][1];
        b = s_static_face_light_cache[triangle_offset + ti][2];
        if (opaque_fog_solid) {
            r = g_ps1_scene.fog_r;
            g = g_ps1_scene.fog_g;
            b = g_ps1_scene.fog_b;
        } else if (fog_enabled) {
            fog0 = s_static_fog_cache[vertex_offset + p0];
            fog1 = s_static_fog_cache[vertex_offset + p1];
            fog2 = s_static_fog_cache[vertex_offset + p2];
            gouraud_fog = fog0 != fog1 || fog0 != fog2;
            if (gouraud_fog) {
                r0 = r1 = r2 = r;
                g0 = g1 = g2 = g;
                b0 = b1 = b2 = b;
                apply_scene_fog_amount(fog0, &r0, &g0, &b0);
                apply_scene_fog_amount(fog1, &r1, &g1, &b1);
                apply_scene_fog_amount(fog2, &r2, &g2, &b2);
            } else {
                apply_scene_fog_amount(fog0, &r, &g, &b);
            }
        }

        if (gouraud_fog) {
            ps1_cached_fog_triangle_context fog_ctx;
            fog_ctx.pri = pri;
            fog_ctx.packet_end = packet_end;
            fog_ctx.ot = ot;
            fog_ctx.mesh = mesh;
            fog_ctx.face = face;
            fog_ctx.x[0] = x0; fog_ctx.y[0] = y0;
            fog_ctx.x[1] = x1; fog_ctx.y[1] = y1;
            fog_ctx.x[2] = x2; fog_ctx.y[2] = y2;
            fog_ctx.r[0] = r0; fog_ctx.g[0] = g0; fog_ctx.b[0] = b0;
            fog_ctx.r[1] = r1; fog_ctx.g[1] = g1; fog_ctx.b[1] = b1;
            fog_ctx.r[2] = r2; fog_ctx.g[2] = g2; fog_ctx.b[2] = b2;
            fog_ctx.otz = otz;
            fog_ctx.use_tex = use_tex;
            fog_ctx.tpage = tpage;
            fog_ctx.uoff = uoff;
            fog_ctx.voff = voff;
            if (!draw_cached_flat_fog_triangle(&fog_ctx))
                return added;
        } else if (use_tex) {
            POLY_FT3* poly;
            if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                return added;
            poly = (POLY_FT3*)*pri;
            setPolyFT3(poly);
            setXY3(poly, x0, y0, x1, y1, x2, y2);
            setRGB0(poly, r, g, b);
            poly->tpage = tpage;
            poly->clut = 0;
            setUV3(poly,
                   clamp_u8(uoff + mesh->uvs[face->i0].u),
                   clamp_u8(voff + mesh->uvs[face->i0].v),
                   clamp_u8(uoff + mesh->uvs[face->i1].u),
                   clamp_u8(voff + mesh->uvs[face->i1].v),
                   clamp_u8(uoff + mesh->uvs[face->i2].u),
                   clamp_u8(voff + mesh->uvs[face->i2].v));
            add_world_primitive(ot + otz, poly);
            *pri = (char*)(poly + 1);
        } else {
            POLY_F3* poly;
            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                return added;
            poly = (POLY_F3*)*pri;
            setPolyF3(poly);
            setXY3(poly, x0, y0, x1, y1, x2, y2);
            setRGB0(poly, r, g, b);
            add_world_primitive(ot + otz, poly);
            *pri = (char*)(poly + 1);
        }
        ++added;
    }
    return added;
}

/* Keep the visible portion of a floor triangle when a corner crosses the
 * near plane or its projected span exceeds the GPU's polygon limits. The
 * ordinary plane path still uses RTPT; only boundary triangles pay for this. */
static int draw_clipped_plane_triangle(char** pri, const char* packet_end, uint32_t* ot,
        const ps1_entity* ent, const MATRIX* matrix, int near_z,
        const SVECTOR* a, const SVECTOR* b, const SVECTOR* c,
        const uint8_t ca[3], const uint8_t cb[3], const uint8_t cc[3],
        int ua, int va, int ub, int vb, int uc, int vc,
        int use_texture, uint16_t tpage) {
    ps1_clip_vert source[3], clipped[6];
    ps1_screen_clip_vert projected[6], screen[12];
    const SVECTOR* points[3]={a,b,c};
    const uint8_t* colors[3]={ca,cb,cc};
    const int u[3]={ua,ub,uc}, v[3]={va,vb,vc};
    int count, i, added=0;
    ps1_mesh_tri floor_face={0};
    floor_face.ny=ONE;
    for (i=0;i<3;++i) {
        make_clip_vert(matrix,points[i],0,&source[i]);
        source[i].u=u[i]; source[i].v=v[i];
        source[i].r=colors[i][0]; source[i].g=colors[i][1]; source[i].b=colors[i][2];
    }
    count=clip_world_depth(source,clipped,near_z,s_far_z);
    if (count<3) return 0;
    for (i=0;i<count;++i) project_clip_vert_wide(&clipped[i],&projected[i],near_z);
    count=clip_polygon_to_safe_screen(projected,count,screen);
    for (i=1;i<count-1;++i) {
        const ps1_screen_clip_vert* p[3]={&screen[0],&screen[i],&screen[i+1]};
        const int32_t depth=(p[0]->z+p[1]->z+p[2]->z)/3;
        int bucket, j;
        uint8_t color[3][3];
        if (depth>s_far_z || discard_solid_fog_triangle(p[0]->z,p[1]->z,p[2]->z)) continue;
        if ((int64_t)(p[1]->x-p[0]->x)*(p[2]->y-p[0]->y)==
            (int64_t)(p[1]->y-p[0]->y)*(p[2]->x-p[0]->x)) continue;
        bucket=mesh_triangle_otz_fine(depth>>2);
        bucket=floor_surface_otz(ent,&floor_face,bucket,p[0]->z,p[1]->z,p[2]->z,p[2]->z);
        for (j=0;j<3;++j) {
            color[j][0]=clamp_u8(p[j]->r); color[j][1]=clamp_u8(p[j]->g); color[j][2]=clamp_u8(p[j]->b);
            apply_scene_fog(p[j]->z,&color[j][0],&color[j][1],&color[j][2]);
        }
        if (is_triangle_fully_fogged(p[0]->z) && is_triangle_fully_fogged(p[1]->z) &&
            is_triangle_fully_fogged(p[2]->z) && !ps1_texture_has_cutout(ent->texture_index)) {
            POLY_F3* tri;
            if ((const char*)(*pri+sizeof(POLY_F3))>packet_end) return added;
            tri=(POLY_F3*)*pri; setPolyF3(tri);
            setXY3(tri,p[0]->x,p[0]->y,p[1]->x,p[1]->y,p[2]->x,p[2]->y);
            setRGB0(tri,g_ps1_scene.fog_r,g_ps1_scene.fog_g,g_ps1_scene.fog_b);
            add_world_primitive(ot+bucket,tri); *pri=(char*)(tri+1);
        } else if (use_texture) {
            POLY_GT3* tri;
            if ((const char*)(*pri+sizeof(POLY_GT3))>packet_end) return added;
            tri=(POLY_GT3*)*pri; setPolyGT3(tri);
            setXY3(tri,p[0]->x,p[0]->y,p[1]->x,p[1]->y,p[2]->x,p[2]->y);
            setUV3(tri,p[0]->u,p[0]->v,p[1]->u,p[1]->v,p[2]->u,p[2]->v);
            setRGB0(tri,color[0][0],color[0][1],color[0][2]);
            setRGB1(tri,color[1][0],color[1][1],color[1][2]);
            setRGB2(tri,color[2][0],color[2][1],color[2][2]);
            tri->tpage=tpage; tri->clut=0;
            add_world_primitive(ot+bucket,tri); *pri=(char*)(tri+1);
        } else {
            POLY_G3* tri;
            if ((const char*)(*pri+sizeof(POLY_G3))>packet_end) return added;
            tri=(POLY_G3*)*pri; setPolyG3(tri);
            setXY3(tri,p[0]->x,p[0]->y,p[1]->x,p[1]->y,p[2]->x,p[2]->y);
            setRGB0(tri,color[0][0],color[0][1],color[0][2]);
            setRGB1(tri,color[1][0],color[1][1],color[1][2]);
            setRGB2(tri,color[2][0],color[2][1],color[2][2]);
            add_world_primitive(ot+bucket,tri); *pri=(char*)(tri+1);
        }
        ++added;
    }
    return added;
}

/* Preserve the authored camera-space vertices and unrestricted H/Z ratio
 * for every Viewmodel path. RTPS/RTPT cannot project this close reliably. */
static int cache_viewmodel_projection(const MATRIX* composed, const ps1_mesh* m,
                                     int32_t entity_near_z) {
    uint16_t vi;
    int needs_clip = 0;
    gte_SetRotMatrix(composed);
    gte_SetTransMatrix(composed);
    for (vi = 0; vi < m->vert_count; ++vi) {
        uint16_t reference = m->position_refs ? m->position_refs[vi] : vi;
        if (reference < vi) {
            s_viewmodel_vertex_cache[vi] = s_viewmodel_vertex_cache[reference];
            s_viewmodel_screen_cache[vi] = s_viewmodel_screen_cache[reference];
            s_viewmodel_depth_cache[vi] = s_viewmodel_depth_cache[reference];
            continue;
        }
        int16_t sx, sy;
        VECTOR* vertex = &s_viewmodel_vertex_cache[vi];
        gte_ldv0(&m->verts[vi]);
        gte_rtv0tr();
        gte_stlvnl(vertex);
        project_vector_vert(vertex, &sx, &sy, entity_near_z);
        s_viewmodel_screen_cache[vi] = (int32_t)(
            (uint32_t)(uint16_t)sx | ((uint32_t)(uint16_t)sy << 16));
        s_viewmodel_depth_cache[vi] = vertex->vz;
        if (vertex->vz < entity_near_z)
            needs_clip = 1;
    }
    return needs_clip;
}

/* Keep the Unlit packet loop separate from the large world renderer so it
 * fits in the PS1 instruction cache. Projection is shared with Lit meshes. */
static int __attribute__((noinline)) draw_unlit_viewmodel_fast(
        char** pri, const char* packet_end, uint32_t* ot,
        const ps1_entity* ent, const ps1_mesh* m, const MATRIX* composed,
        int32_t entity_near_z,
        int use_tex, uint16_t tpage, int tex_uoff, int tex_voff) {
    uint16_t ti;
    int added = 0;

    if (!m || m->vert_count > PS1_VIEWMODEL_VERTEX_CACHE_MAX ||
        m->tri_count > PS1_VIEWMODEL_TRI_CACHE_MAX)
        return -1;
    /* Per-face texture pages need the general descriptor cache. */
    for (ti = 0; ti < m->tri_count; ++ti) {
        if (m->tris[ti].texture_index != 0)
            return -1;
    }

    if (cache_viewmodel_projection(composed, m, entity_near_z))
        return -1;

    if (use_tex && tpage &&
        (const char*)(*pri + sizeof(DR_TPAGE)) <= packet_end) {
        DR_TPAGE* dr = (DR_TPAGE*)*pri;
        setDrawTPage(dr, 0, 0, tpage);
        add_world_primitive(ot + (OT_LEN - 2), dr);
        *pri = (char*)(dr + 1);
    }

    for (ti = 0; ti < m->tri_count; ++ti) {
        const ps1_mesh_tri* t = &m->tris[ti];
        if (t->i0 >= m->vert_count || t->i1 >= m->vert_count ||
            t->i2 >= m->vert_count)
            continue;
        const int32_t sxy0 = s_viewmodel_screen_cache[t->i0];
        const int32_t sxy1 = s_viewmodel_screen_cache[t->i1];
        const int32_t sxy2 = s_viewmodel_screen_cache[t->i2];
        const int16_t x0 = (int16_t)(sxy0 & 0xffff);
        const int16_t y0 = (int16_t)(sxy0 >> 16);
        const int16_t x1 = (int16_t)(sxy1 & 0xffff);
        const int16_t y1 = (int16_t)(sxy1 >> 16);
        const int16_t x2 = (int16_t)(sxy2 & 0xffff);
        const int16_t y2 = (int16_t)(sxy2 >> 16);
        const int32_t z0 = s_viewmodel_depth_cache[t->i0];
        const int32_t z1 = s_viewmodel_depth_cache[t->i1];
        const int32_t z2 = s_viewmodel_depth_cache[t->i2];
        const int32_t depth = (z0 + z1 + z2) / 3;
        const int otz = viewmodel_triangle_otz(depth);

        if (depth <= 0 ||
            triangle_outside_screen(x0, y0, x1, y1, x2, y2) ||
            !screen_front_facing(x0, y0, x1, y1, x2, y2))
            continue;

        if (use_tex && tpage) {
            POLY_FT3* tri;
            if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                return added;
            tri = (POLY_FT3*)*pri;
            setPolyFT3(tri);
            setXY3(tri, x0, y0, x1, y1, x2, y2);
            setRGB0(tri, ent->color_r, ent->color_g, ent->color_b);
            tri->tpage = tpage;
            tri->clut = 0;
            setUV3(tri,
                   clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                   clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                   clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
            add_world_primitive(ot + otz, tri);
            *pri = (char*)(tri + 1);
        } else {
            POLY_F3* tri;
            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                return added;
            tri = (POLY_F3*)*pri;
            setPolyF3(tri);
            setXY3(tri, x0, y0, x1, y1, x2, y2);
            setRGB0(tri, ent->color_r, ent->color_g, ent->color_b);
            add_world_primitive(ot + otz, tri);
            *pri = (char*)(tri + 1);
        }
        ++added;
    }
    return added;
}

/* Find the camera's object-space Y once for an authored flat floor. Guard the
 * wide products; singular/extreme transforms use the general winding test. */
static int floor_camera_y(const MATRIX* matrix, int32_t* y, int* mirrored) {
    for (int i = 0; i < 3; ++i)
        if (matrix->t[i] < -65535 || matrix->t[i] > 65535) return 0;
    const int64_t a = (int32_t)matrix->m[1][1]*matrix->m[2][2] - (int64_t)matrix->m[1][2]*matrix->m[2][1];
    const int64_t b = (int32_t)matrix->m[1][2]*matrix->m[2][0] - (int64_t)matrix->m[1][0]*matrix->m[2][2];
    const int64_t c = (int32_t)matrix->m[1][0]*matrix->m[2][1] - (int64_t)matrix->m[1][1]*matrix->m[2][0];
    const int64_t det = matrix->m[0][0]*a + matrix->m[0][1]*b + matrix->m[0][2]*c;
    if (!det) return 0;
    const int64_t d = (int32_t)matrix->m[0][0]*matrix->m[2][2] - (int64_t)matrix->m[0][2]*matrix->m[2][0];
    const int64_t e = (int32_t)matrix->m[0][2]*matrix->m[1][0] - (int64_t)matrix->m[0][0]*matrix->m[1][2];
    const int64_t eye = -4096 * (b*matrix->t[0] + d*matrix->t[1] + e*matrix->t[2]) / det;
    if (eye < -65535 || eye > 65535) return 0;
    *y = (int32_t)eye; *mirrored = det < 0;
    return 1;
}

static int visibility_batch_outside(const ps1_visibility_batch* batch,
                                    const MATRIX* matrix, int near_z) {
    VECTOR center;
    int64_t radius[3];
    if (!batch->vertex_count) return 1;
    gte_ldv0(&batch->center); gte_rtv0tr(); gte_stlvnl(&center);
    /* Bound the products before using native arithmetic. Most small world
     * batches fit; unusual scales retain the original wide calculations. */
    if ((unsigned)batch->extent[0] + batch->extent[1] + batch->extent[2] <= 65535u) {
        int32_t r[3];
        int fits = s_geom_screen >= 0 && s_geom_screen <= 1023 &&
            s_screen_w > 0 && s_screen_w <= 1024 && s_screen_h > 0 && s_screen_h <= 1024;
        const int32_t c[3] = {center.vx, center.vy, center.vz};
        for (int row = 0; row < 3; ++row) {
            uint32_t sum = 0;
            for (int axis = 0; axis < 3; ++axis) {
                int coefficient = matrix->m[row][axis];
                if (coefficient < 0) coefficient = -coefficient;
                sum += (uint32_t)coefficient * batch->extent[axis];
            }
            r[row] = (int32_t)((sum + 4095u) >> 12) + 2;
            if (c[row] < -1048575 + r[row] || c[row] > 1048575 - r[row]) fits = 0;
        }
        if (fits) {
            const int32_t farthest = center.vz + r[2];
            if (farthest < near_z || center.vz - r[2] > s_far_z) return 1;
            return (center.vx + r[0])*s_geom_screen < -(s_screen_w/2+2)*farthest ||
                (center.vx - r[0])*s_geom_screen > (s_screen_w-s_screen_w/2+2)*farthest ||
                (center.vy + r[1])*s_geom_screen < -(s_screen_h/2+2)*farthest ||
                (center.vy - r[1])*s_geom_screen > (s_screen_h-s_screen_h/2+2)*farthest;
        }
    }
    for (int row = 0; row < 3; ++row) {
        int64_t r = 0;
        for (int axis = 0; axis < 3; ++axis) {
            int coefficient = matrix->m[row][axis];
            if (coefficient < 0) coefficient = -coefficient;
            r += (int64_t)coefficient * batch->extent[axis];
        }
        radius[row] = (r + 4095) / 4096 + 2;
    }
    const int64_t farthest = (int64_t)center.vz + radius[2];
    if (farthest < near_z || (int64_t)center.vz - radius[2] > s_far_z) return 1;
    return ((int64_t)center.vx + radius[0])*s_geom_screen < -(s_screen_w/2+2)*farthest ||
        ((int64_t)center.vx - radius[0])*s_geom_screen > (s_screen_w-s_screen_w/2+2)*farthest ||
        ((int64_t)center.vy + radius[1])*s_geom_screen < -(s_screen_h/2+2)*farthest ||
        ((int64_t)center.vy - radius[1])*s_geom_screen > (s_screen_h-s_screen_h/2+2)*farthest;
}

static void __attribute__((noinline)) cache_world_visibility(
        const ps1_entity* ent, const ps1_mesh* m,
        ps1_static_entity_cache* static_cache, int entity_near_z, const MATRIX* composed) {
    const uint16_t static_vertex_offset = static_cache->vertex_offset;
    const uint16_t static_triangle_offset = static_cache->triangle_offset;
    uint16_t vi;
    static_cache->needs_safe_clip = 0;
    const ps1_visibility_topology* visibility = visibility_topology_for(m);
    const unsigned batch_count = visibility ? visibility->batch_count : 1;
    int32_t eye_y = 0;
    int mirrored = 0;
    const int floor_eye_valid = (ent->render_preset == PS1_RENDER_PRESET_FLOOR ||
        ent->render_preset == PS1_RENDER_PRESET_CORRIDOR) && floor_camera_y(composed,&eye_y,&mirrored);
    if (visibility) {
        memset(&s_static_active_vertex[static_vertex_offset],0,m->vert_count);
        for (unsigned bi = 0; bi < batch_count; ++bi) {
            const unsigned index = visibility->batch_offset+bi;
            const ps1_visibility_batch* batch = &s_visibility_batches[index];
            const int delta_y = eye_y-batch->center.vy;
            const int backface = floor_eye_valid && batch->plane_winding &&
                (delta_y < -2 || delta_y > 2) &&
                (((batch->plane_winding < 0) != (delta_y < 0)) != mirrored);
            const int outside = backface || visibility_batch_outside(batch,composed,entity_near_z);
            s_visibility_batch_outside[index] = outside;
            if (!outside) for (unsigned v = 0; v < batch->vertex_count; ++v)
                s_static_active_vertex[static_vertex_offset+s_visibility_vertices[batch->vertex_offset+v]] = 1;
        }
    }
                        for (vi = 0; vi < m->vert_count; ++vi) {
                            uint16_t reference = m->position_refs ? m->position_refs[vi] : vi;
                            if (reference != vi && reference < m->vert_count)
                                continue;
                            if (visibility && !s_static_active_vertex[static_vertex_offset+vi]) continue;
                            gte_ldv0(&m->verts[vi]);
                            gte_rtps();
                            gte_stsxy(&s_static_screen_cache[static_vertex_offset+vi]);
                            gte_stsz(&s_static_depth_cache[static_vertex_offset+vi]);
                            gte_stlvnl(&s_static_camera_cache[static_vertex_offset + vi]);
                            s_static_outcode_cache[static_vertex_offset + vi] = camera_outcode(
                                &s_static_camera_cache[static_vertex_offset + vi], entity_near_z);
                            s_static_fog_cache[static_vertex_offset+vi] = scene_fog_amount_q8(
                                s_static_depth_cache[static_vertex_offset+vi]);
                            const int32_t xy=s_static_screen_cache[static_vertex_offset+vi];
                            const int32_t z=s_static_depth_cache[static_vertex_offset+vi];
                            const int x=(int16_t)xy, y=(int16_t)(xy>>16);
                            s_static_projection_flags[static_vertex_offset+vi] =
                                (ps1_projection_vertex_needs_clip(xy,z,entity_near_z,INT32_MAX,s_geom_screen) ? 1 : 0) |
                                (s_static_camera_cache[static_vertex_offset+vi].vz>s_far_z ? 2 : 0) |
                                (x < -2 || x>s_screen_w+1 || y < -2 || y>s_screen_h+1 ? 4 : 0);
                        }
                        static_cache->visible_triangle_count = 0;
                        for(unsigned bi=0;bi<batch_count;++bi) {
                            const ps1_visibility_batch* batch = visibility
                                ? &s_visibility_batches[visibility->batch_offset+bi] : 0;
                            if(batch) {
                                if (s_visibility_batch_outside[visibility->batch_offset+bi]) continue;
                                uint8_t common=0xff;
                                for(uint16_t vi=0;vi<batch->vertex_count && common;++vi)
                                    common &= s_static_outcode_cache[static_vertex_offset+
                                        s_visibility_vertices[batch->vertex_offset+vi]];
                                if(common)continue;
                            }
                            const unsigned begin=batch ? batch->triangle_begin : 0;
                            const unsigned end=batch ? batch->triangle_end : m->tri_count;
                        for (uint16_t ti = begin; ti < end; ++ti) {
                            const ps1_mesh_tri* face = &m->tris[ti];
                            uint16_t p0, p1, p2;
                            if (face->i0 >= m->vert_count || face->i1 >= m->vert_count || face->i2 >= m->vert_count)
                                continue;
                            p0 = m->position_refs ? m->position_refs[face->i0] : face->i0;
                            p1 = m->position_refs ? m->position_refs[face->i1] : face->i1;
                            p2 = m->position_refs ? m->position_refs[face->i2] : face->i2;
                            if (p0 >= m->vert_count) p0 = face->i0;
                            if (p1 >= m->vert_count) p1 = face->i1;
                            if (p2 >= m->vert_count) p2 = face->i2;
                            if (s_static_outcode_cache[static_vertex_offset + p0] &
                                s_static_outcode_cache[static_vertex_offset + p1] &
                                s_static_outcode_cache[static_vertex_offset + p2])
                                continue;
                            /* Clipped faces need camera-space winding. A
                             * saturated GTE XY can reverse the projected area;
                             * retaining every such backface until after all
                             * clipping doubled the work on two-sided floors. */
                            const int32_t xy0 = s_static_screen_cache[static_vertex_offset + p0];
                            const int32_t xy1 = s_static_screen_cache[static_vertex_offset + p1];
                            const int32_t xy2 = s_static_screen_cache[static_vertex_offset + p2];
                            const unsigned projection_flags=s_static_projection_flags[static_vertex_offset+p0] |
                                s_static_projection_flags[static_vertex_offset+p1] | s_static_projection_flags[static_vertex_offset+p2];
                            const int valid_projection = !(projection_flags & 1);
                            if (valid_projection) {
                                if (!screen_front_facing_strict((int16_t)xy0, (int16_t)(xy0 >> 16),
                                        (int16_t)xy1, (int16_t)(xy1 >> 16), (int16_t)xy2, (int16_t)(xy2 >> 16)))
                                    continue;
                            } else if (!camera_space_quad_front_facing(
                                    &s_static_camera_cache[static_vertex_offset + p0],
                                    &s_static_camera_cache[static_vertex_offset + p1],
                                    &s_static_camera_cache[static_vertex_offset + p2]))
                                continue;
                            int needs_clip = projection_flags != 0;
                            if (!needs_clip && (!authored_floor_face(ent,face) || s_screen_w>1020 || s_screen_h>508)) {
                                int min_x=(int16_t)xy0,max_x=min_x,min_y=(int16_t)(xy0>>16),max_y=min_y;
                                const int x1=(int16_t)xy1,x2=(int16_t)xy2,y1=(int16_t)(xy1>>16),y2=(int16_t)(xy2>>16);
                                if(x1<min_x)min_x=x1;if(x2<min_x)min_x=x2;
                                if(x1>max_x)max_x=x1;if(x2>max_x)max_x=x2;
                                if(y1<min_y)min_y=y1;if(y2<min_y)min_y=y2;
                                if(y1>max_y)max_y=y1;if(y2>max_y)max_y=y2;
                                const int z0=s_static_depth_cache[static_vertex_offset+p0],z1=s_static_depth_cache[static_vertex_offset+p1],z2=s_static_depth_cache[static_vertex_offset+p2];
                                int min_z=z0,max_z=z0;
                                if(z1<min_z)min_z=z1;if(z2<min_z)min_z=z2;
                                if(z1>max_z)max_z=z1;if(z2>max_z)max_z=z2;
                                needs_clip=max_x-min_x>1020 || max_y-min_y>508 ||
                                    (!authored_floor_face(ent,face) && (max_x-min_x>64 || max_y-min_y>64) && max_z-min_z>min_z/8);
                            }
                            s_static_safe_clip_cache[static_triangle_offset+ti] = needs_clip;
                            if (needs_clip) static_cache->needs_safe_clip = 1;
                            s_static_visible_triangles[static_triangle_offset + static_cache->visible_triangle_count++] = ti;
                        }
                        }
                        static_cache->visible_list_valid = 1;

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
    int     shell_ot_bias = 0;
    int     added = 0;
    int     shared_character_transform_hit = 0;
    const int is_viewmodel = entity_is_viewmodel(ent);
    const ps1_entity* viewmodel_source = is_viewmodel && !entity_rigid_frame(ent)
        ? source_entity_by_id(ent->id) : 0;
    const int entity_near_z = is_viewmodel
        ? viewmodel_near_z()
        : (ent->render_preset == PS1_RENDER_PRESET_CORRIDOR
            ? corridor_near_z() : s_near_z);

    rigid_frame = entity_rigid_frame(ent);
    /* This applies to both legacy rigid-bone animation and baked vertex
     * animation. Baked meshes are separate material parts too, so submitting
     * Hat after Head in one coarse OT bucket would draw Hat first and then let
     * Head overwrite it. Keep costume shells in a nearer bucket. */
    shell_ot_bias = character_shell_ot_bias(ent);
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
        if (ent->rigid_root_entity_index >= 0 &&
            s_shared_character_transform.valid &&
            s_shared_character_transform.root_index ==
                ent->rigid_root_entity_index &&
            s_shared_character_transform.position[0] == ent->position[0] &&
            s_shared_character_transform.position[1] == ent->position[1] &&
            s_shared_character_transform.position[2] == ent->position[2] &&
            s_shared_character_transform.rotation[0] == ent->rotation[0] &&
            s_shared_character_transform.rotation[1] == ent->rotation[1] &&
            s_shared_character_transform.rotation[2] == ent->rotation[2]) {
            pos = s_shared_character_transform.psx_position;
            rmtx = s_shared_character_transform.rotation_matrix;
            shared_character_transform_hit = 1;
        } else {
            unity_euler_to_psx_object(
                ent->rotation[0], ent->rotation[1], ent->rotation[2], &rotvec);
            unity_pos_to_psx(ent->position, &pos);
            RotMatrix(&rotvec, &rmtx);
            if (ent->rigid_root_entity_index >= 0) {
                s_shared_character_transform.valid = 1;
                s_shared_character_transform.light_matrix_valid = 0;
                s_shared_character_transform.root_index =
                    ent->rigid_root_entity_index;
                s_shared_character_transform.position[0] = ent->position[0];
                s_shared_character_transform.position[1] = ent->position[1];
                s_shared_character_transform.position[2] = ent->position[2];
                s_shared_character_transform.rotation[0] = ent->rotation[0];
                s_shared_character_transform.rotation[1] = ent->rotation[1];
                s_shared_character_transform.rotation[2] = ent->rotation[2];
                s_shared_character_transform.psx_position = pos;
                s_shared_character_transform.rotation_matrix = rmtx;
            }
        }
    }

    /* A ViewModel is authored below the Camera and must remain in the same
     * camera-local pose while the player looks around.  Runtime hierarchy
     * entities currently carry flattened world Euler angles; adding parent
     * Euler deltas is not a valid rotation composition when the model already
     * has an FBX basis rotation (the arms use 90/180 degrees).  Rebuild its
     * object basis from the immutable authored pose and later compose it with
     * the authored camera view.  Vertex animation remains fully dynamic. */
    if (viewmodel_source && s_render_camera_source) {
        unity_euler_to_psx_object(
            viewmodel_source->rotation[0], viewmodel_source->rotation[1],
            viewmodel_source->rotation[2], &rotvec);
        unity_pos_to_psx(viewmodel_source->position, &pos);
        RotMatrix(&rotvec, &rmtx);
        shared_character_transform_hit = 0;
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
    if (!ent->render_unlit) {
        int axis;
        fix16_t minimum = 0x7fffffff;
        for (axis = 0; axis < 3; ++axis) {
            fix16_t magnitude = ent->scale[axis] < 0 ? -ent->scale[axis] : ent->scale[axis];
            if (magnitude > 0 && magnitude < minimum)
                minimum = magnitude;
        }
        for (axis = 0; axis < 3; ++axis)
            s_normal_scale_q12[axis] = ent->scale[axis] != 0
                ? (int32_t)(((int64_t)minimum * ONE) / ent->scale[axis]) : 0;
    }
    s_shading_unlit = ent->render_unlit != 0;
    if (s_shading_unlit) {
        memset(&lmtx, 0, sizeof(lmtx));
    } else if (shared_character_transform_hit && s_shared_character_transform.light_matrix_valid) {
        lmtx = s_shared_character_transform.light_matrix;
    } else {
        MulMatrix0(&s_light_mtx, &rmtx, &lmtx);
        if (!rigid_frame && ent->rigid_root_entity_index >= 0) {
            s_shared_character_transform.light_matrix = lmtx;
            s_shared_character_transform.light_matrix_valid = 1;
        }
    }
    if (viewmodel_source && s_render_camera_source) {
        MATRIX authored_camera;
        VECTOR authored_eye;
        unity_pos_to_psx(s_render_camera_source->position, &authored_eye);
        build_unity_camera_matrix(
            &authored_eye, s_render_camera_source->rotation[0],
            s_render_camera_source->rotation[1],
            s_render_camera_source->rotation[2], &authored_camera);
        CompMatrixLV(&authored_camera, &omtx, &composed);
    } else {
        CompMatrixLV(&s_cam_mtx, &omtx, &composed);
    }

    if (!is_viewmodel) {
        const int is_skinned_mesh =
            rigid_frame != 0 || ent->vertex_anim_frame_count > 0 ||
            ent->render_preset == PS1_RENDER_PRESET_CHARACTER;
        const int distance_cull_applies =
            s_distance_cull_enabled && s_distance_cull_z > 0 &&
            (is_skinned_mesh ? s_distance_cull_skinned
                             : s_distance_cull_meshes);
        const int frustum_cull_applies =
            !s_frustum_cull_enabled ||
            (is_skinned_mesh ? s_frustum_cull_skinned
                             : s_frustum_cull_meshes);
        fix16_t mesh_size_fix = ent->mesh_size > 0 ? ent->mesh_size : FIX16_ONE;
        fix16_t s0 = ent->scale[0] < 0 ? -ent->scale[0] : ent->scale[0];
        fix16_t s1 = ent->scale[1] < 0 ? -ent->scale[1] : ent->scale[1];
        fix16_t s2 = ent->scale[2] < 0 ? -ent->scale[2] : ent->scale[2];
        fix16_t max_s = s0;
        int cull_x = composed.t[0];
        int cull_y = composed.t[1];
        int cull_z = composed.t[2];
        const ps1_mesh* bounds_mesh = 0;
        if (s1 > max_s) max_s = s1;
        if (s2 > max_s) max_s = s2;
        if (max_s <= 0) max_s = FIX16_ONE;

        /* Combined scale = entity scale * mesh_size */
        max_s = (fix16_t)(((int64_t)max_s * mesh_size_fix) >> 16);

        /* Convert Fix16 scale (65536 = 1.0m = 64 PSX units) with a generous
         * 2.0x extent margin for built-in primitives. Custom vertices use a
         * +/-1024 template and scale_q12, so derive their real PSX radius
         * instead of collapsing every large mesh to the 128-unit minimum. */
        int bounding_radius = (int)(((int64_t)max_s * 128) >> 16);
        if (ent->mesh == PS1_MESH_CUSTOM && ent->mesh_index > 0 && ent->mesh_index <= g_ps1_mesh_count) {
            VECTOR local_center;
            VECTOR camera_center;
            int scale_x = scale.vx < 0 ? -(int)scale.vx : (int)scale.vx;
            int scale_y = scale.vy < 0 ? -(int)scale.vy : (int)scale.vy;
            int scale_z = scale.vz < 0 ? -(int)scale.vz : (int)scale.vz;
            int max_scale_q12 = scale_x;
            bounds_mesh = &g_ps1_meshes[ent->mesh_index - 1u];
            if (scale_y > max_scale_q12) max_scale_q12 = scale_y;
            if (scale_z > max_scale_q12) max_scale_q12 = scale_z;
            if (max_scale_q12 <= 0) max_scale_q12 = 1;

            local_center.vx = bounds_mesh->bounds_center_x;
            local_center.vy = bounds_mesh->bounds_center_y;
            local_center.vz = bounds_mesh->bounds_center_z;
            ApplyMatrixLV(&composed, &local_center, &camera_center);
            cull_x = camera_center.vx + composed.t[0];
            cull_y = camera_center.vy + composed.t[1];
            cull_z = camera_center.vz + composed.t[2];
            if (bounds_mesh->bounds_radius > 0) {
                bounding_radius = (int)(((int64_t)bounds_mesh->bounds_radius *
                                         max_scale_q12 + (ONE / 2)) >> 12);
            }
        } else if (ent->mesh == PS1_MESH_PLANE) {
            /* Planes are large flat quads */
            bounding_radius = (int)(((int64_t)bounding_radius * 3) >> 1);
        }
        if (bounds_mesh) {
            if (bounding_radius < 1) bounding_radius = 1;
        } else if (bounding_radius < 128) {
            bounding_radius = 128;
        }

        if (cull_z + bounding_radius < s_near_z)
            return 0;
        /* Reject the entire object early when its closest bound is already
         * behind opaque fog. Partially intersecting objects continue into the
        * per-polygon all-vertices test below. */
        if (s_discard_solid_fog_polygons &&
            cull_z - bounding_radius >= s_fog_end_z)
            return 0;
        /* Shape-centered sphere culling. Distance is measured to the closest
         * point on the sphere, so an off-center/long mesh stays visible until
         * its actual geometry has left the configured range. */
        int64_t dist_sq = (int64_t)cull_x * cull_x +
                          (int64_t)cull_y * cull_y +
                          (int64_t)cull_z * cull_z;
        {
        const int object_far_z = distance_cull_applies &&
                                 s_distance_cull_z < s_far_z
            ? s_distance_cull_z : s_far_z;
        int64_t max_dist = (int64_t)object_far_z + bounding_radius;
        if (dist_sq > max_dist * max_dist)
            return 0;
        }
        if (frustum_cull_applies && s_geom_screen > 0 && cull_z > 0) {
            const int depth = cull_z > s_near_z ? cull_z : s_near_z;
            const int world_margin = s_frustum_cull_enabled
                ? s_frustum_cull_margin : 0;
            const int pixel_margin = s_frustum_cull_enabled ? 0 : 48;
            const int x_limit =
                (int)(((int64_t)depth * (s_screen_w / 2 + pixel_margin)) /
                      s_geom_screen) + bounding_radius + world_margin;
            const int y_limit =
                (int)(((int64_t)depth * (s_screen_h / 2 + pixel_margin)) /
                      s_geom_screen) + bounding_radius + world_margin;
            const int abs_x = cull_x < 0 ? -cull_x : cull_x;
            const int abs_y = cull_y < 0 ? -cull_y : cull_y;
            if (abs_x > x_limit || abs_y > y_limit)
                return 0;
        }
    }

    gte_SetRotMatrix(&composed);
    gte_SetTransMatrix(&composed);
    s_shading_unlit = ent->render_unlit != 0;
    if (!s_shading_unlit) gte_SetLightMatrix(&lmtx);
    s_shading_world = omtx;
    s_shading_rotation = rmtx;
    s_shading_mesh = 0;
    s_shading_next_mesh = 0;
    s_shading_lerp_q8 = 0;
    s_shading_use_probe = 0;
    s_shading_probe_source = 0;

#if PS1_PROJECTION_DEBUG
    if (ent->mesh == PS1_MESH_CUSTOM)
        draw_custom_projection_debug(pri, packet_end, ot);
#endif

    base_r = ent->color_r;
    base_g = ent->color_g;
    base_b = ent->color_b;
    if (ent->mesh == PS1_MESH_CUSTOM && ent->mesh_index > 0) {
        const unsigned int idx = (unsigned int)(ent->mesh_index - 1);
        if (idx < g_ps1_mesh_count) {
            const ps1_mesh* m = &g_ps1_meshes[idx];
            const ps1_mesh* m_next = 0;
            const int use_vertex_lerp = entity_vertex_lerp_mesh(ent, m, &m_next);
            s_shading_mesh = m;
            s_shading_next_mesh = use_vertex_lerp ? m_next : 0;
            s_shading_lerp_q8 = ent->vertex_anim_lerp_q8;
            if (!s_shading_unlit && s_scene_light_count && (rigid_frame || ent->vertex_anim_frame_count > 0)) {
                const ps1_mesh* source = m;
                SVECTOR center;
                VECTOR world_center;
                int32_t point[3];
                /* Small animated parts share one light sample, as rigid parts
                 * already share normal bins. Anchor vertex-animation probes
                 * to the clip's first frame so idle deformation does not force
                 * all triangle lighting to be rebuilt every rendered frame. */
                if (!rigid_frame && ent->vertex_anim_first_mesh_index > 0 &&
                    ent->vertex_anim_first_mesh_index <= g_ps1_mesh_count)
                    source = &g_ps1_meshes[ent->vertex_anim_first_mesh_index - 1u];
                center.vx = clamp_svector_i16((int32_t)source->bounds_center_x * source->scale_q12 / (m->scale_q12 ? m->scale_q12 : 1));
                center.vy = clamp_svector_i16((int32_t)source->bounds_center_y * source->scale_q12 / (m->scale_q12 ? m->scale_q12 : 1));
                center.vz = clamp_svector_i16((int32_t)source->bounds_center_z * source->scale_q12 / (m->scale_q12 ? m->scale_q12 : 1));
                center.pad = 0;
                transform_point(&omtx, &center, &world_center);
                point[0] = world_center.vx; point[1] = world_center.vy; point[2] = world_center.vz;
                s_shading_probe_count = ps1_lighting_prepare(s_scene_lights, s_scene_light_count,
                    point, s_shading_probe_lights);
                s_shading_use_probe = 1;
                s_shading_probe_source = source;
            }
            const int use_tex =
                ent->texture_index > 0 && m->uvs &&
                ent->texture_index <= g_ps1_texture_count &&
                ps1_texture_width((unsigned int)ent->texture_index) > 0 &&
                ps1_texture_height((unsigned int)ent->texture_index) > 0;
            const int is_terrain = (m->flags & PS1_MESH_FLAG_TERRAIN) != 0;
            const int is_smooth = (m->flags & PS1_MESH_FLAG_SMOOTH) != 0;
            if (rigid_frame) {
                uint32_t light_mask = m->rigid_light_bin_mask;
                int light_bin;
                /* Old/generated fallback meshes may not carry a mask. Their
                 * rigid triangles fall back to the neutral direction bin. */
                if (light_mask == 0u)
                    light_mask = 1u << 13;
                for (light_bin = 0; light_bin < 27; ++light_bin) {
                    if ((light_mask & (1u << light_bin)) != 0u) {
                        if (s_shading_unlit) {
                            rigid_light_colors[light_bin][0] = base_r;
                            rigid_light_colors[light_bin][1] = base_g;
                            rigid_light_colors[light_bin][2] = base_b;
                            continue;
                        }
                        SVECTOR normal;
                        rigid_light_bin_normal(light_bin, &normal);
                        gte_shade_normal_vec(
                            &normal, base_r, base_g, base_b,
                            &rigid_light_colors[light_bin][0],
                            &rigid_light_colors[light_bin][1],
                            &rigid_light_colors[light_bin][2]);
                    }
                }
            }
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
                is_viewmodel && m->vert_count <= PS1_VIEWMODEL_VERTEX_CACHE_MAX;
            const int viewmodel_light_cached =
                is_viewmodel && !s_shading_unlit &&
                m->tri_count <= PS1_VIEWMODEL_TRI_CACHE_MAX;
            const int rigid_cached =
                rigid_frame && m->position_refs &&
                m->vert_count <= PS1_RIGID_VERTEX_CACHE_MAX;
            const int static_cache_eligible =
                !is_viewmodel && !rigid_frame &&
                m->vert_count <= PS1_STATIC_VERTEX_CACHE_MAX;
            ps1_static_entity_cache* static_cache = static_cache_eligible
                ? static_entity_cache_for(ent, m) : 0;
            const int static_cached = static_cache != 0;
            const uint16_t static_vertex_offset = static_cached
                ? static_cache->vertex_offset : 0u;
            const uint16_t static_triangle_offset = static_cached
                ? static_cache->triangle_offset : 0u;
            if (is_viewmodel && ent->render_unlit && viewmodel_cached) {
                const int fast_added = draw_unlit_viewmodel_fast(
                    pri, packet_end, ot, ent, m, &composed, entity_near_z,
                    use_tex, tpage, tex_uoff, tex_voff);
                if (fast_added >= 0)
                    return fast_added;
            }
            if (viewmodel_cached) {
                /* Lit and Unlit must use the same unrestricted close-range
                 * projection, including camera-space vertices for clipping. */
                cache_viewmodel_projection(&composed, m, entity_near_z);
            }
            if (viewmodel_light_cached &&
                (!s_viewmodel_light_valid ||
                 s_viewmodel_light_entity != ent ||
                 s_viewmodel_light_tris != m->tris ||
                 s_viewmodel_light_base_r != base_r ||
                 s_viewmodel_light_base_g != base_g ||
                 s_viewmodel_light_base_b != base_b ||
                 s_viewmodel_normal_scale_q12[0] != s_normal_scale_q12[0] ||
                 s_viewmodel_normal_scale_q12[1] != s_normal_scale_q12[1] ||
                 s_viewmodel_normal_scale_q12[2] != s_normal_scale_q12[2] ||
                 !matrix_equal(&s_viewmodel_light_matrix, &lmtx) ||
                 (s_scene_light_count && (
                    s_viewmodel_light_revision != s_scene_light_revision ||
                    s_viewmodel_lighting_vertices != m->verts ||
                    !matrix_equal(&s_viewmodel_lighting_world, &omtx))))) {
                uint16_t ti;
                for (ti = 0; ti < m->tri_count; ++ti) {
                    const ps1_mesh_tri* face = &m->tris[ti];
                    SVECTOR normal = { face->nx, face->ny, face->nz, 0 };
                    SVECTOR center = mesh_face_center(m, face);
                    if (normal.vx == 0 && normal.vy == 0 && normal.vz == 0)
                        normal.vy = 3800;
                    viewmodel_cpu_shade_normal(
                        &lmtx, &normal, &center, base_r, base_g, base_b,
                        &s_viewmodel_face_light_cache[ti][0],
                        &s_viewmodel_face_light_cache[ti][1],
                        &s_viewmodel_face_light_cache[ti][2]);
                }
                s_viewmodel_light_entity = ent;
                s_viewmodel_light_tris = m->tris;
                s_viewmodel_light_matrix = lmtx;
                s_viewmodel_lighting_world = omtx;
                s_viewmodel_light_revision = s_scene_light_revision;
                s_viewmodel_lighting_vertices = m->verts;
                for (int axis = 0; axis < 3; ++axis)
                    s_viewmodel_normal_scale_q12[axis] = s_normal_scale_q12[axis];
                s_viewmodel_light_base_r = base_r;
                s_viewmodel_light_base_g = base_g;
                s_viewmodel_light_base_b = base_b;
                s_viewmodel_light_valid = 1;
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
                const int direct_position_refs =
                    m->position_refs && !is_smooth && !is_terrain;
                const int projection_dirty =
                    !static_cache->projection_valid ||
                    static_cache->mesh != m ||
                    static_cache->next_mesh != m_next ||
                    static_cache->vertex_lerp_q8 != ent->vertex_anim_lerp_q8 ||
                    static_cache->geom_screen != s_geom_screen ||
                    static_cache->safe_clip_near_z != entity_near_z ||
                    static_cache->safe_clip_far_z != s_far_z ||
                    !matrix_equal(&static_cache->projected_matrix, &composed);
                const int lighting_dirty =
                    !static_cache->lighting_valid ||
                    static_cache->lighting_unlit != s_shading_unlit ||
                    static_cache->lighting_tris != m->tris ||
                    static_cache->base_r != base_r ||
                    static_cache->base_g != base_g ||
                    static_cache->base_b != base_b ||
                    (!s_shading_unlit && (
                    static_cache->lighting_norms != m->norms ||
                    static_cache->lighting_face_norms != m->face_norms ||
                    static_cache->normal_scale_q12[0] != s_normal_scale_q12[0] ||
                    static_cache->normal_scale_q12[1] != s_normal_scale_q12[1] ||
                    static_cache->normal_scale_q12[2] != s_normal_scale_q12[2] ||
                    !matrix_equal(&static_cache->light_matrix, &lmtx) ||
                    (s_scene_light_count && (
                        static_cache->light_revision != s_scene_light_revision ||
                        !matrix_equal(&static_cache->lighting_world, &omtx) ||
                        static_cache->lighting_probe_source != s_shading_probe_source ||
                        (!s_shading_use_probe && (static_cache->lighting_vertices != m->verts ||
                            static_cache->lighting_next_mesh != m_next ||
                            static_cache->lighting_lerp_q8 != ent->vertex_anim_lerp_q8))))));
                if (projection_dirty) {
                    if (!is_viewmodel && !rigid_frame && ent->vertex_anim_frame_count == 0) {
                        cache_world_visibility(ent,m,static_cache,entity_near_z,&composed);
                    } else {
                    if (m->position_refs) {
                        uint16_t batch_indices[3];
                        SVECTOR batch_vertices[3];
                        int batch_count = 0;

                        /* Animated exports duplicate positions at UV/material
                         * seams. Project only vertices whose full animation
                         * trajectory is canonical. The flat cached draw path
                         * follows position_refs directly, so it needs no
                         * seam-vertex copy pass. */
                        for (vi = 0; vi < m->vert_count; ++vi) {
                            const uint16_t reference = m->position_refs[vi];
                            const SVECTOR* vertex;
                            if (reference != vi)
                                continue;
                            vertex = &m->verts[vi];
                            batch_indices[batch_count] = vi;
                            if (use_vertex_lerp) {
                                batch_vertices[batch_count] = lerp_mesh_svector_q8(
                                    vertex, m, &m_next->verts[vi], m_next,
                                    ent->vertex_anim_lerp_q8);
                            } else {
                                batch_vertices[batch_count] = *vertex;
                            }
                            ++batch_count;
                            if (batch_count == 3) {
                                int32_t sxy0, sxy1, sxy2;
                                int32_t z0, z1, z2;
                                gte_ldv3(&batch_vertices[0], &batch_vertices[1], &batch_vertices[2]);
                                gte_rtpt();
                                gte_stsxy0(&sxy0);
                                gte_stsxy1(&sxy1);
                                gte_stsxy2(&sxy2);
                                gte_stsz3(&z0, &z1, &z2);
                                s_static_screen_cache[static_vertex_offset + batch_indices[0]] = sxy0;
                                s_static_screen_cache[static_vertex_offset + batch_indices[1]] = sxy1;
                                s_static_screen_cache[static_vertex_offset + batch_indices[2]] = sxy2;
                                s_static_depth_cache[static_vertex_offset + batch_indices[0]] = z0;
                                s_static_depth_cache[static_vertex_offset + batch_indices[1]] = z1;
                                s_static_depth_cache[static_vertex_offset + batch_indices[2]] = z2;
                                batch_count = 0;
                            }
                        }
                        for (vi = 0; vi < (uint16_t)batch_count; ++vi) {
                            int32_t sxy;
                            int32_t depth;
                            const uint16_t index = batch_indices[vi];
                            gte_ldv0(&batch_vertices[vi]);
                            gte_rtps();
                            gte_stsxy(&sxy);
                            gte_stsz(&depth);
                            s_static_screen_cache[static_vertex_offset + index] = sxy;
                            s_static_depth_cache[static_vertex_offset + index] = depth;
                        }
                        if (!direct_position_refs) {
                            for (vi = 0; vi < m->vert_count; ++vi) {
                                uint16_t reference = m->position_refs[vi];
                                if (reference >= m->vert_count)
                                    reference = vi;
                                if (reference == vi)
                                    continue;
                                s_static_screen_cache[static_vertex_offset + vi] =
                                    s_static_screen_cache[static_vertex_offset + reference];
                                s_static_depth_cache[static_vertex_offset + vi] =
                                    s_static_depth_cache[static_vertex_offset + reference];
                            }
                        }
                    } else {
                    for (vi = 0; vi + 2u < m->vert_count; vi += 3u) {
                    int32_t sxy0, sxy1, sxy2;
                    int32_t z0, z1, z2;
                    SVECTOR interpolated[3];
                    const SVECTOR* v0 = &m->verts[vi];
                    const SVECTOR* v1 = &m->verts[vi + 1u];
                    const SVECTOR* v2 = &m->verts[vi + 2u];
                    if (use_vertex_lerp) {
                        interpolated[0] = lerp_mesh_svector_q8(
                            v0, m, &m_next->verts[vi], m_next,
                            ent->vertex_anim_lerp_q8);
                        interpolated[1] = lerp_mesh_svector_q8(
                            v1, m, &m_next->verts[vi + 1u], m_next,
                            ent->vertex_anim_lerp_q8);
                        interpolated[2] = lerp_mesh_svector_q8(
                            v2, m, &m_next->verts[vi + 2u], m_next,
                            ent->vertex_anim_lerp_q8);
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
                        interpolated = lerp_mesh_svector_q8(
                            vertex, m, &m_next->verts[vi], m_next,
                            ent->vertex_anim_lerp_q8);
                        vertex = &interpolated;
                    }
                    gte_ldv0(vertex);
                    gte_rtps();
                    gte_stsxy(&sxy);
                    gte_stsz(&depth);
                    s_static_screen_cache[static_vertex_offset + vi] = sxy;
                    s_static_depth_cache[static_vertex_offset + vi] = depth;
                    }
                    }
                    for (vi = 0; vi < m->vert_count; ++vi) {
                        if (direct_position_refs &&
                            m->position_refs[vi] != vi)
                            continue;
                        s_static_fog_cache[static_vertex_offset + vi] =
                            (uint16_t)scene_fog_amount_q8(
                                s_static_depth_cache[static_vertex_offset + vi]);
                    }
                    static_cache->visible_list_valid = 0;
                    }
                    /* The compact flat-mesh path deliberately avoids the
                     * per-triangle camera-space clipping setup. Remember
                     * whether the current projection contains a triangle
                     * which crosses the near plane or exceeds the GTE's safe
                     * screen-coordinate span. Only those meshes fall through
                     * to the general clipping path below. This keeps the hot
                     * path for ordinary props while preventing large floors
                     * from losing whole triangles at the edge of the view. */
                    if (!static_cache->visible_list_valid) static_cache->needs_safe_clip = 0;
                    if (!is_terrain && !is_smooth && !static_cache->visible_list_valid) {
                        uint16_t ti;
                        for (ti = 0; ti < m->tri_count; ++ti) {
                            const ps1_mesh_tri* face = &m->tris[ti];
                            uint16_t p0, p1, p2;
                            if (face->i0 >= m->vert_count ||
                                face->i1 >= m->vert_count ||
                                face->i2 >= m->vert_count)
                                continue;
                            p0 = m->position_refs
                                ? m->position_refs[face->i0] : face->i0;
                            p1 = m->position_refs
                                ? m->position_refs[face->i1] : face->i1;
                            p2 = m->position_refs
                                ? m->position_refs[face->i2] : face->i2;
                            if (p0 >= m->vert_count) p0 = face->i0;
                            if (p1 >= m->vert_count) p1 = face->i1;
                            if (p2 >= m->vert_count) p2 = face->i2;
                            if (cached_triangle_needs_safe_clip(
                                    s_static_screen_cache[static_vertex_offset + p0],
                                    s_static_screen_cache[static_vertex_offset + p1],
                                    s_static_screen_cache[static_vertex_offset + p2],
                                    s_static_depth_cache[static_vertex_offset + p0],
                                    s_static_depth_cache[static_vertex_offset + p1],
                                    s_static_depth_cache[static_vertex_offset + p2],
                                    entity_near_z, !authored_floor_face(ent, face))) {
                                static_cache->needs_safe_clip = 1;
                                break;
                            }
                        }
                    }
                    static_cache->projected_matrix = composed;
                    static_cache->geom_screen = s_geom_screen;
                    static_cache->safe_clip_near_z = entity_near_z;
                    static_cache->safe_clip_far_z = s_far_z;
                    static_cache->next_mesh = m_next;
                    static_cache->vertex_lerp_q8 = ent->vertex_anim_lerp_q8;
                    static_cache->projection_valid = 1;
                }
                if (lighting_dirty && is_smooth && !is_terrain && m->norms) {
                    for (vi = 0; vi < m->vert_count; ++vi) {
                        if (s_shading_unlit) {
                            s_static_light_cache[static_vertex_offset + vi][0] = base_r;
                            s_static_light_cache[static_vertex_offset + vi][1] = base_g;
                            s_static_light_cache[static_vertex_offset + vi][2] = base_b;
                            continue;
                        }
                        if (use_tex) {
                            gte_shade_textured_smooth_normal(
                                &m->norms[vi], &m->verts[vi], base_r, base_g, base_b,
                                &s_static_light_cache[static_vertex_offset + vi][0],
                                &s_static_light_cache[static_vertex_offset + vi][1],
                                &s_static_light_cache[static_vertex_offset + vi][2]);
                        } else {
                            gte_shade_normal(
                                &m->norms[vi], &m->verts[vi], base_r, base_g, base_b,
                                &s_static_light_cache[static_vertex_offset + vi][0],
                                &s_static_light_cache[static_vertex_offset + vi][1],
                                &s_static_light_cache[static_vertex_offset + vi][2]);
                        }
                    }
                }
                /* Topology and UVs can be shared between poses; character
                 * face normals change with deformation and invalidate lighting.
                 * Unlit meshes still avoid unnecessary lighting invalidation. */
                if (lighting_dirty && !is_smooth && !is_terrain) {
                    uint16_t ti;
                    for (ti = 0; ti < m->tri_count; ++ti) {
                        const ps1_mesh_tri* source = &m->tris[ti];
                        if (s_shading_unlit) {
                            /* Preserve the existing fixed-point material and
                             * face-colour modulation, without preparing a
                             * normal, centre or lighting command. */
                            s_static_face_light_cache[static_triangle_offset + ti][0] =
                                (uint8_t)((((uint32_t)255 * base_r) >> 8) * source->color_r >> 8);
                            s_static_face_light_cache[static_triangle_offset + ti][1] =
                                (uint8_t)((((uint32_t)255 * base_g) >> 8) * source->color_g >> 8);
                            s_static_face_light_cache[static_triangle_offset + ti][2] =
                                (uint8_t)((((uint32_t)255 * base_b) >> 8) * source->color_b >> 8);
                            continue;
                        }
                        SVECTOR normal = ps1_mesh_face_normal(m, source, ti);
                        SVECTOR center = mesh_face_center(m, source);
                        uint8_t cd[4] = { 128, 128, 128, 0 };
                        uint8_t r, g, b;
                        if (normal.vx == 0 && normal.vy == 0 && normal.vz == 0)
                            normal.vy = 3800;
                        gte_shade_normal(&normal, &center, 255, 255, 255, &cd[0], &cd[1], &cd[2]);
                        r = cd[0]; g = cd[1]; b = cd[2];
                        s_static_face_light_cache[static_triangle_offset + ti][0] =
                            (uint8_t)((((uint32_t)r * base_r) >> 8) * source->color_r >> 8);
                        s_static_face_light_cache[static_triangle_offset + ti][1] =
                            (uint8_t)((((uint32_t)g * base_g) >> 8) * source->color_g >> 8);
                        s_static_face_light_cache[static_triangle_offset + ti][2] =
                            (uint8_t)((((uint32_t)b * base_b) >> 8) * source->color_b >> 8);
                    }
                }
                if (lighting_dirty) {
                    static_cache->light_matrix = lmtx;
                    static_cache->lighting_world = omtx;
                    static_cache->light_revision = s_scene_light_revision;
                    static_cache->lighting_vertices = m->verts;
                    static_cache->lighting_next_mesh = m_next;
                    static_cache->lighting_lerp_q8 = ent->vertex_anim_lerp_q8;
                    static_cache->lighting_probe_source = s_shading_probe_source;
                    for (vi = 0; vi < 3; ++vi)
                        static_cache->normal_scale_q12[vi] = s_normal_scale_q12[vi];
                    static_cache->lighting_norms = m->norms;
                    static_cache->lighting_face_norms = m->face_norms;
                    static_cache->lighting_tris = m->tris;
                    static_cache->base_r = base_r;
                    static_cache->base_g = base_g;
                    static_cache->base_b = base_b;
                    static_cache->lighting_valid = 1;
                    static_cache->lighting_unlit = s_shading_unlit;
                }
                static_cache->mesh = m;
            }

            if (use_tex && tpage && (const char*)(*pri + sizeof(DR_TPAGE)) <= packet_end) {
                DR_TPAGE* dr = (DR_TPAGE*)*pri;
                setDrawTPage(dr, 0, 0, tpage);
                add_world_primitive(ot + (OT_LEN - 2), dr);
                *pri = (char*)(dr + 1);
            }

            int split_cached_flat = 0;
            if (static_cached && !is_terrain && !is_smooth) {
                const int fast_added = draw_cached_flat_mesh(
                    pri, packet_end, ot, ent, m,
                    static_vertex_offset, static_triangle_offset,
                    entity_near_z, shell_ot_bias,
                    use_tex, tpage, tex_uoff, tex_voff,
                    static_cache->needs_safe_clip,
                    static_cache->visible_list_valid ? &s_static_visible_triangles[static_triangle_offset] : 0,
                    static_cache->visible_triangle_count);
                if (!static_cache->needs_safe_clip)
                    return fast_added;
                added += fast_added;
                split_cached_flat = 1;
            }

            if (rigid_cached) {
                const int screen_right = s_screen_w + 64;
                const int screen_bottom = s_screen_h + 64;
                uint16_t ti;
                for (ti = 0; ti < m->tri_count; ++ti) {
                    const ps1_mesh_tri* t = &m->tris[ti];
                    const SVECTOR face_center = mesh_face_center(m, t);
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
                    if (!is_viewmodel &&
                        (z0 > s_far_z || z1 > s_far_z || z2 > s_far_z))
                        continue;
                    if ((x0 < -64 && x1 < -64 && x2 < -64) ||
                        (x0 > screen_right && x1 > screen_right && x2 > screen_right) ||
                        (y0 < -64 && y1 < -64 && y2 < -64) ||
                        (y0 > screen_bottom && y1 > screen_bottom && y2 > screen_bottom))
                        continue;
                    if (!is_viewmodel && discard_solid_fog_triangle(z0, z1, z2))
                        continue;

                    /* Rigid bones are separate meshes. Sort every triangle by
                     * its actual average camera depth, using the same /8 OT
                     * scale as mesh_triangle_otz_fine(). The old /16 scale
                     * collapsed nearby body surfaces into the same buckets,
                     * making limbs, torso and clothing overwrite each other. */
                    rigid_otz = (int)((z0 + z1 + z2) / 3) >> 3;
                    /* Accessory bias was authored in the previous /16 bucket
                     * scale; double it to preserve the same physical offset. */
                    rigid_otz -= shell_ot_bias * 2;
                    if (rigid_otz < PS1_WORLD_OT_MIN)
                        rigid_otz = PS1_WORLD_OT_MIN;
                    else if (rigid_otz >= OT_LEN)
                        rigid_otz = OT_LEN - 1;

                    const int32_t skinned_tri_depth = (z0 + z1 + z2) / 3;
                    const int tri_use_tex = use_tex && tpage && !is_triangle_fully_fogged(skinned_tri_depth) && (scene_fog_amount_q8(skinned_tri_depth) <= 0);
                    if (tri_use_tex) {
                        uint8_t fog_r = rigid_light_colors[light_bin][0];
                        uint8_t fog_g = rigid_light_colors[light_bin][1];
                        uint8_t fog_b = rigid_light_colors[light_bin][2];
                        POLY_FT3* tri;
                        if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                            return added;
                        tri = (POLY_FT3*)*pri;
                        setPolyFT3(tri);
                        setXY3(tri, x0, y0, x1, y1, x2, y2);
                        setRGB0(tri, fog_r, fog_g, fog_b);
                        tri->tpage = tpage;
                        tri->clut = 0;
                        setUV3(tri,
                               clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                               clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                               clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                        add_world_primitive(ot + rigid_otz, tri);
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
                        apply_scene_fog(skinned_tri_depth, &fog_r, &fog_g, &fog_b);
                        setRGB0(tri, fog_r, fog_g, fog_b);
                        add_world_primitive(ot + rigid_otz, tri);
                        *pri = (char*)(tri + 1);
                    }
                    ++added;
                }
                return added;
            }

            /* Most meshes use one entity material for every triangle.  Keep
             * that descriptor in registers, and only resolve a face material
             * when ProModeler explicitly overrides it.  The old loop called
             * five texture accessors for every textured triangle even though
             * all character triangles share the same texture index. */
            const int default_use_tex = use_tex;
            const uint16_t default_tpage = tpage;
            const int default_tex_uoff = tex_uoff;
            const int default_tex_voff = tex_voff;
            unsigned int cached_face_texture_index = 0xffffffffu;
            int cached_face_use_tex = 0;
            uint16_t cached_face_tpage = 0;
            int cached_face_tex_uoff = 0;
            int cached_face_tex_voff = 0;

            const uint16_t visible_count = static_cached && static_cache->visible_list_valid
                ? static_cache->visible_triangle_count : m->tri_count;
            for (uint16_t draw_index = 0; draw_index < visible_count; ++draw_index) {
                const uint16_t ti = static_cached && static_cache->visible_list_valid
                    ? s_static_visible_triangles[static_triangle_offset + draw_index] : draw_index;
                const ps1_mesh_tri* t = &m->tris[ti];
                if (t->i0 >= m->vert_count || t->i1 >= m->vert_count || t->i2 >= m->vert_count)
                    continue;
                if (static_cached && !is_viewmodel) {
                    uint16_t p0 = m->position_refs ? m->position_refs[t->i0] : t->i0;
                    uint16_t p1 = m->position_refs ? m->position_refs[t->i1] : t->i1;
                    uint16_t p2 = m->position_refs ? m->position_refs[t->i2] : t->i2;
                    int32_t z0, z1, z2;
                    if (p0 >= m->vert_count) p0 = t->i0;
                    if (p1 >= m->vert_count) p1 = t->i1;
                    if (p2 >= m->vert_count) p2 = t->i2;
                    z0 = s_static_depth_cache[static_vertex_offset + p0];
                    z1 = s_static_depth_cache[static_vertex_offset + p1];
                    z2 = s_static_depth_cache[static_vertex_offset + p2];
                    if (!rigid_frame && ent->vertex_anim_frame_count == 0 &&
                        (s_static_outcode_cache[static_vertex_offset + p0] &
                         s_static_outcode_cache[static_vertex_offset + p1] &
                         s_static_outcode_cache[static_vertex_offset + p2]))
                        continue;
                    if (split_cached_flat && !(static_cache->visible_list_valid
                        ? s_static_safe_clip_cache[static_triangle_offset+ti] : cached_triangle_needs_safe_clip(
                            s_static_screen_cache[static_vertex_offset + p0],
                            s_static_screen_cache[static_vertex_offset + p1],
                            s_static_screen_cache[static_vertex_offset + p2],
                            z0, z1, z2, entity_near_z, !authored_floor_face(ent, t))))
                        continue;
                    /* Reject only wholly invisible faces. A near-plane
                     * crossing must reach the clipper even when its GTE XY
                     * appears offscreen. Large subdivided floors otherwise
                     * transform and clip hundreds of faces behind the camera. */
                    if ((z0 < entity_near_z && z1 < entity_near_z && z2 < entity_near_z) ||
                        (z0 > s_far_z && z1 > s_far_z && z2 > s_far_z) ||
                        discard_solid_fog_triangle(z0, z1, z2))
                        continue;
                    if (z0 >= entity_near_z && z1 >= entity_near_z && z2 >= entity_near_z) {
                        const int32_t xy0 = s_static_screen_cache[static_vertex_offset + p0];
                        const int32_t xy1 = s_static_screen_cache[static_vertex_offset + p1];
                        const int32_t xy2 = s_static_screen_cache[static_vertex_offset + p2];
                        if (!(static_cache->visible_list_valid ? s_static_safe_clip_cache[static_triangle_offset+ti] :
                            cached_triangle_needs_safe_clip(xy0, xy1, xy2, z0, z1, z2, entity_near_z, !authored_floor_face(ent, t))) &&
                            triangle_outside_screen((int16_t)xy0, (int16_t)(xy0 >> 16),
                                (int16_t)xy1, (int16_t)(xy1 >> 16),
                                (int16_t)xy2, (int16_t)(xy2 >> 16)))
                            continue;
                    }
                }
                const SVECTOR face_center = static_cached ? (SVECTOR){0,0,0,0} : mesh_face_center(m, t);
                const unsigned int texture_index = t->texture_index != 0
                    ? (unsigned int)t->texture_index
                    : (unsigned int)ent->texture_index;
                int use_tex;
                uint16_t tpage;
                int tex_uoff;
                int tex_voff;
                SVECTOR faceN;
                uint8_t r, g, b;
                int32_t p, nclip_val;
                int16_t tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2;

                if (t->texture_index == 0) {
                    use_tex = default_use_tex;
                    tpage = default_tpage;
                    tex_uoff = default_tex_uoff;
                    tex_voff = default_tex_voff;
                } else {
                    if (cached_face_texture_index != texture_index) {
                        cached_face_texture_index = texture_index;
                        cached_face_use_tex =
                            texture_index <= g_ps1_texture_count && m->uvs &&
                            ps1_texture_width(texture_index) > 0 &&
                            ps1_texture_height(texture_index) > 0;
                        cached_face_tpage = cached_face_use_tex
                            ? ps1_texture_tpage(texture_index) : 0;
                        cached_face_tex_uoff = cached_face_use_tex
                            ? (int)ps1_texture_u_offset(texture_index) : 0;
                        cached_face_tex_voff = cached_face_use_tex
                            ? (int)ps1_texture_v_offset(texture_index) : 0;
                    }
                    use_tex = cached_face_use_tex;
                    tpage = cached_face_tpage;
                    tex_uoff = cached_face_tex_uoff;
                    tex_voff = cached_face_tex_voff;
                }
                if (t->i0 >= m->vert_count || t->i1 >= m->vert_count || t->i2 >= m->vert_count)
                    continue;

                {
                    SVECTOR lerp0, lerp1, lerp2;
                    const SVECTOR* v0 = &m->verts[t->i0];
                    const SVECTOR* v1 = &m->verts[t->i1];
                    const SVECTOR* v2 = &m->verts[t->i2];
                    /* Cached world meshes already interpolate in their cache
                     * pass. Viewmodels intentionally use the current baked
                     * frame here: that is the camera-relative correction path
                     * used before projection was optimized. In either case,
                     * per-triangle lerps would only discard their results. */
                    if (use_vertex_lerp && !static_cached && !viewmodel_cached) {
                        lerp0 = lerp_mesh_svector_q8(
                            v0, m, &m_next->verts[t->i0], m_next,
                            ent->vertex_anim_lerp_q8);
                        lerp1 = lerp_mesh_svector_q8(
                            v1, m, &m_next->verts[t->i1], m_next,
                            ent->vertex_anim_lerp_q8);
                        lerp2 = lerp_mesh_svector_q8(
                            v2, m, &m_next->verts[t->i2], m_next,
                            ent->vertex_anim_lerp_q8);
                        v0 = &lerp0;
                        v1 = &lerp1;
                        v2 = &lerp2;
                    }

                /* Unlit hands are the common close-camera case.  When the
                 * cached vertices are all in front of the near plane there is
                 * no reason to build clip vertices, evaluate normals, or run
                 * the general polygon clipper.  Keep the exact same screen
                 * winding/visibility tests and packet format as the full path,
                 * then fall through only for the rare near-plane crossing. */
                if (is_viewmodel && view_model_soft && s_shading_unlit &&
                    viewmodel_cached) {
                    const int32_t z0 = s_viewmodel_depth_cache[t->i0];
                    const int32_t z1 = s_viewmodel_depth_cache[t->i1];
                    const int32_t z2 = s_viewmodel_depth_cache[t->i2];
                    if (z0 >= entity_near_z && z1 >= entity_near_z &&
                        z2 >= entity_near_z) {
                        const int32_t sxy0 = s_viewmodel_screen_cache[t->i0];
                        const int32_t sxy1 = s_viewmodel_screen_cache[t->i1];
                        const int32_t sxy2 = s_viewmodel_screen_cache[t->i2];
                        const int16_t x0 = (int16_t)(sxy0 & 0xffff);
                        const int16_t y0 = (int16_t)(sxy0 >> 16);
                        const int16_t x1 = (int16_t)(sxy1 & 0xffff);
                        const int16_t y1 = (int16_t)(sxy1 >> 16);
                        const int16_t x2 = (int16_t)(sxy2 & 0xffff);
                        const int16_t y2 = (int16_t)(sxy2 >> 16);
                        const int32_t depth = (z0 + z1 + z2) / 3;
                        if (depth > 0 &&
                            !triangle_outside_screen(x0, y0, x1, y1, x2, y2) &&
                            screen_front_facing(x0, y0, x1, y1, x2, y2)) {
                            const int fast_otz = viewmodel_triangle_otz(depth);
                            if (use_tex && tpage) {
                                POLY_FT3* tri;
                                if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                                    return added;
                                tri = (POLY_FT3*)*pri;
                                setPolyFT3(tri);
                                setXY3(tri, x0, y0, x1, y1, x2, y2);
                                setRGB0(tri, base_r, base_g, base_b);
                                tri->tpage = tpage;
                                tri->clut = 0;
                                setUV3(tri,
                                       clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                                       clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                                       clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                                add_world_primitive(ot + fast_otz, tri);
                                *pri = (char*)(tri + 1);
                                ++added;
                            } else {
                                POLY_F3* tri;
                                if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                    return added;
                                tri = (POLY_F3*)*pri;
                                setPolyF3(tri);
                                setXY3(tri, x0, y0, x1, y1, x2, y2);
                                setRGB0(tri, base_r, base_g, base_b);
                                add_world_primitive(ot + fast_otz, tri);
                                *pri = (char*)(tri + 1);
                                ++added;
                            }
                        }
                        continue;
                    }
                }

                if (is_viewmodel && view_model_soft) {
                    ps1_clip_vert src[3];
                    if (viewmodel_cached) {
                        VECTOR cached_p[3];
                        const int all_front =
                            s_viewmodel_depth_cache[t->i0] >= entity_near_z &&
                            s_viewmodel_depth_cache[t->i1] >= entity_near_z &&
                            s_viewmodel_depth_cache[t->i2] >= entity_near_z;
                        if (all_front) {
                            /* The hot path only needs projected XY and depth.
                             * Camera-space XY is intentionally left at zero;
                             * screen-space winding below performs backface
                             * rejection without 64-bit perspective products. */
                            cached_p[0].vx = cached_p[0].vy = 0;
                            cached_p[1].vx = cached_p[1].vy = 0;
                            cached_p[2].vx = cached_p[2].vy = 0;
                            cached_p[0].vz = s_viewmodel_depth_cache[t->i0];
                            cached_p[1].vz = s_viewmodel_depth_cache[t->i1];
                            cached_p[2].vz = s_viewmodel_depth_cache[t->i2];
                        } else {
                            /* Only the rare near-plane crossing needs full
                             * camera-space coordinates for software clipping. */
                            cached_p[0] = s_viewmodel_vertex_cache[t->i0];
                            cached_p[1] = s_viewmodel_vertex_cache[t->i1];
                            cached_p[2] = s_viewmodel_vertex_cache[t->i2];
                        }
                        make_cached_clip_vert(&cached_p[0],
                                              (use_tex && m->uvs) ? &m->uvs[t->i0] : 0, &src[0]);
                        make_cached_clip_vert(&cached_p[1],
                                              (use_tex && m->uvs) ? &m->uvs[t->i1] : 0, &src[1]);
                        make_cached_clip_vert(&cached_p[2],
                                              (use_tex && m->uvs) ? &m->uvs[t->i2] : 0, &src[2]);
                    } else {
                        make_clip_vert(&composed, v0,
                                       (use_tex && m->uvs) ? &m->uvs[t->i0] : 0, &src[0]);
                        make_clip_vert(&composed, v1,
                                       (use_tex && m->uvs) ? &m->uvs[t->i1] : 0, &src[1]);
                        make_clip_vert(&composed, v2,
                                       (use_tex && m->uvs) ? &m->uvs[t->i2] : 0, &src[2]);
                    }

                    if (src[0].p.vz >= entity_near_z &&
                        src[1].p.vz >= entity_near_z &&
                        src[2].p.vz >= entity_near_z &&
                        !viewmodel_cached &&
                        !camera_space_front_facing(src))
                        continue;

                    if (src[0].p.vz >= entity_near_z &&
                        src[1].p.vz >= entity_near_z &&
                        src[2].p.vz >= entity_near_z) {
                        int16_t x0, y0, x1, y1, x2, y2;
                        if (viewmodel_cached) {
                            const int32_t sxy0 = s_viewmodel_screen_cache[t->i0];
                            const int32_t sxy1 = s_viewmodel_screen_cache[t->i1];
                            const int32_t sxy2 = s_viewmodel_screen_cache[t->i2];
                            x0 = (int16_t)(sxy0 & 0xffff);
                            y0 = (int16_t)(sxy0 >> 16);
                            x1 = (int16_t)(sxy1 & 0xffff);
                            y1 = (int16_t)(sxy1 >> 16);
                            x2 = (int16_t)(sxy2 & 0xffff);
                            y2 = (int16_t)(sxy2 >> 16);
                        } else {
                            project_clip_vert(&src[0], &x0, &y0, entity_near_z);
                            project_clip_vert(&src[1], &x1, &y1, entity_near_z);
                            project_clip_vert(&src[2], &x2, &y2, entity_near_z);
                        }
                        if (triangle_outside_screen(x0, y0, x1, y1, x2, y2))
                            continue;
                        if (!screen_front_facing(x0, y0, x1, y1, x2, y2))
                            continue;

                        p = (src[0].p.vz + src[1].p.vz + src[2].p.vz) / 3;
                        if (p <= 0)
                            continue;
                        otz = viewmodel_triangle_otz(p);

                        faceN = ps1_mesh_face_normal(m, t, ti);
                        if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                            faceN.vy = 3800;

                        if (viewmodel_light_cached) {
                            r = s_viewmodel_face_light_cache[ti][0];
                            g = s_viewmodel_face_light_cache[ti][1];
                            b = s_viewmodel_face_light_cache[ti][2];
                        } else {
                            viewmodel_cpu_shade_normal(
                                &lmtx, &faceN, &face_center, base_r, base_g, base_b, &r, &g, &b);
                        }

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
                                add_world_primitive(ot + otz, tri);
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
                                add_world_primitive(ot + otz, tri);
                                *pri = (char*)(tri + 1);
                                ++added;
                            }
                        }
                        continue;
                    }

                    {
                        ps1_clip_vert clipped[6];
                        int clipped_count = clip_triangle_near(src, clipped, entity_near_z);
                        int qi;
                        if (clipped_count < 3)
                            continue;

                        faceN = ps1_mesh_face_normal(m, t, ti);
                        if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                            faceN.vy = 3800;
                        if (viewmodel_light_cached) {
                            r = s_viewmodel_face_light_cache[ti][0];
                            g = s_viewmodel_face_light_cache[ti][1];
                            b = s_viewmodel_face_light_cache[ti][2];
                        } else {
                            viewmodel_cpu_shade_normal(
                                &lmtx, &faceN, &face_center, base_r, base_g, base_b, &r, &g, &b);
                        }

                        for (qi = 0; qi < clipped_count - 2; ++qi) {
                            const ps1_clip_vert* c0 = &clipped[0];
                            const ps1_clip_vert* c1 = &clipped[qi + 1];
                            const ps1_clip_vert* c2 = &clipped[qi + 2];
                            int16_t x0, y0, x1, y1, x2, y2;
                            project_clip_vert(c0, &x0, &y0, entity_near_z);
                            project_clip_vert(c1, &x1, &y1, entity_near_z);
                            project_clip_vert(c2, &x2, &y2, entity_near_z);
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
                                    add_world_primitive(ot + otz, tri);
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
                                    add_world_primitive(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                    ++added;
                                }
                            }
                        }
                        continue;
                    }
                }

                /* The GTE cannot clip a polygon against the camera plane or a
                 * safe viewport span. The old world path either discarded a
                 * whole face when one point crossed the near plane, or sent
                 * oversized coordinates that the GPU dropped/wrapped. Only
                 * triangles needing clipping take this CPU path. */
                if (!is_viewmodel && !rigid_frame &&
                    ent->vertex_anim_frame_count == 0) {
                    ps1_clip_vert src[3];
                    int needs_cpu_clip;
                    if (static_cached) {
                        uint16_t p0 = m->position_refs
                            ? m->position_refs[t->i0] : t->i0;
                        uint16_t p1 = m->position_refs
                            ? m->position_refs[t->i1] : t->i1;
                        uint16_t p2 = m->position_refs
                            ? m->position_refs[t->i2] : t->i2;
                        int32_t csxy0, csxy1, csxy2;
                        if (p0 >= m->vert_count) p0 = t->i0;
                        if (p1 >= m->vert_count) p1 = t->i1;
                        if (p2 >= m->vert_count) p2 = t->i2;
                        csxy0 = s_static_screen_cache[static_vertex_offset + p0];
                        csxy1 = s_static_screen_cache[static_vertex_offset + p1];
                        csxy2 = s_static_screen_cache[static_vertex_offset + p2];
                        needs_cpu_clip = static_cache->visible_list_valid ?
                            s_static_safe_clip_cache[static_triangle_offset+ti] : cached_triangle_needs_safe_clip(
                            csxy0, csxy1, csxy2,
                            s_static_depth_cache[static_vertex_offset + p0],
                            s_static_depth_cache[static_vertex_offset + p1],
                            s_static_depth_cache[static_vertex_offset + p2],
                            entity_near_z, !authored_floor_face(ent, t));
                    } else {
                        make_clip_vert(&composed, v0,
                                       (use_tex && m->uvs) ? &m->uvs[t->i0] : 0, &src[0]);
                        make_clip_vert(&composed, v1,
                                       (use_tex && m->uvs) ? &m->uvs[t->i1] : 0, &src[1]);
                        make_clip_vert(&composed, v2,
                                       (use_tex && m->uvs) ? &m->uvs[t->i2] : 0, &src[2]);
                        needs_cpu_clip =
                            camera_triangle_needs_safe_clip(src, entity_near_z, !authored_floor_face(ent, t));
                    }

                    if (needs_cpu_clip) {
                        ps1_clip_vert clipped[6];
                        const int clipped_smooth =
                            is_smooth && !is_terrain && m->norms;
                        int clipped_count;
                        int qi;

                        if (static_cached) {
                            uint16_t p0 = m->position_refs ? m->position_refs[t->i0] : t->i0;
                            uint16_t p1 = m->position_refs ? m->position_refs[t->i1] : t->i1;
                            uint16_t p2 = m->position_refs ? m->position_refs[t->i2] : t->i2;
                            if (p0 >= m->vert_count) p0 = t->i0;
                            if (p1 >= m->vert_count) p1 = t->i1;
                            if (p2 >= m->vert_count) p2 = t->i2;
                            make_cached_clip_vert(&s_static_camera_cache[static_vertex_offset + p0],
                                           (use_tex && m->uvs) ? &m->uvs[t->i0] : 0, &src[0]);
                            make_cached_clip_vert(&s_static_camera_cache[static_vertex_offset + p1],
                                           (use_tex && m->uvs) ? &m->uvs[t->i1] : 0, &src[1]);
                            make_cached_clip_vert(&s_static_camera_cache[static_vertex_offset + p2],
                                           (use_tex && m->uvs) ? &m->uvs[t->i2] : 0, &src[2]);
                        }

                        faceN = ps1_mesh_face_normal(m, t, ti);
                        if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                            faceN.vy = 3800;

                        if (clipped_smooth) {
                            uint8_t sr, sg, sb;
                            if (static_cached) {
                                src[0].r = s_static_light_cache[static_vertex_offset + t->i0][0];
                                src[0].g = s_static_light_cache[static_vertex_offset + t->i0][1];
                                src[0].b = s_static_light_cache[static_vertex_offset + t->i0][2];
                                src[1].r = s_static_light_cache[static_vertex_offset + t->i1][0];
                                src[1].g = s_static_light_cache[static_vertex_offset + t->i1][1];
                                src[1].b = s_static_light_cache[static_vertex_offset + t->i1][2];
                                src[2].r = s_static_light_cache[static_vertex_offset + t->i2][0];
                                src[2].g = s_static_light_cache[static_vertex_offset + t->i2][1];
                                src[2].b = s_static_light_cache[static_vertex_offset + t->i2][2];
                            } else {
                                if (use_tex) {
                                    gte_shade_textured_smooth_normal(
                                        &m->norms[t->i0], &m->verts[t->i0], base_r, base_g, base_b,
                                        &sr, &sg, &sb);
                                } else {
                                    gte_shade_normal(&m->norms[t->i0], &m->verts[t->i0], base_r, base_g, base_b,
                                                     &sr, &sg, &sb);
                                }
                                src[0].r = sr; src[0].g = sg; src[0].b = sb;
                                if (use_tex) {
                                    gte_shade_textured_smooth_normal(
                                        &m->norms[t->i1], &m->verts[t->i1], base_r, base_g, base_b,
                                        &sr, &sg, &sb);
                                } else {
                                    gte_shade_normal(&m->norms[t->i1], &m->verts[t->i1], base_r, base_g, base_b,
                                                     &sr, &sg, &sb);
                                }
                                src[1].r = sr; src[1].g = sg; src[1].b = sb;
                                if (use_tex) {
                                    gte_shade_textured_smooth_normal(
                                        &m->norms[t->i2], &m->verts[t->i2], base_r, base_g, base_b,
                                        &sr, &sg, &sb);
                                } else {
                                    gte_shade_normal(&m->norms[t->i2], &m->verts[t->i2], base_r, base_g, base_b,
                                                     &sr, &sg, &sb);
                                }
                                src[2].r = sr; src[2].g = sg; src[2].b = sb;
                            }
                        } else {
                            if (static_cached && !is_terrain) {
                                r = s_static_face_light_cache[static_triangle_offset + ti][0];
                                g = s_static_face_light_cache[static_triangle_offset + ti][1];
                                b = s_static_face_light_cache[static_triangle_offset + ti][2];
                            } else if (is_terrain) {
                                r = (uint8_t)(((uint16_t)t->shade * t->color_r) / 255u);
                                g = (uint8_t)(((uint16_t)t->shade * t->color_g) / 255u);
                                b = (uint8_t)(((uint16_t)t->shade * t->color_b) / 255u);
                            } else {
                                uint8_t cd[4];
                                cd[0] = 128; cd[1] = 128; cd[2] = 128; cd[3] = 0;
                                gte_shade_normal(&faceN, &face_center, 255, 255, 255, &cd[0], &cd[1], &cd[2]);
                                r = cd[0]; g = cd[1]; b = cd[2];
                                r = (uint8_t)(((uint16_t)r * base_r) >> 8);
                                g = (uint8_t)(((uint16_t)g * base_g) >> 8);
                                b = (uint8_t)(((uint16_t)b * base_b) >> 8);
                                r = (uint8_t)(((uint16_t)r * t->color_r) >> 8);
                                g = (uint8_t)(((uint16_t)g * t->color_g) >> 8);
                                b = (uint8_t)(((uint16_t)b * t->color_b) >> 8);
                            }
                        }

                        clipped_count = clip_world_depth(src, clipped, entity_near_z, s_far_z);
                        if (clipped_count < 3)
                            continue;

                        {
                            ps1_screen_clip_vert projected[6];
                            ps1_screen_clip_vert screen_clipped[12];
                            int screen_count;
                            int reuse_projection = static_cached && clipped_count == 3;
                            uint16_t cached_positions[3] = {t->i2, t->i0, t->i1};
                            for (qi = 0; reuse_projection && qi < 3; ++qi) {
                                uint16_t reference = m->position_refs
                                    ? m->position_refs[cached_positions[qi]] : cached_positions[qi];
                                if (reference >= m->vert_count) reference = cached_positions[qi];
                                cached_positions[qi] = reference;
                                const int z = src[qi].p.vz;
                                if (z < entity_near_z || z > s_far_z || z > 65535 ||
                                    src[qi].p.vx < -32768 || src[qi].p.vx > 32767 ||
                                    src[qi].p.vy < -32768 || src[qi].p.vy > 32767 ||
                                    ps1_projection_vertex_needs_clip(
                                        s_static_screen_cache[static_vertex_offset + reference],
                                        s_static_depth_cache[static_vertex_offset + reference],
                                        entity_near_z, s_far_z, s_geom_screen))
                                    reuse_projection = 0;
                            }
                            for (qi = 0; qi < clipped_count; ++qi) {
                                if (reuse_projection) {
                                    const int32_t xy = s_static_screen_cache[
                                        static_vertex_offset + cached_positions[qi]];
                                    projected[qi].x = (int16_t)xy;
                                    projected[qi].y = (int16_t)(xy >> 16);
                                    projected[qi].z = clipped[qi].p.vz;
                                    projected[qi].u = clipped[qi].u;
                                    projected[qi].v = clipped[qi].v;
                                    projected[qi].r = clipped[qi].r;
                                    projected[qi].g = clipped[qi].g;
                                    projected[qi].b = clipped[qi].b;
                                } else project_clip_vert_wide(
                                    &clipped[qi], &projected[qi], entity_near_z);
                            }
                            screen_count = clip_polygon_to_safe_screen(
                                projected, clipped_count, screen_clipped);

                        for (qi = 0; qi < screen_count - 2; ++qi) {
                            ps1_screen_clip_vert fragments[PS1_AFFINE_FRAGMENTS][3];
                            const int authored_floor = authored_floor_face(ent, t);
                            const int subdivide = use_tex && !authored_floor;
                            int fragment_count = 1;
                            if (subdivide) {
                                const ps1_screen_clip_vert face[3] = {
                                    screen_clipped[0], screen_clipped[qi + 1], screen_clipped[qi + 2] };
                                fragment_count = ps1_affine_subdivide(face, fragments);
                            }
                            for (int fi = 0; fi < fragment_count; ++fi) {
                            const ps1_screen_clip_vert* c0 = subdivide ? &fragments[fi][0] : &screen_clipped[0];
                            const ps1_screen_clip_vert* c1 = subdivide ? &fragments[fi][1] : &screen_clipped[qi+1];
                            const ps1_screen_clip_vert* c2 = subdivide ? &fragments[fi][2] : &screen_clipped[qi+2];
                            int16_t x0, y0, x1, y1, x2, y2;
                            uint8_t cr0, cg0, cb0, cr1, cg1, cb1, cr2, cg2, cb2;

                            x0 = (int16_t)c0->x; y0 = (int16_t)c0->y;
                            x1 = (int16_t)c1->x; y1 = (int16_t)c1->y;
                            x2 = (int16_t)c2->x; y2 = (int16_t)c2->y;
                            if (!screen_front_facing_strict(x0, y0, x1, y1, x2, y2))
                                continue;
                            /* Floor boundaries use shared projection and
                             * clipping, not per-triangle geometry inflation. */
                            if (!authored_floor)
                                SEAM_FILL_EXPAND(x0, y0, x1, y1, x2, y2);

                            /* Large front AND rear surfaces use the same
                             * bounded subdivision. Their local fragment depth
                             * can now order a nearby wall in front of stairs,
                             * instead of sorting a whole oblique wall by its
                             * distant centroid. */
                            p = ps1_clipped_face_sort_depth(c0->z, c1->z, c2->z,
                                                           entity_near_z, s_far_z);
                            if (p <= 0 || (!is_viewmodel && (p > (s_far_z >> 2))))
                                continue;
                            if (!is_viewmodel &&
                                discard_solid_fog_triangle(c0->z, c1->z, c2->z))
                                continue;
                            otz = mesh_triangle_otz_fine(p);
                            otz = floor_surface_otz(
                                ent, t, otz,
                                authored_floor ? src[0].p.vz : c0->z,
                                authored_floor ? src[1].p.vz : c1->z,
                                authored_floor ? src[2].p.vz : c2->z,
                                authored_floor ? src[2].p.vz : c2->z);
                            if (smooth_object_otz_floor > 0 && otz < smooth_object_otz_floor)
                                otz = smooth_object_otz_floor;
                            if (shell_ot_bias > 0) {
                                otz -= shell_ot_bias * 2;
                                if (otz < PS1_WORLD_OT_MIN)
                                    otz = PS1_WORLD_OT_MIN;
                            }

                            if (clipped_smooth) {
                                cr0 = clamp_u8(c0->r); cg0 = clamp_u8(c0->g); cb0 = clamp_u8(c0->b);
                                cr1 = clamp_u8(c1->r); cg1 = clamp_u8(c1->g); cb1 = clamp_u8(c1->b);
                                cr2 = clamp_u8(c2->r); cg2 = clamp_u8(c2->g); cb2 = clamp_u8(c2->b);
                                apply_scene_fog3(
                                    p << 2,
                                    &cr0, &cg0, &cb0,
                                    &cr1, &cg1, &cb1,
                                    &cr2, &cg2, &cb2);
                                if (use_tex && tpage &&
                                    (!is_triangle_fully_fogged(c0->z) || !is_triangle_fully_fogged(c1->z) ||
                                     !is_triangle_fully_fogged(c2->z) || ps1_texture_has_cutout(texture_index))) {
                                    POLY_GT3* tri;
                                    if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                        return added;
                                    tri = (POLY_GT3*)*pri;
                                    setPolyGT3(tri);
                                    setXY3(tri, x0, y0, x1, y1, x2, y2);
                                    tri->r0 = cr0; tri->g0 = cg0; tri->b0 = cb0;
                                    tri->r1 = cr1; tri->g1 = cg1; tri->b1 = cb1;
                                    tri->r2 = cr2; tri->g2 = cg2; tri->b2 = cb2;
                                    tri->tpage = tpage;
                                    tri->clut = 0;
                                    setUV3(tri,
                                           clamp_u8(tex_uoff + c0->u), clamp_u8(tex_voff + c0->v),
                                           clamp_u8(tex_uoff + c1->u), clamp_u8(tex_voff + c1->v),
                                           clamp_u8(tex_uoff + c2->u), clamp_u8(tex_voff + c2->v));
                                    add_world_primitive(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                } else {
                                    POLY_G3* tri;
                                    if ((const char*)(*pri + sizeof(POLY_G3)) > packet_end)
                                        return added;
                                    tri = (POLY_G3*)*pri;
                                    setPolyG3(tri);
                                    setXY3(tri, x0, y0, x1, y1, x2, y2);
                                    tri->r0 = cr0; tri->g0 = cg0; tri->b0 = cb0;
                                    tri->r1 = cr1; tri->g1 = cg1; tri->b1 = cb1;
                                    tri->r2 = cr2; tri->g2 = cg2; tri->b2 = cb2;
                                    add_world_primitive(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                }
                            } else {
                                cr0 = r; cg0 = g; cb0 = b;
                                cr1 = r; cg1 = g; cb1 = b;
                                cr2 = r; cg2 = g; cb2 = b;
                                apply_scene_fog(c0->z, &cr0, &cg0, &cb0);
                                apply_scene_fog(c1->z, &cr1, &cg1, &cb1);
                                apply_scene_fog(c2->z, &cr2, &cg2, &cb2);
                                if (use_tex && tpage &&
                                    (!is_triangle_fully_fogged(c0->z) || !is_triangle_fully_fogged(c1->z) ||
                                     !is_triangle_fully_fogged(c2->z) || ps1_texture_has_cutout(texture_index))) {
                                    POLY_GT3* tri;
                                    if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                        return added;
                                    tri = (POLY_GT3*)*pri;
                                    setPolyGT3(tri);
                                    setXY3(tri, x0, y0, x1, y1, x2, y2);
                                    setRGB0(tri, cr0, cg0, cb0);
                                    setRGB1(tri, cr1, cg1, cb1);
                                    setRGB2(tri, cr2, cg2, cb2);
                                    tri->tpage = tpage;
                                    tri->clut = 0;
                                    setUV3(tri,
                                           clamp_u8(tex_uoff + c0->u), clamp_u8(tex_voff + c0->v),
                                           clamp_u8(tex_uoff + c1->u), clamp_u8(tex_voff + c1->v),
                                           clamp_u8(tex_uoff + c2->u), clamp_u8(tex_voff + c2->v));
                                    add_world_primitive(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                } else {
                                    POLY_F3* tri;
                                    if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                        return added;
                                    tri = (POLY_F3*)*pri;
                                    setPolyF3(tri);
                                    setXY3(tri, x0, y0, x1, y1, x2, y2);
                                    setRGB0(tri, cr0, cg0, cb0);
                                    add_world_primitive(ot + otz, tri);
                                    *pri = (char*)(tri + 1);
                                }
                            }
                            ++added;
                        }
                        }
                        }
                        continue;
                    }
                }

                int is_backface = 0;
                int32_t z0 = 0, z1 = 0, z2 = 0;
                if (static_cached) {
                    uint16_t p0 = m->position_refs
                        ? m->position_refs[t->i0] : t->i0;
                    uint16_t p1 = m->position_refs
                        ? m->position_refs[t->i1] : t->i1;
                    uint16_t p2 = m->position_refs
                        ? m->position_refs[t->i2] : t->i2;
                    int32_t sxy0, sxy1, sxy2;
                    if (p0 >= m->vert_count) p0 = t->i0;
                    if (p1 >= m->vert_count) p1 = t->i1;
                    if (p2 >= m->vert_count) p2 = t->i2;
                    sxy0 = s_static_screen_cache[static_vertex_offset + p0];
                    sxy1 = s_static_screen_cache[static_vertex_offset + p1];
                    sxy2 = s_static_screen_cache[static_vertex_offset + p2];
                    z0 = s_static_depth_cache[static_vertex_offset + p0];
                    z1 = s_static_depth_cache[static_vertex_offset + p1];
                    z2 = s_static_depth_cache[static_vertex_offset + p2];
                    if (z0 < entity_near_z || z1 < entity_near_z || z2 < entity_near_z)
                        continue;
                    if (z0 > s_far_z || z1 > s_far_z || z2 > s_far_z)
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
                    z0 = s_rigid_depth_cache[p0];
                    z1 = s_rigid_depth_cache[p1];
                    z2 = s_rigid_depth_cache[p2];
                    if (z0 <= 0 || z1 <= 0 || z2 <= 0)
                        continue;
                    if (!is_viewmodel &&
                        (z0 > s_far_z || z1 > s_far_z || z2 > s_far_z))
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
                    gte_ldv3(v0, v1, v2);
                    gte_rtpt();
                    gte_stsz3(&z0, &z1, &z2);
                    if (!is_viewmodel && !rigid_frame &&
                        (z0 < entity_near_z || z1 < entity_near_z || z2 < entity_near_z))
                        continue;
                    if (!is_viewmodel && !rigid_frame &&
                        (z0 > s_far_z || z1 > s_far_z || z2 > s_far_z))
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
                if (p <= 0 || (!is_viewmodel && (p > (s_far_z >> 2))))
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
                if (!is_viewmodel && !rigid_frame &&
                    ent->vertex_anim_frame_count == 0 && !authored_floor_face(ent, t)) {
                    /* UV-repeat subdivision can leave adjacent exported
                     * vertices one fixed-point unit apart. At PS1 projection
                     * precision that becomes a dotted one-pixel background
                     * seam, so overlap static world triangles by one pixel. */
                    SEAM_FILL_EXPAND(
                        tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                }
                const int32_t tri_depth = (z0 + z1 + z2) / 3;
                if (!is_viewmodel && discard_solid_fog_triangle(z0, z1, z2))
                    continue;
                /* Every world mesh must use the same physical depth scale.
                 * Mixing /8 character buckets with /16 static-mesh buckets
                 * made geometry behind a character sort as if it were in
                 * front. /8 is also fine enough for close material shells. */
                otz = mesh_triangle_otz_fine(p);
                if (is_viewmodel)
                    otz = viewmodel_triangle_otz(p);
                else if (!rigid_frame)
                    otz = floor_surface_otz(ent, t, otz, z0, z1, z2, z2);
                if (smooth_object_otz_floor > 0 && otz < smooth_object_otz_floor)
                    otz = smooth_object_otz_floor;
                if (!is_viewmodel && shell_ot_bias > 0) {
                    otz -= shell_ot_bias * 2;
                    if (otz < PS1_WORLD_OT_MIN)
                        otz = PS1_WORLD_OT_MIN;
                }

                /* Per-face GTE lighting using the exported face normal.
                 * For reversed-winding copies (back faces), the cross product
                 * in the exporter produces an inward-pointing normal. GTE
                 * will light it correctly facing the interior light source. */
                faceN = ps1_mesh_face_normal(m, t, ti);
                if (faceN.vx == 0 && faceN.vy == 0 && faceN.vz == 0)
                    faceN.vy = 3800;


                if (use_tex && tpage) {
                    if (is_triangle_fully_fogged(tri_depth) && !ps1_texture_has_cutout(texture_index)) {
                        POLY_F3* tri;
                        if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                            return added;
                        tri = (POLY_F3*)*pri;
                        setPolyF3(tri);
                        setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                        setRGB0(tri, g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
                        add_world_primitive(ot + otz, tri);
                        *pri = (char*)(tri + 1);
                        ++added;
                        continue;
                    }
                    if (!rigid_frame && !is_terrain && is_smooth && m->norms) {
                        uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
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
                            gte_shade_textured_smooth_normal(&m->norms[t->i0], &m->verts[t->i0], base_r, base_g, base_b, &r0, &g0, &b0);
                            gte_shade_textured_smooth_normal(&m->norms[t->i1], &m->verts[t->i1], base_r, base_g, base_b, &r1, &g1, &b1);
                            gte_shade_textured_smooth_normal(&m->norms[t->i2], &m->verts[t->i2], base_r, base_g, base_b, &r2, &g2, &b2);
                        }
                        /* Preserve texture detail throughout the fade. Fully
                         * fogged faces use the opaque branch above; partial fog
                         * modulates per-vertex colour on a textured primitive. */
                        if (g_ps1_scene.fog_enabled) {
                            int f0 = scene_fog_amount_q8(z0);
                            int f1 = scene_fog_amount_q8(z1);
                            int f2 = scene_fog_amount_q8(z2);
                            if (f0 > 0 || f1 > 0 || f2 > 0) {
                                POLY_GT3* gtri;
                                if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                    return added;
                                gtri = (POLY_GT3*)*pri;
                                setPolyGT3(gtri);
                                setXY3(gtri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                                gtri->tpage = tpage;
                                gtri->clut = 0;
                                setUV3(gtri,
                                    clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                                    clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                                    clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                                apply_scene_fog_amount(f0, &r0, &g0, &b0);
                                apply_scene_fog_amount(f1, &r1, &g1, &b1);
                                apply_scene_fog_amount(f2, &r2, &g2, &b2);
                                gtri->r0 = r0; gtri->g0 = g0; gtri->b0 = b0;
                                gtri->r1 = r1; gtri->g1 = g1; gtri->b1 = b1;
                                gtri->r2 = r2; gtri->g2 = g2; gtri->b2 = b2;
                                add_world_primitive(ot + otz, gtri);
                                *pri = (char*)(gtri + 1);
                                ++added;
                                continue;
                            }
                        }
                        {
                            POLY_GT3* tri;
                            if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                return added;
                            tri = (POLY_GT3*)*pri;
                            setPolyGT3(tri);
                            setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                            tri->r0 = r0; tri->g0 = g0; tri->b0 = b0;
                            tri->r1 = r1; tri->g1 = g1; tri->b1 = b1;
                            tri->r2 = r2; tri->g2 = g2; tri->b2 = b2;
                            tri->tpage = tpage;
                            tri->clut = 0;
                            setUV3(tri,
                                   clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                                   clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                                   clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                            add_world_primitive(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                            ++added;
                        }
                        continue;
                    }
                    {
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
                                gte_shade_normal(&faceN, &face_center, 255, 255, 255, &cd[0], &cd[1], &cd[2]);
                                r = cd[0]; g = cd[1]; b = cd[2];
                            }
                            if (is_terrain) {
                                r = (uint8_t)(((uint16_t)t->shade * t->color_r) / 255u);
                                g = (uint8_t)(((uint16_t)t->shade * t->color_g) / 255u);
                                b = (uint8_t)(((uint16_t)t->shade * t->color_b) / 255u);
                            }
                            if (!rigid_frame) {
                                r = (uint8_t)(((uint16_t)r * base_r) >> 8);
                                g = (uint8_t)(((uint16_t)g * base_g) >> 8);
                                b = (uint8_t)(((uint16_t)b * base_b) >> 8);
                                if (!is_terrain) {
                                    r = (uint8_t)(((uint16_t)r * t->color_r) >> 8);
                                    g = (uint8_t)(((uint16_t)g * t->color_g) >> 8);
                                    b = (uint8_t)(((uint16_t)b * t->color_b) >> 8);
                                }
                            }
                        }
                        /* Partial fog must not erase the material's texture. */
                        if (g_ps1_scene.fog_enabled) {
                            int f0 = scene_fog_amount_q8(z0);
                            int f1 = scene_fog_amount_q8(z1);
                            int f2 = scene_fog_amount_q8(z2);
                            if (f0 > 0 || f1 > 0 || f2 > 0) {
                                POLY_GT3* gtri;
                                if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                    return added;
                                gtri = (POLY_GT3*)*pri;
                                setPolyGT3(gtri);
                                setXY3(gtri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                                gtri->tpage = tpage;
                                gtri->clut = 0;
                                setUV3(gtri,
                                    clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                                    clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                                    clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                                uint8_t r0 = r, g0 = g, b0 = b;
                                uint8_t r1 = r, g1 = g, b1 = b;
                                uint8_t r2 = r, g2 = g, b2 = b;
                                apply_scene_fog_amount(f0, &r0, &g0, &b0);
                                apply_scene_fog_amount(f1, &r1, &g1, &b1);
                                apply_scene_fog_amount(f2, &r2, &g2, &b2);
                                gtri->r0 = r0; gtri->g0 = g0; gtri->b0 = b0;
                                gtri->r1 = r1; gtri->g1 = g1; gtri->b1 = b1;
                                gtri->r2 = r2; gtri->g2 = g2; gtri->b2 = b2;
                                add_world_primitive(ot + otz, gtri);
                                *pri = (char*)(gtri + 1);
                                ++added;
                                continue;
                            }
                        }
                        {
                            POLY_FT3* tri;
                            if ((const char*)(*pri + sizeof(POLY_FT3)) > packet_end)
                                return added;
                            tri = (POLY_FT3*)*pri;
                            setPolyFT3(tri);
                            setXY3(tri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                            setRGB0(tri, r, g, b);
                            tri->tpage = tpage;
                            tri->clut = 0;
                            setUV3(tri,
                                   clamp_u8(tex_uoff + m->uvs[t->i0].u), clamp_u8(tex_voff + m->uvs[t->i0].v),
                                   clamp_u8(tex_uoff + m->uvs[t->i1].u), clamp_u8(tex_voff + m->uvs[t->i1].v),
                                   clamp_u8(tex_uoff + m->uvs[t->i2].u), clamp_u8(tex_voff + m->uvs[t->i2].v));
                            add_world_primitive(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                            ++added;
                        }
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
                            gte_shade_normal(&m->norms[t->i0], &m->verts[t->i0], base_r, base_g, base_b, &r0, &g0, &b0);
                            gte_shade_normal(&m->norms[t->i1], &m->verts[t->i1], base_r, base_g, base_b, &r1, &g1, &b1);
                            gte_shade_normal(&m->norms[t->i2], &m->verts[t->i2], base_r, base_g, base_b, &r2, &g2, &b2);
                        }
                        apply_scene_fog_vertex3(
                            z0, z1, z2,
                            &r0, &g0, &b0,
                            &r1, &g1, &b1,
                            &r2, &g2, &b2);
                        tri->r0 = r0; tri->g0 = g0; tri->b0 = b0;
                        tri->r1 = r1; tri->g1 = g1; tri->b1 = b1;
                        tri->r2 = r2; tri->g2 = g2; tri->b2 = b2;
                        add_world_primitive(ot + otz, tri);
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
                                gte_shade_normal(&faceN, &face_center, 255, 255, 255, &cd[0], &cd[1], &cd[2]);
                                r = cd[0]; g = cd[1]; b = cd[2];
                            }
                            if (is_terrain) {
                                r = (uint8_t)(((uint16_t)t->shade * t->color_r) / 255u);
                                g = (uint8_t)(((uint16_t)t->shade * t->color_g) / 255u);
                                b = (uint8_t)(((uint16_t)t->shade * t->color_b) / 255u);
                            }
                            if (!rigid_frame) {
                                r = (uint8_t)(((uint16_t)r * base_r) >> 8);
                                g = (uint8_t)(((uint16_t)g * base_g) >> 8);
                                b = (uint8_t)(((uint16_t)b * base_b) >> 8);
                                if (!is_terrain) {
                                    r = (uint8_t)(((uint16_t)r * t->color_r) >> 8);
                                    g = (uint8_t)(((uint16_t)g * t->color_g) >> 8);
                                    b = (uint8_t)(((uint16_t)b * t->color_b) >> 8);
                                }
                            }
                        }

                        if (g_ps1_scene.fog_enabled) {
                            POLY_G3* gtri;
                            if ((const char*)(*pri + sizeof(POLY_G3)) > packet_end)
                                return added;
                            gtri = (POLY_G3*)*pri;
                            setPolyG3(gtri);
                            setXY3(gtri, tri_x0, tri_y0, tri_x1, tri_y1, tri_x2, tri_y2);
                            uint8_t r0 = r, g0 = g, b0 = b;
                            uint8_t r1 = r, g1 = g, b1 = b;
                            uint8_t r2 = r, g2 = g, b2 = b;
                            apply_scene_fog_vertex3(z0, z1, z2, &r0, &g0, &b0, &r1, &g1, &b1, &r2, &g2, &b2);
                            gtri->r0 = r0; gtri->g0 = g0; gtri->b0 = b0;
                            gtri->r1 = r1; gtri->g1 = g1; gtri->b1 = b1;
                            gtri->r2 = r2; gtri->g2 = g2; gtri->b2 = b2;
                            add_world_primitive(ot + otz, gtri);
                            *pri = (char*)(gtri + 1);
                            ++added;
                            continue;
                        }
                        apply_scene_fog(tri_depth, &r, &g, &b);
                        setRGB0(tri, r, g, b);
                        add_world_primitive(ot + otz, tri);
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

        /* Keep each material repeat in one UV cell, including fractional
         * tiling, offsets and mirroring. Refine long spans for GTE precision. */
        ps1_plane_span spans_x[PS1_PLANE_AXIS_CAP], spans_z[PS1_PLANE_AXIS_CAP];
        const int tiles_x = ps1_plane_axis(use_texture ? ent->texture_tiling_u_q8 : 0,
            ent->texture_offset_u_q8, ent->mesh_subdivision_levels, spans_x);
        const int tiles_z = ps1_plane_axis(use_texture ? ent->texture_tiling_v_q8 : 0,
            ent->texture_offset_v_q8, ent->mesh_subdivision_levels, spans_z);

        for (int tz = 0; tz < tiles_z; ++tz) {
            for (int tx = 0; tx < tiles_x; ++tx) {
                SVECTOR pv0, pv1, pv2, pv3;
                uint8_t colors[4][3];
                const int x0 = -hx + (int)((int64_t)2*hx*spans_x[tx].start_q16/65536);
                const int x1 = -hx + (int)((int64_t)2*hx*spans_x[tx].end_q16/65536);
                const int z0 = -hz + (int)((int64_t)2*hz*spans_z[tz].start_q16/65536);
                const int z1 = -hz + (int)((int64_t)2*hz*spans_z[tz].end_q16/65536);
                const int tile_u0 = use_texture
                    ? texture_u0 + ((texture_u1-texture_u0)*spans_x[tx].uv_start_q8+128)/256 : 0;
                const int tile_u1 = use_texture
                    ? texture_u0 + ((texture_u1-texture_u0)*spans_x[tx].uv_end_q8+128)/256 : 0;
                const int tile_v0 = use_texture
                    ? texture_v0 + ((texture_v1-texture_v0)*spans_z[tz].uv_start_q8+128)/256 : 0;
                const int tile_v1 = use_texture
                    ? texture_v0 + ((texture_v1-texture_v0)*spans_z[tz].uv_end_q8+128)/256 : 0;

                pv0.vx = (short)x0; pv0.vy = 0; pv0.vz = (short)z0; pv0.pad = 0;
                pv1.vx = (short)x1; pv1.vy = 0; pv1.vz = (short)z0; pv1.pad = 0;
                pv2.vx = (short)x1; pv2.vy = 0; pv2.vz = (short)z1; pv2.pad = 0;
                pv3.vx = (short)x0; pv3.vy = 0; pv3.vz = (short)z1; pv3.pad = 0;

                /* Match the editor's Gouraud lighting. Sampling only the tile
                 * centre made point lights appear as large square patches. */
                gte_shade_normal(&s_plane_norm_up, &pv0, base_r, base_g, base_b, &colors[0][0], &colors[0][1], &colors[0][2]);
                gte_shade_normal(&s_plane_norm_up, &pv1, base_r, base_g, base_b, &colors[1][0], &colors[1][1], &colors[1][2]);
                gte_shade_normal(&s_plane_norm_up, &pv2, base_r, base_g, base_b, &colors[2][0], &colors[2][1], &colors[2][2]);
                gte_shade_normal(&s_plane_norm_up, &pv3, base_r, base_g, base_b, &colors[3][0], &colors[3][1], &colors[3][2]);
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
                    int32_t cz0 = (int32_t)(((int64_t)composed.m[2][0]*pv0.vx + (int64_t)composed.m[2][1]*pv0.vy + (int64_t)composed.m[2][2]*pv0.vz) >> 12) + composed.t[2];
                    int32_t cz1 = (int32_t)(((int64_t)composed.m[2][0]*pv1.vx + (int64_t)composed.m[2][1]*pv1.vy + (int64_t)composed.m[2][2]*pv1.vz) >> 12) + composed.t[2];
                    int32_t cz2 = (int32_t)(((int64_t)composed.m[2][0]*pv2.vx + (int64_t)composed.m[2][1]*pv2.vy + (int64_t)composed.m[2][2]*pv2.vz) >> 12) + composed.t[2];
                    int32_t tri_depth1 = (cz0 + cz1 + cz2) / 3;
                    /* Use the same OT scale as Cube/Sphere primitives.
                     * The previous p>>2 put Plane in buckets half as deep,
                     * causing it to paint over geometry physically above it. */
                    otz = mesh_triangle_otz_fine(p);
                    {
                        ps1_mesh_tri floor_face = { 0 };
                        floor_face.ny = ONE;
                        otz = floor_surface_otz(ent, &floor_face, otz, cz0, cz1, cz2, cz2);
                    }
                    if (cached_triangle_needs_safe_clip(sxy0,sxy1,sxy2,cz0,cz1,cz2,entity_near_z,0)) {
                        added += draw_clipped_plane_triangle(pri,packet_end,ot,ent,&composed,entity_near_z,
                            &pv0,&pv1,&pv2,colors[0],colors[1],colors[2],
                            tile_u0,tile_v0,tile_u1,tile_v0,tile_u1,tile_v1,use_texture,texture_tpage);
                    } else if (
                        otz < OT_LEN && (p << 2) <= s_far_z &&
                        (is_viewmodel ||
                         !discard_solid_fog_triangle(cz0, cz1, cz2))) {
                        if (is_triangle_fully_fogged(cz0) && is_triangle_fully_fogged(cz1) && is_triangle_fully_fogged(cz2) && !ps1_texture_has_cutout(ent->texture_index)) {
                            POLY_F3* tri;
                            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                return added;
                            tri = (POLY_F3*)*pri;
                            setPolyF3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setRGB0(tri, g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
                            add_world_primitive(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        } else if (use_texture) {
                            POLY_GT3* tri;
                            if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                return added;
                            tri = (POLY_GT3*)*pri;
                            setPolyGT3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setUV3(tri, tile_u0, tile_v0,
                                   tile_u1, tile_v0,
                                   tile_u1, tile_v1);
                            setRGB0(tri, colors[0][0], colors[0][1], colors[0][2]);
                            setRGB1(tri, colors[1][0], colors[1][1], colors[1][2]);
                            setRGB2(tri, colors[2][0], colors[2][1], colors[2][2]);
                            apply_scene_fog(cz0, &tri->r0, &tri->g0, &tri->b0);
                            apply_scene_fog(cz1, &tri->r1, &tri->g1, &tri->b1);
                            apply_scene_fog(cz2, &tri->r2, &tri->g2, &tri->b2);
                            tri->tpage = texture_tpage;
                            tri->clut = 0;
                            add_world_primitive(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        } else {
                            POLY_G3* tri;
                            if ((const char*)(*pri + sizeof(POLY_G3)) > packet_end)
                                return added;
                            tri = (POLY_G3*)*pri;
                            setPolyG3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setRGB0(tri, colors[0][0], colors[0][1], colors[0][2]);
                            setRGB1(tri, colors[1][0], colors[1][1], colors[1][2]);
                            setRGB2(tri, colors[2][0], colors[2][1], colors[2][2]);
                            add_world_primitive(ot + otz, tri);
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
                    int32_t cz0 = (int32_t)(((int64_t)composed.m[2][0]*pv0.vx + (int64_t)composed.m[2][1]*pv0.vy + (int64_t)composed.m[2][2]*pv0.vz) >> 12) + composed.t[2];
                    int32_t cz2 = (int32_t)(((int64_t)composed.m[2][0]*pv2.vx + (int64_t)composed.m[2][1]*pv2.vy + (int64_t)composed.m[2][2]*pv2.vz) >> 12) + composed.t[2];
                    int32_t cz3 = (int32_t)(((int64_t)composed.m[2][0]*pv3.vx + (int64_t)composed.m[2][1]*pv3.vy + (int64_t)composed.m[2][2]*pv3.vz) >> 12) + composed.t[2];
                    int32_t tri_depth2 = (cz0 + cz2 + cz3) / 3;
                    otz = mesh_triangle_otz_fine(p);
                    {
                        ps1_mesh_tri floor_face = { 0 };
                        floor_face.ny = ONE;
                        otz = floor_surface_otz(ent, &floor_face, otz, cz0, cz2, cz3, cz3);
                    }
                    if (cached_triangle_needs_safe_clip(sxy0,sxy1,sxy2,cz0,cz2,cz3,entity_near_z,0)) {
                        added += draw_clipped_plane_triangle(pri,packet_end,ot,ent,&composed,entity_near_z,
                            &pv0,&pv2,&pv3,colors[0],colors[2],colors[3],
                            tile_u0,tile_v0,tile_u1,tile_v1,tile_u0,tile_v1,use_texture,texture_tpage);
                    } else if (
                        otz < OT_LEN && (p << 2) <= s_far_z &&
                        (is_viewmodel ||
                         !discard_solid_fog_triangle(cz0, cz2, cz3))) {
                        if (is_triangle_fully_fogged(cz0) && is_triangle_fully_fogged(cz2) && is_triangle_fully_fogged(cz3) && !ps1_texture_has_cutout(ent->texture_index)) {
                            POLY_F3* tri;
                            if ((const char*)(*pri + sizeof(POLY_F3)) > packet_end)
                                return added;
                            tri = (POLY_F3*)*pri;
                            setPolyF3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setRGB0(tri, g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
                            add_world_primitive(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        } else if (use_texture) {
                            POLY_GT3* tri;
                            if ((const char*)(*pri + sizeof(POLY_GT3)) > packet_end)
                                return added;
                            tri = (POLY_GT3*)*pri;
                            setPolyGT3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setUV3(tri, tile_u0, tile_v0,
                                   tile_u1, tile_v1,
                                   tile_u0, tile_v1);
                            setRGB0(tri, colors[0][0], colors[0][1], colors[0][2]);
                            setRGB1(tri, colors[2][0], colors[2][1], colors[2][2]);
                            setRGB2(tri, colors[3][0], colors[3][1], colors[3][2]);
                            apply_scene_fog(cz0, &tri->r0, &tri->g0, &tri->b0);
                            apply_scene_fog(cz2, &tri->r1, &tri->g1, &tri->b1);
                            apply_scene_fog(cz3, &tri->r2, &tri->g2, &tri->b2);
                            tri->tpage = texture_tpage;
                            tri->clut = 0;
                            add_world_primitive(ot + otz, tri);
                            *pri = (char*)(tri + 1);
                        } else {
                            POLY_G3* tri;
                            if ((const char*)(*pri + sizeof(POLY_G3)) > packet_end)
                                return added;
                            tri = (POLY_G3*)*pri;
                            setPolyG3(tri);
                            setXY3(tri,
                                   (int16_t)(sxy0 & 0xffff), (int16_t)(sxy0 >> 16),
                                   (int16_t)(sxy1 & 0xffff), (int16_t)(sxy1 >> 16),
                                   (int16_t)(sxy2 & 0xffff), (int16_t)(sxy2 >> 16));
                            setRGB0(tri, colors[0][0], colors[0][1], colors[0][2]);
                            setRGB1(tri, colors[2][0], colors[2][1], colors[2][2]);
                            setRGB2(tri, colors[3][0], colors[3][1], colors[3][2]);
                            add_world_primitive(ot + otz, tri);
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
            if (cv0.vz < entity_near_z || cv1.vz < entity_near_z ||
                cv2.vz < entity_near_z)
                continue;
            if (cv0.vz > s_far_z || cv1.vz > s_far_z || cv2.vz > s_far_z)
                continue;
            if (!is_viewmodel &&
                discard_solid_fog_triangle(cv0.vz, cv1.vz, cv2.vz))
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
            if (otz >= OT_LEN || (p << 2) > s_far_z)
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
                apply_scene_fog((cv0.vz + cv1.vz + cv2.vz) / 3, &r, &g, &b);
                setRGB0(poly, r, g, b);
                add_world_primitive(ot + otz, poly);
                *pri = (char*)(poly + 1);
                ++added;
            }
        }
        return added;
    }

    {
        for (fi = 0; fi < 6; ++fi) {
            int32_t p;
            VECTOR cv0, cv1, cv2, cv3;

        transform_point(&composed, &s_cube_verts[s_cube_faces[fi].v0], &cv0);
        transform_point(&composed, &s_cube_verts[s_cube_faces[fi].v1], &cv1);
        transform_point(&composed, &s_cube_verts[s_cube_faces[fi].v2], &cv2);
        transform_point(&composed, &s_cube_verts[s_cube_faces[fi].v3], &cv3);
        if (cv0.vz < entity_near_z || cv1.vz < entity_near_z ||
            cv2.vz < entity_near_z || cv3.vz < entity_near_z)
            continue;
        if (cv0.vz > s_far_z || cv1.vz > s_far_z || cv2.vz > s_far_z || cv3.vz > s_far_z)
            continue;
        if (!is_viewmodel &&
            discard_solid_fog_quad(cv0.vz, cv1.vz, cv2.vz, cv3.vz))
            continue;
        if (!camera_space_quad_front_facing(&cv0, &cv1, &cv2))
            continue;

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
        if (otz >= OT_LEN || (p << 2) > s_far_z)
            continue;

        if ((const char*)(*pri + sizeof(POLY_G4)) > packet_end)
            return added;
        POLY_G4* poly = (POLY_G4*)*pri;
        setPolyG4(poly);

        gte_stsxy0(&poly->x0);
        gte_stsxy1(&poly->x1);
        gte_stsxy2(&poly->x2);

        gte_ldv0(&s_cube_verts[s_cube_faces[fi].v3]);
        gte_rtps();
        gte_stsxy(&poly->x3);

        /* Each face keeps its hard normal, while positional light falloff
         * varies continuously across its vertices as in the editor. */
        gte_shade_normal(&s_cube_norms[fi], &s_cube_verts[s_cube_faces[fi].v0], base_r, base_g, base_b, &poly->r0, &poly->g0, &poly->b0);
        gte_shade_normal(&s_cube_norms[fi], &s_cube_verts[s_cube_faces[fi].v1], base_r, base_g, base_b, &poly->r1, &poly->g1, &poly->b1);
        gte_shade_normal(&s_cube_norms[fi], &s_cube_verts[s_cube_faces[fi].v2], base_r, base_g, base_b, &poly->r2, &poly->g2, &poly->b2);
        gte_shade_normal(&s_cube_norms[fi], &s_cube_verts[s_cube_faces[fi].v3], base_r, base_g, base_b, &poly->r3, &poly->g3, &poly->b3);
        apply_scene_fog(cv0.vz, &poly->r0, &poly->g0, &poly->b0);
        apply_scene_fog(cv1.vz, &poly->r1, &poly->g1, &poly->b1);
        apply_scene_fog(cv2.vz, &poly->r2, &poly->g2, &poly->b2);
        apply_scene_fog(cv3.vz, &poly->r3, &poly->g3, &poly->b3);

        gte_avsz4();
        gte_stotz(&p);
        otz = mesh_triangle_otz_fine(p);
        if (otz >= OT_LEN || (p << 2) > s_far_z)
            continue;
        /* Preserve this face's depth. Moving every face to the whole cube's
         * farthest corner let nearer floor tiles paint over its front face. */

            add_world_primitive(ot + otz, poly);
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
                 * the average camera Z divided by four. Occluders use clipped
                 * CPU-side Z values, so reproduce that AVSZ3 scale, then use
                 * the same /8 world OT scale as every visible mesh. */
                int otz = mesh_triangle_otz_fine(local_z >> 2);
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
                add_world_primitive(ot + otz, triangle);
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
                add_world_primitive(ot + otz, triangle);
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
    s_render_camera_source = cam_idx >= 0
        ? ps1_scene_source_entity((unsigned int)cam_idx) : 0;
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
    s_shared_character_transform.valid = 0;

    pri = *nextpri;

    /* Use the full scene entity count so entities beyond the 128-slot runtime
     * cap (e.g. character bones exported at the end of the list) are rendered.
     * ps1_scene_entity() falls back to read-only ROM data for those indices. */
    count = g_ps1_scene.entity_count;
    if (s_indexed_entity_count != count)
        rebuild_render_entity_lists();
    actor_mask = build_occluder_actor_mask();
    background_index = active_background_texture_index();
    /* With the flat fog-color background, a polygon whose every vertex has
     * reached fully opaque fog is visually identical to the cleared pixels.
     * Skip its GPU packet entirely. Keep it when a prerendered background is
     * active because that polygon may still be needed to hide the image. */
    s_discard_solid_fog_polygons =
        (uint8_t)(g_ps1_scene.fog_enabled && background_index == 0);
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
            setShadeTex(bg, 1);
            bg->tpage = tpage;
            bg->clut = 0;
            add_world_primitive(ot + (OT_LEN - 1), bg);
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

    /* Explicit Character presets are dynamic even without baked animation.
     * Give them packet priority and avoid treating their transforms as props. */
    for (i = 0; i < s_character_count; ++i) {
        const ps1_entity* ent = ps1_scene_entity(s_character_entities[i]);
        if (!ent || !ent->mesh_enabled)
            continue;
        total_added += draw_entity(&pri, world_packet_end, ot, ent, 0);
        if ((const char*)pri >= world_packet_end) {
            any_overflow = 1;
            break;
        }
    }

    /* Corridor geometry is close-camera environment geometry. Submit it
     * before ordinary props so packet pressure cannot punch holes in walls. */
    for (i = 0; i < s_corridor_count; ++i) {
        const ps1_entity* ent = ps1_scene_entity(s_corridor_entities[i]);
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
