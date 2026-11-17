#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <psxgpu.h>
#include "../templates/ps1/starter/runtime/textures.h"

static const uint32_t images[6][5] = {{1}, {2}, {3}, {4}, {5}, {6}};
#ifndef MIPSYNC_TEST_TEXTURE_COUNT
#define MIPSYNC_TEST_TEXTURE_COUNT 257u
#endif
/* Pad the fixtures to the runtime boundary without designated initializers,
 * so this test also compiles with the Windows editor's C compiler. */
#define EMPTY_TEXTURES_2 {0}, {0}
#define EMPTY_TEXTURES_4 EMPTY_TEXTURES_2, EMPTY_TEXTURES_2
#define EMPTY_TEXTURES_8 EMPTY_TEXTURES_4, EMPTY_TEXTURES_4
#define EMPTY_TEXTURES_16 EMPTY_TEXTURES_8, EMPTY_TEXTURES_8
#define EMPTY_TEXTURES_32 EMPTY_TEXTURES_16, EMPTY_TEXTURES_16
#define EMPTY_TEXTURES_64 EMPTY_TEXTURES_32, EMPTY_TEXTURES_32
const ps1_texture_desc g_ps1_textures[257] = {
    {images[0], 5, 0, 0, 2, 0}, /* Forward reference. */
    {images[1], 5, 0, 0, 0, 0}, /* Palette owner. */
    {images[2], 5, 0, 0, 1, 1}, /* Chained reference and shared residency. */
    {images[3], 5, 0, 0, 5, 0}, /* Cycle must terminate. */
    {images[4], 5, 0, 0, 4, 0},
    {images[5], 5, 0, 0, 99, 0}, /* Invalid owner must be rejected. */
    {images[1], 5, 0, 0, 0, 257}, /* Invalid slot with both image and palette data. */
    {images[0], 5, 0, 0, 2, 256}, /* Alias the last valid slot. */
    /* Eight fixtures plus 247 empty entries put the next descriptor at 256. */
    EMPTY_TEXTURES_64, EMPTY_TEXTURES_64, EMPTY_TEXTURES_64,
    EMPTY_TEXTURES_32, EMPTY_TEXTURES_16, EMPTY_TEXTURES_4, EMPTY_TEXTURES_2, {0},
    {images[0], 5, 1, 1, 2, 0}, /* Slot zero falls back to descriptor 256. */
    {images[0], 5, 1, 1, 2, 0} /* Descriptor exists beyond the runtime limit. */
};
const unsigned int g_ps1_texture_count = MIPSYNC_TEST_TEXTURE_COUNT;
static RECT image_rect = {528, 32, 16, 64};
static RECT palette_rect = {512, 480, 256, 1};
static uint32_t pixels[512];
static unsigned uploads;
static unsigned gpu_commands;

int GetTimInfo(const uint32_t* tim, TIM_IMAGE* out) {
    memset(out, 0, sizeof(*out));
    out->mode = tim[0] == 2 ? 9 : 1;
    out->prect = &image_rect;
    out->paddr = pixels;
    if (tim[0] == 2) { out->crect = &palette_rect; out->caddr = pixels; }
    return 0;
}
void ps1_gpu_load_image(const RECT* rect, const uint32_t* data) {
    (void)rect; (void)data; ++uploads; ++gpu_commands;
}
int DrawSync(int mode) { (void)mode; ++gpu_commands; return 0; }
void DrawPrim(const uint32_t* packet) { (void)packet; ++gpu_commands; }
static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}
static void check_invalid_index(unsigned int index) {
    const unsigned initial_gpu_commands = gpu_commands;
    check(ps1_texture_tpage(index) == 0, "Invalid texture must not expose a tpage");
    check(ps1_texture_clut(index) == 0, "Invalid texture must not expose a CLUT");
    check(ps1_texture_width(index) == 0 && ps1_texture_height(index) == 0,
          "Invalid texture must not expose dimensions");
    check(ps1_texture_vram_x(index) == 0 && ps1_texture_vram_y(index) == 0,
          "Invalid texture must not expose VRAM coordinates");
    check(ps1_texture_u_offset(index) == 0 && ps1_texture_v_offset(index) == 0,
          "Invalid texture must not expose UV offsets");
    check(ps1_texture_is_background(index) == 0 && ps1_texture_has_cutout(index) == 0,
          "Invalid texture must not expose descriptor flags");
    ps1_texture_load_to_vram(index);
    ps1_texture_ensure_resident(index);
    check(gpu_commands == initial_gpu_commands,
          "Invalid texture must not upload, synchronize or flush the GPU");
}

