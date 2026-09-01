#ifndef MIPSYNC_SCENE_H
#define MIPSYNC_SCENE_H

#include "fixedp.h"
#include "mipsync_gte.h"
#include <stdint.h>

#define PS1_MAX_FIELD_VALUES 32

typedef enum {
    PS1_MESH_NONE   = 0,
    PS1_MESH_CUBE   = 1,
    PS1_MESH_PLANE  = 2,
    PS1_MESH_SPHERE = 3,
    PS1_MESH_CUSTOM = 4,
} ps1_mesh_kind;

typedef struct ps1_mesh_tri {
    uint16_t i0, i1, i2;
    int16_t  nx, ny, nz; /* 12.4 fixed (ONE=4096) */
    uint8_t  shade;      /* 0-255 baked light, mainly for terrain */
    uint8_t  color_r, color_g, color_b; /* baked vertex color for terrain paint */
    uint8_t  texture_index; /* 0 = use entity material; 1+ = face material */
} ps1_mesh_tri;

typedef struct ps1_uv8 {
    uint8_t u;
    uint8_t v;
} ps1_uv8;

typedef struct ps1_mesh {
    const SVECTOR*      verts;     /* local space, PSX units */
    const SVECTOR*      norms;     /* local space normals, 12.4 fixed (ONE=4096) */
    uint16_t            vert_count;
    const ps1_uv8*      uvs;       /* NULL = flat shaded only */
    const uint16_t*     position_refs; /* optional first-identical-position index per UV vertex */
    const ps1_mesh_tri* tris;
    uint16_t            tri_count;
    int16_t             scale_q12; /* 12.12 scale from normalized PS1 verts to source mesh size */
    int16_t             bounds_center_x; /* normalized local-space AABB center */
    int16_t             bounds_center_y;
    int16_t             bounds_center_z;
    uint16_t            bounds_radius;   /* sphere enclosing the normalized local AABB */
    uint8_t             flags;
    /* Bit N is set when a rigid triangle uses lighting direction bin N.
     * Precomputing this at export avoids shading all 27 possible directions
     * for every small character part on every frame. */
    uint32_t            rigid_light_bin_mask;
} ps1_mesh;

#define PS1_MESH_FLAG_TERRAIN 1
#define PS1_MESH_FLAG_SMOOTH  2

typedef enum {
    PS1_RENDER_PRESET_PROP = 0,
    PS1_RENDER_PRESET_CORRIDOR = 1,
    PS1_RENDER_PRESET_CHARACTER = 2,
    PS1_RENDER_PRESET_VIEWMODEL = 3,
    PS1_RENDER_PRESET_FLOOR = 4
} ps1_render_preset;

