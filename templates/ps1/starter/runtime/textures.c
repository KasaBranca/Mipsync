#include "textures.h"
#include "gpu_transfer.h"
#include <psxgpu.h>
#include <stdint.h>

#define PS1_TEXTURE_CAPACITY 256u

static uint16_t s_tpages[PS1_TEXTURE_CAPACITY];
static uint16_t s_cluts[PS1_TEXTURE_CAPACITY];
static uint16_t s_widths[PS1_TEXTURE_CAPACITY];
static uint16_t s_heights[PS1_TEXTURE_CAPACITY];
static uint16_t s_vram_x[PS1_TEXTURE_CAPACITY];
static uint16_t s_vram_y[PS1_TEXTURE_CAPACITY];
static uint8_t s_modes[PS1_TEXTURE_CAPACITY];
static uint16_t s_slot_texture[PS1_TEXTURE_CAPACITY];

static int ps1_texture_index_valid(unsigned int texture_index) {
    return texture_index != 0 && texture_index <= g_ps1_texture_count &&
        texture_index <= PS1_TEXTURE_CAPACITY;
}

static unsigned int ps1_texture_residency_slot(unsigned int texture_index) {
    unsigned int slot;
    if (!ps1_texture_index_valid(texture_index)) return 0;
    slot = g_ps1_textures[texture_index - 1u].residency_slot;
    if (!slot) slot = texture_index;
    return slot <= PS1_TEXTURE_CAPACITY ? slot : 0;
}

static uint16_t ps1_texture_pixel_width(const TIM_IMAGE* tim) {
    const unsigned int mode = (unsigned int)(tim->mode & 3u);
    const unsigned int pixels_per_word = mode == 0u ? 4u : (mode == 1u ? 2u : 1u);
    return (uint16_t)((unsigned int)tim->prect->w * pixels_per_word);
}

static void ps1_texture_flush_gpu_cache(void) {
    DR_TPAGE flush;
    setlen(&flush, 1);
    flush.code[0] = 0x01000000u;
    DrawPrim((const uint32_t*)&flush);
    DrawSync(0);
}

