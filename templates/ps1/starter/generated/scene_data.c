/* Stub scene_data.c — replaced by Build PS1 export. */
#include "../runtime/scene.h"

static const ps1_entity k_ps1_entities[] = {
    { 1u, "Placeholder", {0,0,0}, {0,0,0}, {65536,65536,65536},
      PS1_MESH_CUBE, 0u, 65536, 80, 120, 200, 0, 256, 256, 0, 0, 1, 0, 0u, 0u, 0, 1, 1,
      3932160 /*60 deg*/, 6553 /*0.1*/, 6553600 /*100*/ },
};

static const ps1_script_binding k_ps1_bindings[] = {
    { 0u, 0u, 0u, { 0 } },
};

static const char k_ps1_ui_text[] = "";

static const ps1_ui_element k_ps1_ui_elements[] = {
};

static const ps1_ui_glyph k_ps1_ui_glyphs[] = {
};

static const uint16_t k_ps1_ui_glyph_rows[] = {
};

static const ps1_ui_button_group k_ps1_ui_button_groups[] = {
};

static const ps1_ui_button_rect k_ps1_ui_button_rects[] = {
};

const ps1_scene g_ps1_scene = {
    k_ps1_entities,
    1u,
    0,
    k_ps1_bindings,
    0u,
    { 0, 0, 0, 0, 255, 255, 255, 32, 32, 32 },
    0, 13, 13, 20,
    655360, 2621440,
    g_ps1_meshes,
    0u,
    k_ps1_ui_elements,
    0u,
    k_ps1_ui_text,
    0u,
    k_ps1_ui_glyphs,
    0u,
    k_ps1_ui_glyph_rows,
    0u,
    k_ps1_ui_button_groups,
    0u,
    k_ps1_ui_button_rects,
    0u,
    0u
};
