#ifndef MIPSYNC_TEXTURES_H
#define MIPSYNC_TEXTURES_H

#include <stdint.h>

typedef struct {
    const uint32_t* tim;
    unsigned int    tim_word_count;
    uint8_t         is_background;
    uint8_t         has_cutout;
    /* Optional one-based descriptor whose CLUT is shared by this image. */
    uint16_t        clut_texture_index;
    /* One-based owner of a VRAM slot shared by mutually exclusive poses. */
    uint16_t        residency_slot;
    /* Zero preserves raw TIM storage; otherwise tim points to zero-run data.
     * tim_word_count remains the decoded size, excluding any stored padding. */
    unsigned int    encoded_byte_count;
} ps1_texture_desc;

extern const ps1_texture_desc g_ps1_textures[];
extern const unsigned int     g_ps1_texture_count;

/* Upload TIM textures to VRAM. Call once after GPU init. */
void ps1_textures_init(void);
/* Optional caller-owned, word-aligned scratch storage. Keep it alive while
 * encoded textures may be uploaded. No heap allocation or retained TIM
 * pointers are used. A null/empty buffer disables encoded texture uploads. */
void ps1_textures_set_unpack_buffer(uint32_t* buffer, unsigned int word_capacity);
void ps1_texture_load_to_vram(unsigned int texture_index);
void ps1_texture_ensure_resident(unsigned int texture_index);

/* 0 = none; 1..g_ps1_texture_count = tpage for POLY_FT* primitives. */
uint16_t ps1_texture_tpage(unsigned int texture_index);
uint16_t ps1_texture_clut(unsigned int texture_index);
uint16_t ps1_texture_width(unsigned int texture_index);
uint16_t ps1_texture_height(unsigned int texture_index);
uint16_t ps1_texture_vram_x(unsigned int texture_index);
uint16_t ps1_texture_vram_y(unsigned int texture_index);
uint8_t ps1_texture_u_offset(unsigned int texture_index);
uint8_t ps1_texture_v_offset(unsigned int texture_index);
uint8_t ps1_texture_is_background(unsigned int texture_index);
uint8_t ps1_texture_has_cutout(unsigned int texture_index);

#endif /* MIPSYNC_TEXTURES_H */
