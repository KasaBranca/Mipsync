#include "textures.h"
#include <psxgpu.h>
#include <stdint.h>

static uint16_t s_tpages[256];
static uint16_t s_widths[256];
static uint16_t s_heights[256];
static uint16_t s_vram_x[256];
static uint16_t s_vram_y[256];

static void ps1_texture_flush_gpu_cache(void) {
    DR_TPAGE flush;
    setlen(&flush, 1);
    flush.code[0] = 0x01000000u;
    DrawPrim((const uint32_t*)&flush);
    DrawSync(0);
}

static int ps1_texture_get_tim(unsigned int texture_index, TIM_IMAGE* tim) {
    const ps1_texture_desc* t;
    if (texture_index == 0 || texture_index > g_ps1_texture_count || texture_index > 256u)
        return 0;
    t = &g_ps1_textures[texture_index - 1u];
    if (!t->tim || t->tim_word_count < 5)
        return 0;
    if (GetTimInfo(t->tim, tim) != 0)
        return 0;
    if (!tim->prect || !tim->paddr)
        return 0;
    return 1;
}

void ps1_texture_load_to_vram(unsigned int texture_index) {
    TIM_IMAGE tim;
    if (!ps1_texture_get_tim(texture_index, &tim))
        return;
    if (tim.mode & 0x8)
        LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    DrawSync(0);
    ps1_texture_flush_gpu_cache();
    s_widths[texture_index - 1u] = (uint16_t)tim.prect->w;
    s_heights[texture_index - 1u] = (uint16_t)tim.prect->h;
    s_vram_x[texture_index - 1u] = (uint16_t)tim.prect->x;
    s_vram_y[texture_index - 1u] = (uint16_t)tim.prect->y;
}

void ps1_textures_init(void) {
    unsigned int i;

    for (i = 0; i < (unsigned int)(sizeof(s_tpages) / sizeof(s_tpages[0])); ++i) {
        s_tpages[i] = 0;
        s_widths[i] = 0;
        s_heights[i] = 0;
        s_vram_x[i] = 0;
        s_vram_y[i] = 0;
    }

    for (i = 0; i < g_ps1_texture_count && i < 256u; ++i) {
        const ps1_texture_desc* t = &g_ps1_textures[i];
        TIM_IMAGE tim;

        if (!ps1_texture_get_tim(i + 1u, &tim))
            continue;

        if (tim.mode & 0x8)
            LoadImage(tim.crect, tim.caddr);
        LoadImage(tim.prect, tim.paddr);

        {
            const int tp = (int)(tim.mode & 3u);
            s_tpages[i] = getTPage(tp, 0, tim.prect->x, tim.prect->y);
            if (s_tpages[i] == 0)
                s_tpages[i] = getTPage(2, 0, tim.prect->x, tim.prect->y);
            s_widths[i] = (uint16_t)tim.prect->w;
            s_heights[i] = (uint16_t)tim.prect->h;
            s_vram_x[i] = (uint16_t)tim.prect->x;
            s_vram_y[i] = (uint16_t)tim.prect->y;
        }
    }

    DrawSync(0);
    ps1_texture_flush_gpu_cache();
}

uint16_t ps1_texture_tpage(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    return s_tpages[texture_index - 1];
}

uint16_t ps1_texture_width(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    return s_widths[texture_index - 1];
}

uint16_t ps1_texture_height(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    return s_heights[texture_index - 1];
}

uint16_t ps1_texture_vram_x(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    return s_vram_x[texture_index - 1];
}

uint16_t ps1_texture_vram_y(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    return s_vram_y[texture_index - 1];
}

uint8_t ps1_texture_u_offset(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    /* getTPage() anchors 16-bpp pages on 64-texel X boundaries. */
    return (uint8_t)(s_vram_x[texture_index - 1] & 63u);
}

uint8_t ps1_texture_v_offset(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    /* Texture page Y is 0 or 256; textures packed lower inside the page need
       this offset or they sample whatever was uploaded at the page origin. */
    return (uint8_t)(s_vram_y[texture_index - 1] & 255u);
}

uint8_t ps1_texture_is_background(unsigned int texture_index) {
    if (texture_index == 0 || texture_index > g_ps1_texture_count)
        return 0;
    return g_ps1_textures[texture_index - 1].is_background;
}