static int ps1_texture_get_tim(unsigned int texture_index, TIM_IMAGE* tim) {
    const ps1_texture_desc* t;
    if (!ps1_texture_index_valid(texture_index))
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

/* Resolve descriptor metadata rather than partially initialized s_cluts.
 * A shared palette owner may appear later in the generated table. */
static uint16_t ps1_texture_resolve_clut(unsigned int texture_index) {
    unsigned int remaining = g_ps1_texture_count < PS1_TEXTURE_CAPACITY
        ? g_ps1_texture_count : PS1_TEXTURE_CAPACITY;
    while (remaining-- && texture_index) {
        TIM_IMAGE tim;
        if (!ps1_texture_get_tim(texture_index, &tim)) return 0;
        if ((tim.mode & 0x8u) && tim.crect)
            return getClut(tim.crect->x, tim.crect->y);
        texture_index = g_ps1_textures[texture_index - 1u].clut_texture_index;
    }
    return 0;
}

void ps1_texture_load_to_vram(unsigned int texture_index) {
    TIM_IMAGE tim;
    const unsigned int slot = ps1_texture_residency_slot(texture_index);
    /* Reject invalid destinations before queuing any image or palette upload. */
    if (!slot || !ps1_texture_get_tim(texture_index, &tim))
        return;
    if (tim.mode & 0x8)
        ps1_gpu_load_image(tim.crect, tim.caddr);
    ps1_gpu_load_image(tim.prect, tim.paddr);
    DrawSync(0);
    ps1_texture_flush_gpu_cache();
    s_heights[texture_index - 1u] = (uint16_t)tim.prect->h;
    s_vram_x[texture_index - 1u] = (uint16_t)tim.prect->x;
    s_vram_y[texture_index - 1u] = (uint16_t)tim.prect->y;
    s_modes[texture_index - 1u] = (uint8_t)(tim.mode & 3u);
    s_widths[texture_index - 1u] = ps1_texture_pixel_width(&tim);
    s_cluts[texture_index - 1u] = ps1_texture_resolve_clut(texture_index);
    s_slot_texture[slot - 1u] = (uint16_t)texture_index;
}

void ps1_texture_ensure_resident(unsigned int texture_index) {
    const unsigned int slot = ps1_texture_residency_slot(texture_index);
    if (!slot) return;
    if (s_slot_texture[slot - 1u] != texture_index)
        ps1_texture_load_to_vram(texture_index);
}

void ps1_textures_init(void) {
    unsigned int i;

    for (i = 0; i < (unsigned int)(sizeof(s_tpages) / sizeof(s_tpages[0])); ++i) {
        s_tpages[i] = 0;
        s_cluts[i] = 0;
        s_widths[i] = 0;
        s_heights[i] = 0;
        s_vram_x[i] = 0;
        s_vram_y[i] = 0;
        s_modes[i] = 2;
        s_slot_texture[i] = 0;
    }

    for (i = 0; i < g_ps1_texture_count && i < PS1_TEXTURE_CAPACITY; ++i) {
        const unsigned int slot = ps1_texture_residency_slot(i + 1u);
        TIM_IMAGE tim;

        if (!slot || !ps1_texture_get_tim(i + 1u, &tim))
            continue;

        if (slot == i + 1u) {
            if (tim.mode & 0x8)
                ps1_gpu_load_image(tim.crect, tim.caddr);
            ps1_gpu_load_image(tim.prect, tim.paddr);
            s_slot_texture[i] = (uint16_t)(i + 1u);
        }

        {
            const int tp = (int)(tim.mode & 3u);
            s_tpages[i] = getTPage(tp, 0, tim.prect->x, tim.prect->y);
            if (s_tpages[i] == 0)
                s_tpages[i] = getTPage(2, 0, tim.prect->x, tim.prect->y);
            s_widths[i] = ps1_texture_pixel_width(&tim);
            s_heights[i] = (uint16_t)tim.prect->h;
            s_vram_x[i] = (uint16_t)tim.prect->x;
            s_vram_y[i] = (uint16_t)tim.prect->y;
            s_modes[i] = (uint8_t)tp;
            s_cluts[i] = ps1_texture_resolve_clut(i + 1u);
        }
    }

    DrawSync(0);
    ps1_texture_flush_gpu_cache();
}

uint16_t ps1_texture_tpage(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return s_tpages[texture_index - 1];
}

uint16_t ps1_texture_clut(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return s_cluts[texture_index - 1];
}

uint16_t ps1_texture_width(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return s_widths[texture_index - 1];
}

uint16_t ps1_texture_height(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return s_heights[texture_index - 1];
}

uint16_t ps1_texture_vram_x(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return s_vram_x[texture_index - 1];
}

uint16_t ps1_texture_vram_y(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return s_vram_y[texture_index - 1];
}

uint8_t ps1_texture_u_offset(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    /* VRAM X is measured in 16-bit words. One word stores four 4-bpp,
       two 8-bpp, or one 16-bpp texel inside a 64-word texture page. */
    return (uint8_t)((s_vram_x[texture_index - 1] & 63u) <<
        (s_modes[texture_index - 1] == 0 ? 2 :
         s_modes[texture_index - 1] == 1 ? 1 : 0));
}

uint8_t ps1_texture_v_offset(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    /* Texture page Y is 0 or 256; textures packed lower inside the page need
       this offset or they sample whatever was uploaded at the page origin. */
    return (uint8_t)(s_vram_y[texture_index - 1] & 255u);
}

uint8_t ps1_texture_is_background(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index))
        return 0;
    return g_ps1_textures[texture_index - 1].is_background;
}
uint8_t ps1_texture_has_cutout(unsigned int texture_index) {
    if (!ps1_texture_index_valid(texture_index)) return 0;
    return g_ps1_textures[texture_index - 1].has_cutout;
}
