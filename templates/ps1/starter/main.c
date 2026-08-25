/*

 * Mipsync Engine — PS1 runtime entry point.

 *

 * Loads the exported startup scene (generated/scene_data.c), binds Mipsync

 * scripts to scene entities, runs Awake/Start/Update, renders mesh entities

 * with the GTE, and routes Input/Physics host calls to the PSX pad.

 */



#include <stdint.h>

#include <psxgpu.h>

#include <psxapi.h>



#include "runtime/fixedp.h"

#include "runtime/host.h"

#include "runtime/input.h"

#include "runtime/physics.h"
#include "runtime/audio.h"

#include "runtime/render.h"
#include "runtime/ui.h"

#include "runtime/scene.h"


#include "runtime/textures.h"

#include "runtime/scripts_data.h"

#include "runtime/vm.h"



#define SCREEN_W 320

#define SCREEN_H 240

/* Must match render.c OT_LEN. */
#define OT_LEN   2048

/* On-screen debug text is very slow on PS1 — off by default. */
#ifndef PS1_DRAW_DEBUG_HUD
#define PS1_DRAW_DEBUG_HUD 0
#endif

/* Primitive packet buffer per frame.
 * Keep this conservative: PS1 has only 2 MiB RAM, and generated mesh/animation
 * data also lives there. The renderer clips safely when this fills. */
#define PACKET_LEN 73728

#define MAX_MODULES   MIPSYNC_PS1_MODULE_CAP

#define MAX_INSTANCES MIPSYNC_PS1_INSTANCE_CAP



static DISPENV disp[2];

static DRAWENV draw[2];

static uint32_t s_ot[2][OT_LEN];

static char     s_packet[2][PACKET_LEN];

static char*    s_nextpri;

static int      db;
static unsigned int s_loaded_background_index;



static mbc_module       g_modules[MAX_MODULES];

static int              g_module_count;

static script_instance  g_instances[MAX_INSTANCES];

static const mbc_method* g_update[MAX_INSTANCES];

static int              g_start_pending[MAX_INSTANCES];

static int              g_instance_count;

static vm_state         g_vm;

void mipsync_ui_invoke(uint16_t entity_index, uint8_t module_index, const char* method_name) {
    int i;
    mbc_module* module;
    const mbc_method* method;
    if (!method_name || module_index >= (uint8_t)g_module_count)
        return;
    module = &g_modules[module_index];
    method = mbc_find_method(module, method_name);
    if (!method)
        return;
    for (i = 0; i < g_instance_count; ++i) {
        script_instance* instance = &g_instances[i];
        if (instance->entity_index == entity_index && instance->module == module) {
            vm_run_method(&g_vm, module, instance, method);
            return;
        }
    }
}