int main(void) {
    ps1_textures_init();
    const uint16_t expected = getClut(palette_rect.x, palette_rect.y);
    check(ps1_texture_clut(1) == expected, "Forward palette reference must resolve at initialization");
    check(ps1_texture_clut(2) == expected, "Palette owner must retain its palette");
    check(ps1_texture_clut(3) == expected, "Chained palette reference must resolve");
    check(ps1_texture_clut(4) == 0 && ps1_texture_clut(5) == 0, "Cyclic references must terminate");
    check(ps1_texture_clut(6) == 0, "Out-of-range palette reference must be rejected");
    check(ps1_texture_width(1) == 32 && ps1_texture_u_offset(1) == 32, "8-bit widths and offsets use pixels");
    const unsigned initial = uploads;
    ps1_texture_ensure_resident(3);
    check(uploads == initial + 1, "Shared slot must upload the requested image");
    check(ps1_texture_clut(3) == expected, "Reload must preserve shared palette resolution");
    ps1_texture_ensure_resident(3);
    check(uploads == initial + 1, "Resident image must not upload again");
    ps1_texture_ensure_resident(1);
    check(uploads == initial + 2, "Switching shared slot back must upload its owner");
    check_invalid_index(0);
    check_invalid_index(g_ps1_texture_count + 1u);
    check_invalid_index(257);
    check_invalid_index(UINT_MAX);

    if (g_ps1_texture_count >= 7u) {
        const unsigned before_invalid_slot = gpu_commands;
        ps1_texture_load_to_vram(7);
        ps1_texture_ensure_resident(7);
        check(gpu_commands == before_invalid_slot,
              "Invalid residency slot must be rejected before palette or image uploads");
        check(ps1_texture_width(7) == 0,
              "Initialization must leave invalid-slot metadata unset");
    }
    if (g_ps1_texture_count >= 256u) {
        check(ps1_texture_tpage(256) == ps1_texture_tpage(1) &&
              ps1_texture_clut(256) == expected,
              "Last valid descriptor retains its tpage and shared palette");
        check(ps1_texture_width(256) == 32 && ps1_texture_height(256) == 64 &&
              ps1_texture_vram_x(256) == 528 && ps1_texture_vram_y(256) == 32 &&
              ps1_texture_u_offset(256) == 32 && ps1_texture_v_offset(256) == 32,
              "Last valid descriptor retains dimensions and offsets");
        check(ps1_texture_is_background(256) && ps1_texture_has_cutout(256),
              "Last valid descriptor retains its flags");
        const unsigned boundary_uploads = uploads;
        ps1_texture_ensure_resident(256);
        check(uploads == boundary_uploads,
              "Slot-zero fallback must retain the initialized last descriptor");
        ps1_texture_ensure_resident(8);
        check(uploads == boundary_uploads + 1,
              "Alias can upload into the last valid residency slot");
        ps1_texture_ensure_resident(8);
        check(uploads == boundary_uploads + 1,
              "Last-slot alias remains resident without another upload");
        ps1_texture_ensure_resident(256);
        check(uploads == boundary_uploads + 2,
              "Slot-zero fallback can restore the last descriptor after an alias");
        ps1_texture_load_to_vram(256);
        check(uploads == boundary_uploads + 3,
              "Direct loader accepts the last valid descriptor and slot");
    }
    puts("PS1 palette, residency and texture bounds checks passed.");
    return 0;
}
