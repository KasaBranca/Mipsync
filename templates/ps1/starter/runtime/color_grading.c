#include "color_grading.h"
#include "render.h"
#include "gpu_transfer.h"
#include <psxgpu.h>

void ps1_render_submit(uint32_t* ot, int framebuffer_x, int framebuffer_y, int width, int height, const uint16_t* lut) {
    static uint16_t pixels[320 * 16] __attribute__((aligned(4))); /* 10 KiB staging. */
    uint32_t ui_tag;
    int x, y;
    if (!lut) { DrawOTag(ot + PS1_RENDER_OT_LEN - 1); return; }
    /* UI is exclusively in bucket 1; world polygons are in buckets 2+. */
    ui_tag = ot[1]; ot[1] = 0x00ffffffu;
    DrawOTag(ot + PS1_RENDER_OT_LEN - 1);
    DrawSync(0);
    for (y = 0; y < height; y += 16) {
        for (x = 0; x < width; x += 320) {
            RECT rect;
            rect.x = framebuffer_x + x; rect.y = framebuffer_y + y;
            rect.w = width - x < 320 ? width - x : 320;
            rect.h = height - y < 16 ? height - y : 16;
            ps1_gpu_store_image(&rect, (uint32_t*)pixels);
            ps1_grade_pixels(pixels, (unsigned int)rect.w * rect.h, lut);
            LoadImage(&rect, (const uint32_t*)pixels); DrawSync(0);
        }
    }
    ot[1] = ui_tag;
    DrawOTag(ot + 1);
}
