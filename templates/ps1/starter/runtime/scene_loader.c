#include "scene_loader.h"
#include "scene.h"
#include "audio.h"
#include "host.h"
#include "boot_brand.h"
#include "../generated/scene_catalog.h"
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxcd.h>
#include <stdio.h>
#include <string.h>

static int s_pending_scene = -1;

static int path_equal(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca == '\\') ca = '/';
        if (cb == '\\') cb = '/';
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
    }
    return *a == *b;
}

int ps1_scene_request_load_index(int index) {
    if (index < 0 || index >= MIPSYNC_BUILD_SCENE_COUNT) {
        host_log("Scene is not in Scenes In Build");
        return 0;
    }
    s_pending_scene = index;
    return 1;
}

int ps1_scene_request_load_path(const char* path) {
    int i;
    for (i = 0; i < MIPSYNC_BUILD_SCENE_COUNT; ++i) {
        if (path_equal(path, k_mipsync_scene_paths[i]))
            return ps1_scene_request_load_index(i);
    }
    host_log("Scene is not in Scenes In Build");
    return 0;
}

void ps1_scene_process_load(void) {
    int index = s_pending_scene;
    unsigned int i;
    if (index < 0) return;
    s_pending_scene = -1;
    printf("MIPSYNC_SCENE_LOAD %d %s\n", index, k_mipsync_scene_paths[index]);
    for (i = 0; i < g_ps1_scene.entity_count; ++i)
        ps1_audio_stop(i);
    /* BIOS LoadExec replaces the current scene's executable in RAM. Quiesce
     * all SDK/BIOS drivers first; the next main() installs fresh handlers. */
    CdControlB(CdlPause, 0, 0);
    DrawSync(0);
    StopPAD();
    StopCallback();
    /* The SDK CD driver and BIOS file driver use separate interrupt/event
     * handlers. Restore the BIOS driver before LoadExec does CD file I/O.
     * StopCallback also leaves the CPU in a critical section; _96_init
     * reinstalls the CD IRQ/DMA handlers and enables interrupts again. */
    _96_remove();
    _96_init();
    ps1_boot_brand_prepare_scene_load();
    LoadExec(k_mipsync_scene_executables[index], 0, 0);
    /* A successful load never returns. Restore the old scene on failure. */
    ps1_boot_brand_cancel_scene_load();
    RestartCallback();
    StartPAD();
    host_log("Could not load scene from CD");
    printf("MIPSYNC_SCENE_LOAD_FAILED %d\n", index);
}
