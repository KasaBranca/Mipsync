#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psxgpu.h>
#include "../templates/ps1/starter/runtime/textures.h"

static const uint32_t images[6][5] = {{1}, {2}, {3}, {4}, {5}, {6}};
const ps1_texture_desc g_ps1_textures[] = {
    {images[0], 5, 0, 0, 2, 0}, /* Forward reference. */
    {images[1], 5, 0, 0, 0, 0}, /* Palette owner. */
    {images[2], 5, 0, 0, 1, 1}, /* Chained reference and shared residency. */
    {images[3], 5, 0, 0, 5, 0}, /* Cycle must terminate. */
    {images[4], 5, 0, 0, 4, 0},
    {images[5], 5, 0, 0, 99, 0} /* Invalid owner must be rejected. */
};
const unsigned int g_ps1_texture_count = 6;
static RECT image_rect = {528, 32, 16, 64};
static RECT palette_rect = {512, 480, 256, 1};
static uint32_t pixels[512];
static unsigned uploads;

int GetTimInfo(const uint32_t* tim, TIM_IMAGE* out) {
    memset(out, 0, sizeof(*out));
    out->mode = tim[0] == 2 ? 9 : 1;
    out->prect = &image_rect;
    out->paddr = pixels;
    if (tim[0] == 2) { out->crect = &palette_rect; out->caddr = pixels; }
    return 0;
}
void ps1_gpu_load_image(const RECT* rect, const uint32_t* data) {
    (void)rect; (void)data; ++uploads;
}
int DrawSync(int mode) { (void)mode; return 0; }
void DrawPrim(const uint32_t* packet) { (void)packet; }
static void check(int condition, const char* message) {
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
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
    puts("PS1 palette and residency checks passed.");
    return 0;
}