static void SetupDisplay(void) {

    ResetGraph(0);

    SetDefDispEnv(&disp[0], 0, 0,        SCREEN_W, SCREEN_H);

    SetDefDispEnv(&disp[1], 0, SCREEN_H, SCREEN_W, SCREEN_H);

    SetDefDrawEnv(&draw[0], 0, SCREEN_H, SCREEN_W, SCREEN_H);

    SetDefDrawEnv(&draw[1], 0, 0,        SCREEN_W, SCREEN_H);



    /* Match the clear color to distance fog so geometry disappears into the
     * scene background instead of exposing a differently colored horizon. */
    if (g_ps1_scene.fog_enabled) {
        setRGB0(&draw[0], g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
        setRGB0(&draw[1], g_ps1_scene.fog_r, g_ps1_scene.fog_g, g_ps1_scene.fog_b);
    } else {
        setRGB0(&draw[0], 48, 50, 54);
        setRGB0(&draw[1], 48, 50, 54);
    }
    draw[0].isbg = 1;
    draw[0].dtd = 0;
    draw[1].isbg = 1;
    draw[1].dtd = 0;



    db = 0;
    s_loaded_background_index = 0;

    ClearOTagR(s_ot[0], OT_LEN);

    ClearOTagR(s_ot[1], OT_LEN);

    s_nextpri = s_packet[0];

    PutDispEnv(&disp[db]);

    PutDrawEnv(&draw[db]);

    SetDispMask(1);

}



static void PresentFrame(void) {
    unsigned int bg_index = g_ps1_scene.background_texture_index;
    const int cam_idx = ps1_scene_camera_index();
    const ps1_entity* cam = cam_idx >= 0 ? ps1_scene_entity((unsigned int)cam_idx) : 0;
    if (cam && cam->has_camera && cam->camera_background_texture_index != 0)
        bg_index = cam->camera_background_texture_index;
    const int has_background =
        bg_index != 0 &&
        ps1_texture_width(bg_index) > 0 &&
        ps1_texture_height(bg_index) > 0;

    DrawSync(0);

    /* Pre-rendered camera backgrounds share/reuse VRAM slots. Switching the
     * active camera changes the descriptor immediately, but the corresponding
     * TIM must be uploaded before the already-built OT is submitted. Do this
     * only on an actual background change; no per-frame framebuffer copy. */
    if (has_background && s_loaded_background_index != bg_index) {
        ps1_texture_load_to_vram(bg_index);
        s_loaded_background_index = bg_index;
    } else if (!has_background) {
        s_loaded_background_index = 0;
    }

    VSync(0);

    db ^= 1;

    ClearOTagR(s_ot[db], OT_LEN);

    PutDispEnv(&disp[db]);

    if (has_background) {
        draw[db].isbg = 0;
    } else {
        draw[db].isbg = 1;
    }

    PutDrawEnv(&draw[db]);

    SetDispMask(1);

    s_nextpri = s_packet[db];

    DrawOTag(s_ot[1 - db] + OT_LEN - 1);

}



static void DecodeModules(void) {

    g_module_count = 0;

    for (unsigned int i = 0; i < g_mipsync_script_count && g_module_count < MAX_MODULES; ++i) {

        const mipsync_script_blob* blob = &g_mipsync_scripts[i];

        if (!blob->data || !blob->size) continue;

        if (mbc_decode(blob->data, blob->size, &g_modules[g_module_count]))

            ++g_module_count;

    }

}



static mbc_module* FindModule(unsigned int index) {

    if (index >= (unsigned int)g_module_count) return 0;

    return &g_modules[index];

}



static void BindSceneScripts(void) {

    g_instance_count = 0;

    for (unsigned int i = 0; i < g_ps1_scene.binding_count && g_instance_count < MAX_INSTANCES; ++i) {

        const ps1_script_binding* binding = &g_ps1_scene.bindings[i];

        mbc_module* mod = FindModule(binding->module_index);

        if (!mod) continue;



        script_instance* inst = &g_instances[g_instance_count];

        inst->module = mod;

        inst->entity_index = binding->entity_index;

        for (int f = 0; f < VM_FIELD_CAP; ++f) inst->fields[f] = 0;
        for (int f = 0; f < VM_FIELD_CAP; ++f) {
            inst->runtime_fields[f].tag = HOST_VAL_NUMBER;
            inst->runtime_fields[f].ival = 0;
        }
        inst->array_count = 0;
        for (int c = 0; c < VM_COROUTINE_CAP; ++c) inst->coroutines[c].active = 0;

        for (uint8_t f = 0; f < binding->field_count && f < VM_FIELD_CAP; ++f)

        {
            inst->fields[f] = binding->fields[f];
            inst->runtime_fields[f].ival = binding->fields[f];
        }



        const mbc_method* awake = mbc_find_method(mod, "Awake");

        if (awake) vm_run_method(&g_vm, mod, inst, awake);



        g_update[g_instance_count] = mbc_find_method(mod, "Update");

        g_start_pending[g_instance_count] = mbc_find_method(mod, "Start") ? 1 : 0;

        ++g_instance_count;

    }

}



static void RunPendingStarts(void) {

    for (int i = 0; i < g_instance_count; ++i) {

        if (!g_start_pending[i]) continue;

        const mbc_method* start = mbc_find_method(g_instances[i].module, "Start");

        if (start) vm_run_method(&g_vm, g_instances[i].module, &g_instances[i], start);

        g_start_pending[i] = 0;

    }

}



static void RunUpdates(fix16_t delta_time) {

    for (int i = 0; i < g_instance_count; ++i) {

        if (g_update[i])

            vm_run_method(&g_vm, g_instances[i].module, &g_instances[i], g_update[i]);

        vm_resume_coroutines(&g_vm, g_instances[i].module, &g_instances[i], delta_time);

    }

}



int main(void) {

    uint32_t frame = 0;
    int last_vblank;

    SetupDisplay();

    host_init();
    ps1_input_init();

#if PS1_DRAW_DEBUG_HUD
    FntLoad(960, 0);
    FntOpen(8, 196, 304, 40, 0, 256);
#endif

    ps1_render_init(SCREEN_W, SCREEN_H);
    ps1_textures_init();

    ps1_scene_init();

    /* Load initial background texture to VRAM to prevent race conditions on the first frame */
    {
        unsigned int init_bg_index = g_ps1_scene.background_texture_index;
        const int cam_idx = ps1_scene_camera_index();
        const ps1_entity* cam = cam_idx >= 0 ? ps1_scene_entity((unsigned int)cam_idx) : 0;
        if (cam && cam->has_camera && cam->camera_background_texture_index != 0)
            init_bg_index = cam->camera_background_texture_index;
        if (init_bg_index != 0 && ps1_texture_width(init_bg_index) > 0) {
            ps1_texture_load_to_vram(init_bg_index);
            s_loaded_background_index = init_bg_index;
        }
    }

    /* Present the scene before blocking on CD -> SPU audio uploads. Large
       clips no longer leave the display black during startup. */
    for (int startup_frame = 0; startup_frame < 2; ++startup_frame) {
        ps1_scene_begin_frame();
        ps1_render_frame(s_ot[db], &s_nextpri, s_packet[db] + PACKET_LEN);
        PresentFrame();
    }

    ps1_audio_init();
    ps1_audio_begin_scene();

    host_log("MIPSYNC PS1 ENGINE");

    DecodeModules();

    BindSceneScripts();

    RunPendingStarts();
    ps1_scene_resolve_hierarchy();
    last_vblank = VSync(-1);



    if (g_instance_count == 0)

        host_log("No scene scripts — add Mipsync scripts to entities and Build PS1.");



    while (1) {
        int current_vblank = VSync(-1);
        int elapsed_vblanks = current_vblank - last_vblank;
        fix16_t delta_time;
        if (elapsed_vblanks < 1) elapsed_vblanks = 1;
        if (elapsed_vblanks > 4) elapsed_vblanks = 4;
        last_vblank = current_vblank;
        delta_time = fix16_div(FIX16_FROM_INT(elapsed_vblanks), FIX16_FROM_INT(60));

        ps1_input_poll();
        ps1_ui_update();

        host_set_delta_q16_16(delta_time);

        RunUpdates(delta_time);
        ps1_audio_update();
        ps1_scene_update_vertex_anims(delta_time);
        ps1_scene_resolve_hierarchy();
        ps1_scene_begin_frame();

        ps1_render_frame(s_ot[db], &s_nextpri, s_packet[db] + PACKET_LEN);

#if PS1_DRAW_DEBUG_HUD
        host_render(frame, (uint32_t)g_instance_count, (uint32_t)g_instance_count);
        FntFlush(-1);
#endif

        PresentFrame();

        ++frame;

    }

    return 0;

}
