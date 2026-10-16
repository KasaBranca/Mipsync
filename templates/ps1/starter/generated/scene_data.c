/* Stub scene_data.c — replaced by Build PS1 export.
 * Named fields keep the bundled starter compatible with runtime ABI additions. */
#include "../runtime/scene.h"

static const ps1_entity k_ps1_entities[] = {
    {
        .id = 1u, .name = "Placeholder", .parent_entity_index = -1,
        .position = {0, 0, 5 * FIX16_ONE}, .scale = {FIX16_ONE, FIX16_ONE, FIX16_ONE},
        .mesh = PS1_MESH_CUBE, .mesh_size = FIX16_ONE, .mesh_enabled = 1,
        .color_r = 80, .color_g = 120, .color_b = 200,
        .texture_tiling_u_q8 = 256, .texture_tiling_v_q8 = 256,
        .rigid_root_entity_index = -1, .collider_shape = -1,
        .camera_shot_trigger_index = -1, .collider_camera_target_index = -1
    },
    {
        .id = 2u, .name = "Camera", .parent_entity_index = -1,
        .scale = {FIX16_ONE, FIX16_ONE, FIX16_ONE},
        .rigid_root_entity_index = -1, .collider_shape = -1,
        .has_camera = 1, .camera_primary = 1, .camera_fov = 60 * FIX16_ONE,
        .camera_near = FIX16_ONE / 10, .camera_far = 100 * FIX16_ONE,
        .camera_shot_trigger_index = -1, .collider_camera_target_index = -1
    }
};

const ps1_scene g_ps1_scene = {
    .entities = k_ps1_entities,
    .entity_count = 2u,
    .camera_entity_index = 1,
    .light = {.color_r = 255, .color_g = 255, .color_b = 255,
              .ambient_r = 32, .ambient_g = 32, .ambient_b = 32},
    .fog_r = 13, .fog_g = 13, .fog_b = 20,
    .fog_start = 10 * FIX16_ONE, .fog_end = 40 * FIX16_ONE,
    .meshes = g_ps1_meshes
};