typedef struct ps1_entity {
    uint32_t    id;
    const char* name;
    int16_t     parent_entity_index; /* source scene index, -1 for root */
    fix16_t     position[3];
    fix16_t     rotation[3]; /* euler degrees */
    fix16_t     scale[3];
    ps1_mesh_kind mesh;
    uint16_t    mesh_index; /* for PS1_MESH_CUSTOM */
    fix16_t     mesh_size;
    uint8_t     color_r, color_g, color_b;
    uint8_t     texture_index; /* 0 = none; 1+ indexes g_ps1_textures */
    int16_t     texture_tiling_u_q8; /* Material tiling, signed 8.8 fixed */
    int16_t     texture_tiling_v_q8;
    int16_t     texture_offset_u_q8; /* Material offset, signed 8.8 fixed */
    int16_t     texture_offset_v_q8;
    uint8_t     mesh_enabled;
    uint8_t     render_preset;
    uint8_t     prerender_occluder;
    uint8_t     seam_fill;
    uint8_t     mesh_subdivision_levels; /* 0=default, 1..4 authored render subdivision */
    uint16_t    vertex_anim_first_mesh_index;
    uint16_t    vertex_anim_frame_count;
    uint8_t     vertex_anim_fps;
    uint16_t    vertex_anim_next_mesh_index;
    uint8_t     vertex_anim_lerp_q8;
    uint16_t    vertex_idle_first_mesh_index;
    uint16_t    vertex_idle_frame_count;
    uint8_t     vertex_idle_fps;
    uint16_t    vertex_walk_first_mesh_index;
    uint16_t    vertex_walk_frame_count;
    uint8_t     vertex_walk_fps;
    uint16_t    vertex_aim_first_mesh_index;
    uint16_t    vertex_aim_frame_count;
    uint8_t     vertex_aim_fps;
    uint16_t    vertex_trigger_first_mesh_index;
    uint16_t    vertex_trigger_frame_count;
    uint8_t     vertex_trigger_fps;
    uint16_t    rigid_anim_first_frame;
    uint16_t    rigid_anim_frame_count;
    uint8_t     rigid_anim_fps;
    uint16_t    rigid_anim_current_frame;
    uint16_t    rigid_anim_next_frame;
    uint8_t     rigid_anim_lerp_q8;
    uint16_t    rigid_anim_blend_target_frame_plus_one; /* runtime-only absolute target */
    uint16_t    rigid_idle_first_frame;
    uint16_t    rigid_idle_frame_count;
    uint8_t     rigid_idle_fps;
    uint16_t    rigid_walk_first_frame;
    uint16_t    rigid_walk_frame_count;
    uint8_t     rigid_walk_fps;
    uint16_t    rigid_aim_first_frame;
    uint16_t    rigid_aim_frame_count;
    uint8_t     rigid_aim_fps;
    uint16_t    rigid_trigger_first_frame;
    uint16_t    rigid_trigger_frame_count;
    uint8_t     rigid_trigger_fps;
    uint16_t    animator_trigger_parameter_hash;
    uint16_t    animator_active_trigger_hash;
    uint16_t    animator_trigger_exit_duration_q8; /* 8.8 seconds; 0 = full clip */
    uint16_t    animator_trigger_transition_duration_q8; /* 8.8 seconds */
    int16_t     rigid_root_entity_index;
    fix16_t     animator_speed;
    fix16_t     animator_aim;
    uint16_t    transform_anim_first_key;
    uint16_t    transform_anim_key_count;
    uint16_t    transform_anim_length_frames;
    uint16_t    transform_anim_start_frame;
    uint8_t     transform_anim_fps;
    uint8_t     transform_anim_loop;
    uint8_t     has_camera;
    uint8_t     camera_primary;
    fix16_t     camera_fov;
    fix16_t     camera_near;
    fix16_t     camera_far;
    uint8_t     camera_background_texture_index;
    int16_t     camera_shot_trigger_index;
    int16_t     camera_shot_priority;
    int16_t     collider_shape;
    fix16_t     collider_center[3];
    fix16_t     collider_half_extents[3];
    fix16_t     collider_radius;
    fix16_t     collider_capsule_height;
    uint8_t     collider_is_trigger;
    uint8_t     collider_convex;
    uint16_t    collider_mesh_index;
    uint8_t     collider_camera_shot_trigger;
    int16_t     collider_camera_target_index;
    uint8_t     audio_clip_index;
    uint8_t     audio_enabled;
    uint8_t     audio_play_on_awake;
    uint8_t     audio_loop;
    uint8_t     audio_mute;
    uint8_t     audio_volume_q8;
    uint8_t     camera_distance_cull_enabled;
    fix16_t     camera_distance_cull_distance;
    uint8_t     camera_distance_cull_meshes;
    uint8_t     camera_distance_cull_skinned;
    uint8_t     camera_frustum_cull_enabled;
    fix16_t     camera_frustum_cull_margin;
    uint8_t     camera_frustum_cull_meshes;
    uint8_t     camera_frustum_cull_skinned;
} ps1_entity;

typedef struct ps1_rigid_anim_frame {
    fix16_t position[3];
    int16_t matrix[3][3]; /* 12.4 fixed linear transform, no mesh scale */
} ps1_rigid_anim_frame;

typedef struct ps1_transform_anim_key {
    uint16_t frame;
    fix16_t position[3];
    fix16_t rotation[3];
    fix16_t scale[3];
} ps1_transform_anim_key;

typedef struct ps1_script_binding {
    uint16_t entity_index;
    uint16_t module_index;
    uint8_t  field_count;
    fix16_t  fields[PS1_MAX_FIELD_VALUES];
} ps1_script_binding;

typedef struct ps1_light {
    uint8_t enabled; /* 0/1 */
    /* Direction in 12.4 fixed (ONE=4096). Points FROM surface TO light (like -forward). */
    int16_t dir_x, dir_y, dir_z;
    /* Light color (0-255) after intensity applied on the host. */
    uint8_t color_r, color_g, color_b;
    /* Ambient color (0-255). */
    uint8_t ambient_r, ambient_g, ambient_b;
} ps1_light;

