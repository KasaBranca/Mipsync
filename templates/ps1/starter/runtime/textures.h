#ifndef MIPSYNC_TEXTURES_H
#define MIPSYNC_TEXTURES_H

#include <stdint.h>

typedef struct {
    const uint32_t* tim;
    unsigned int    tim_word_count;
    uint8_t         is_background;
} ps1_texture_desc;

extern const ps1_texture_desc g_ps1_textures[];
extern const unsigned int     g_ps1_texture_count;

/* Upload TIM textures to VRAM. Call once after GPU init. */
void ps1_textures_init(void);
void ps1_texture_load_to_vram(unsigned int texture_index);

/* 0 = none; 1..g_ps1_texture_count = tpage for POLY_FT* primitives. */
uint16_t ps1_texture_tpage(unsigned int texture_index);
uint16_t ps1_texture_width(unsigned int texture_index);
uint16_t ps1_texture_height(unsigned int texture_index);
uint16_t ps1_texture_vram_x(unsigned int texture_index);
uint16_t ps1_texture_vram_y(unsigned int texture_index);
uint8_t ps1_texture_u_offset(unsigned int texture_index);
uint8_t ps1_texture_v_offset(unsigned int texture_index);
uint8_t ps1_texture_is_background(unsigned int texture_index);

#endif /* MIPSYNC_TEXTURES_H */
