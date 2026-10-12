#include "boot_brand.h"
#include "boot_brand_policy.h"
#include "boot_brand_decode.h"
#include "boot_brand_timing.h"
#include "boot_brand_image.h"
#include <psxgpu.h>
#include <stdio.h>

/* Documented 128-byte SYSTEM.CNF command-line buffer, not kernel code or
 * scratchpad. LoadExec replaces application RAM but preserves this buffer.
 * https://psx-spx.consoledev.net/ps1/kernelbios/memory-map/ */
#define BOOT_ARGUMENT_BUFFER ((volatile uint8_t*)0x80000180u)

void ps1_boot_brand_prepare_scene_load(void) {
    ps1_boot_handoff_stamp(BOOT_ARGUMENT_BUFFER);
}

void ps1_boot_brand_cancel_scene_load(void) {
    ps1_boot_handoff_clear(BOOT_ARGUMENT_BUFFER);
}

void ps1_boot_brand_show(void) {
    DISPENV display[2];
    DRAWENV draw[2];
    DR_TPAGE page;
    SPRT sprite;
    ps1_boot_brand_decoder decoder = {0, 0, 0};
    /* GPU uploads read 32-bit words, including for a 16-bit texture. */
    uint32_t row_words[MIPSYNC_BOOT_BRAND_WIDTH / 2];
    uint16_t* row = (uint16_t*)row_words;
    RECT upload = {512, 0, MIPSYNC_BOOT_BRAND_WIDTH, 1};
    unsigned int y;
    int fields, field;
    if (ps1_boot_handoff_consume(BOOT_ARGUMENT_BUFFER)) {
        printf("MIPSYNC_BOOT_BRAND scene handoff\n");
        return;
    }
    /* Always show application branding after the BIOS/loader has handed off.
       The disc's standard BIOS logo and region data remain untouched. */
    printf("MIPSYNC_BOOT_BRAND application logo\n");
    ResetGraph(0);
    SetDispMask(0);
    for (field = 0; field < 2; ++field) {
        SetDefDispEnv(&display[field], 0, field * 240, 320, 240);
        SetDefDrawEnv(&draw[field], 0, field * 240, 320, 240);
        draw[field].isbg = 1;
        draw[field].dtd = 0;
        setRGB0(&draw[field], 0, 0, 0);
        PutDrawEnv(&draw[field]);
    }
    PutDispEnv(&display[0]);
    DrawSync(0);
    for (y = 0; y < MIPSYNC_BOOT_BRAND_HEIGHT; ++y) {
        ps1_boot_brand_decode_row(&decoder, k_mipsync_boot_brand_runs,
            sizeof(k_mipsync_boot_brand_runs) / sizeof(k_mipsync_boot_brand_runs[0]),
            row, MIPSYNC_BOOT_BRAND_WIDTH);
        upload.y = (short)y;
        LoadImage(&upload, row_words);
        DrawSync(0);
    }
    setDrawTPage(&page, 0, 0, getTPage(2, 0, 512, 0));
    setSprt(&sprite);
    setShadeTex(&sprite, 0);
    setXY0(&sprite, (320 - MIPSYNC_BOOT_BRAND_WIDTH) / 2,
                   (240 - MIPSYNC_BOOT_BRAND_HEIGHT) / 2);
    setUV0(&sprite, 0, 0);
    setWH(&sprite, MIPSYNC_BOOT_BRAND_WIDTH, MIPSYNC_BOOT_BRAND_HEIGHT);
    sprite.clut = 0;
    VSync(0);
    SetDispMask(1);
    printf("MIPSYNC_BOOT_BRAND shown\n");
    fields = GetVideoMode() ? 75 : 90;
    for (field = 0; field < fields; ++field) {
        int back = (field + 1) & 1;
        int brightness = ps1_boot_brand_brightness(field, fields);
        setRGB0(&sprite, brightness, brightness, brightness);
        PutDrawEnv(&draw[back]);
        DrawPrim((const uint32_t*)&page);
        DrawPrim((const uint32_t*)&sprite);
        DrawSync(0);
        VSync(0);
        PutDispEnv(&display[back]);
    }
    SetDispMask(0);
    /* Scene initialization resets the GPU and replaces this temporary VRAM
     * upload. No heap, per-frame work, texture slot or scene entity is kept. */
}