typedef enum {
    PS1_UI_IMAGE = 1,
    PS1_UI_TEXT  = 2,
    PS1_UI_AUDIO_SPECTRUM = 3,
} ps1_ui_kind;

typedef struct ps1_ui_element {
    uint8_t kind;
    int16_t x, y, w, h;
    uint8_t color_r, color_g, color_b, color_a;
    uint8_t texture_index;
    uint8_t preserve_aspect;
    uint8_t alignment; /* 0=left, 1=center, 2=right */
    uint8_t font_size;
    uint16_t text_offset;
    uint16_t text_length;
    uint8_t spectrum_background[4];
    uint8_t spectrum_bars;
    uint8_t spectrum_gap;
    uint16_t spectrum_sensitivity_q8;
    int16_t rotation_degrees;
} ps1_ui_element;

typedef struct ps1_ui_glyph {
    uint32_t codepoint;
    uint8_t width;
    uint8_t height;
    uint8_t advance;
    uint16_t row_offset;
} ps1_ui_glyph;

typedef struct ps1_ui_button_rect {
    int16_t x, y, w, h;
    uint16_t action_offset;
    uint8_t action_count;
    uint8_t interactable;
} ps1_ui_button_rect;

typedef struct ps1_ui_button_action {
    uint16_t target_entity_index;
    uint8_t module_index;
    const char* method_name;
} ps1_ui_button_action;

typedef struct ps1_ui_button_group {
    uint8_t selected_index;
    uint8_t wrap_navigation;
    uint8_t gamepad_navigation;
    uint8_t gamepad_confirm;
    uint8_t confirm_button;
    uint16_t button_rect_offset;
    uint8_t button_count;
    int16_t cursor_offset_x;
    int16_t cursor_offset_y;
    int16_t cursor_w;
    int16_t cursor_h;
    uint8_t cursor_texture_index;
} ps1_ui_button_group;

typedef struct ps1_scene {
    const ps1_entity*         entities;
    unsigned int              entity_count;
    int                       camera_entity_index;
    const ps1_script_binding* bindings;
    unsigned int              binding_count;
    ps1_light                 light;
    uint8_t                   fog_enabled;
    uint8_t                   fog_r, fog_g, fog_b;
    fix16_t                   fog_start;
    fix16_t                   fog_end;
    const ps1_mesh*           meshes;
    unsigned int              mesh_count;
    const ps1_rigid_anim_frame* rigid_anim_frames;
    unsigned int              rigid_anim_frame_count;
    const ps1_transform_anim_key* transform_anim_keys;
    unsigned int              transform_anim_key_count;
    const ps1_ui_element*     ui_elements;
    unsigned int              ui_count;
    const char*               ui_text;
    unsigned int              ui_text_size;
    const ps1_ui_glyph*       ui_glyphs;
    unsigned int              ui_glyph_count;
    const uint16_t*           ui_glyph_rows;
    unsigned int              ui_glyph_row_count;
    const ps1_ui_button_group* ui_button_groups;
    unsigned int              ui_button_group_count;
    const ps1_ui_button_rect* ui_button_rects;
    unsigned int              ui_button_rect_count;
    const ps1_ui_button_action* ui_button_actions;
    unsigned int              ui_button_action_count;
    unsigned int              background_texture_index;
} ps1_scene;

extern const ps1_scene g_ps1_scene;
extern const ps1_mesh  g_ps1_meshes[];
extern const unsigned int g_ps1_mesh_count;

void ps1_scene_init(void);
/* Apply runtime parent transform deltas to flattened descendants. */
void ps1_scene_resolve_hierarchy(void);
void ps1_scene_begin_frame(void);
void ps1_scene_update_vertex_anims(fix16_t delta_time);
void ps1_scene_set_animator_float(unsigned int root_index, const char* name, fix16_t value);
void ps1_scene_set_animator_trigger(unsigned int root_index, const char* name);
void ps1_scene_set_animator_trigger_held(unsigned int root_index, const char* name);
void ps1_scene_release_animator_trigger(unsigned int root_index, const char* name);
ps1_entity*       ps1_scene_mutable_entity(unsigned int index);
const ps1_entity* ps1_scene_entity(unsigned int index);
const ps1_entity* ps1_scene_source_entity(unsigned int index);
unsigned int      ps1_scene_entity_count(void);
int               ps1_scene_camera_index(void);

const ps1_mesh*   ps1_scene_meshes(void);
unsigned int      ps1_scene_mesh_count(void);

#endif /* MIPSYNC_SCENE_H */
