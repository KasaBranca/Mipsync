#include "ui.h"
#include "scene.h"
#include "textures.h"
#include "audio.h"
#include "fixedp.h"
#include "input.h"

#include <psxgpu.h>

#define UI_OT_SLOT 1

static uint8_t s_spectrum_display[32];
static uint8_t s_button_group_selected[32];
static uint8_t s_button_groups_initialized = 0;

static uint8_t clamp_ui_uv(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

static const uint8_t* font_rows(char ch) {
    static const uint8_t glyphs[36][7] = {
        { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
        { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
        { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
        { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e }, /* 3 */
        { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
        { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e }, /* 5 */
        { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
        { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
        { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e }, /* 9 */
        { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
        { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
        { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
        { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, /* D */
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
        { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
        { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
        { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
        { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
        { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c }, /* J */
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
        { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* N */
        { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
        { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
        { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
        { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
        { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
        { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 }, /* W */
        { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
        { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 }, /* Y */
        { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
    };
    static const uint8_t excl[7] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
    static const uint8_t ques[7] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    static const uint8_t dash[7] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 };
    static const uint8_t dot[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c };
    static const uint8_t colon[7] = { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 };

    if (ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9')
        return glyphs[ch - '0'];
    if (ch >= 'A' && ch <= 'Z')
        return glyphs[10 + ch - 'A'];
    if (ch == '!') return excl;
    if (ch == '?') return ques;
    if (ch == '-' || ch == '_') return dash;
    if (ch == '.') return dot;
    if (ch == ':') return colon;
    return 0;
}

static uint32_t utf8_next(const char* text, unsigned int len, unsigned int* index) {
    uint8_t c0;
    uint32_t cp;
    int extra;
    int i;

    if (!text || !index || *index >= len)
        return 0;
    c0 = (uint8_t)text[(*index)++];
    if (c0 < 0x80u)
        return c0;

    cp = 0;
    extra = 0;
    if ((c0 & 0xe0u) == 0xc0u) {
        cp = c0 & 0x1fu;
        extra = 1;
    } else if ((c0 & 0xf0u) == 0xe0u) {
        cp = c0 & 0x0fu;
        extra = 2;
    } else if ((c0 & 0xf8u) == 0xf0u) {
        cp = c0 & 0x07u;
        extra = 3;
    } else {
        return '?';
    }

    if (*index + (unsigned int)extra > len)
        return '?';
    for (i = 0; i < extra; ++i) {
        uint8_t cx = (uint8_t)text[(*index)++];
        if ((cx & 0xc0u) != 0x80u)
            return '?';
        cp = (cp << 6) | (uint32_t)(cx & 0x3fu);
    }
    return cp;
}

static const ps1_ui_glyph* find_glyph(uint32_t codepoint) {
    unsigned int i;
    for (i = 0; i < g_ps1_scene.ui_glyph_count; ++i) {
        const ps1_ui_glyph* glyph = &g_ps1_scene.ui_glyphs[i];
        if (glyph->codepoint == codepoint)
            return glyph;
    }
    return 0;
}

static int glyph_scale_for_ascii(const ps1_ui_element* ui) {
    int scale = ui->font_size / 8;
    if (scale < 1) scale = 1;
    return scale;
}

static int glyph_scale_for_bitmap(const ps1_ui_element* ui) {
    int scale = ui->font_size / 16;
    if (scale < 1) scale = 1;
    return scale;
}

static int codepoint_width_px(uint32_t cp, const ps1_ui_element* ui) {
    if (cp < 0x80u)
        return 6 * glyph_scale_for_ascii(ui);
    {
        const ps1_ui_glyph* glyph = find_glyph(cp);
        if (glyph)
            return (int)glyph->advance * glyph_scale_for_bitmap(ui);
    }
    return 6 * glyph_scale_for_ascii(ui);
}

static int text_width_px(const char* text, unsigned int len, const ps1_ui_element* ui) {
    unsigned int index = 0;
    int width = 0;
    while (index < len) {
        const uint32_t cp = utf8_next(text, len, &index);
        if (cp == '\n')
            break;
        width += codepoint_width_px(cp, ui);
    }
    return width;
}

static int text_height_px(const char* text, unsigned int len, const ps1_ui_element* ui) {
    unsigned int index = 0;
    int height = 7 * glyph_scale_for_ascii(ui);
    while (index < len) {
        const uint32_t cp = utf8_next(text, len, &index);
        if (cp >= 0x80u) {
            const ps1_ui_glyph* glyph = find_glyph(cp);
            if (glyph) {
                const int h = (int)glyph->height * glyph_scale_for_bitmap(ui);
                if (h > height)
                    height = h;
            }
        }
    }
    return height;
}

static void draw_tile(uint32_t* ot, char** pri, const char* packet_end,
                      int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    TILE* tile;
    if ((const char*)(*pri + sizeof(TILE)) > packet_end)
        return;
    tile = (TILE*)*pri;
    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, r, g, b);
    addPrim(ot + UI_OT_SLOT, tile);
    *pri = (char*)(tile + 1);
}

static void draw_image(uint32_t* ot, char** pri, const char* packet_end, const ps1_ui_element* ui) {
    int x = ui->x;
    int y = ui->y;
    int w = ui->w;
    int h = ui->h;
    if (ui->color_a == 0)
        return;

    if (ui->texture_index != 0) {
        const uint16_t tpage = ps1_texture_tpage(ui->texture_index);
        uint16_t tw = ps1_texture_width(ui->texture_index);
        uint16_t th = ps1_texture_height(ui->texture_index);
        const int uoff = (int)ps1_texture_u_offset(ui->texture_index);
        const int voff = (int)ps1_texture_v_offset(ui->texture_index);
        POLY_FT4* poly;
        if (!tpage)
            return;
        if (tw == 0) tw = 64;
        if (th == 0) th = 64;
        if (ui->preserve_aspect && ui->w > 0 && ui->h > 0 && th > 0) {
            const int target_w = ui->w;
            const int target_h = ui->h;
            int fit_w = target_w;
            int fit_h = (int)(((int32_t)target_w * (int32_t)th) / (int32_t)tw);
            if (fit_h > target_h) {
                fit_h = target_h;
                fit_w = (int)(((int32_t)target_h * (int32_t)tw) / (int32_t)th);
            }
            if (fit_w < 1) fit_w = 1;
            if (fit_h < 1) fit_h = 1;
            x = ui->x + (target_w - fit_w) / 2;
            y = ui->y + (target_h - fit_h) / 2;
            w = fit_w;
            h = fit_h;
        }
        if (tw > 255) tw = 255;
        if (th > 255) th = 255;
        if ((const char*)(*pri + sizeof(POLY_FT4)) > packet_end)
            return;
        poly = (POLY_FT4*)*pri;
        setPolyFT4(poly);
        if (ui->rotation_degrees != 0) {
            const fix16_t turn = fix16_div(FIX16_FROM_INT((int)ui->rotation_degrees), FIX16_FROM_INT(360));
            const fix16_t sn = fix16_sin(turn);
            const fix16_t cs = fix16_cos(turn);
            const int cx = x + w / 2;
            const int cy = y + h / 2;
            const int dx0 = -w / 2;
            const int dx1 = w - w / 2;
            const int dy0 = -h / 2;
            const int dy1 = h - h / 2;
#define ROT_X(dx, dy) (cx + (int)(((int64_t)(dx) * cs - (int64_t)(dy) * sn) >> 16))
#define ROT_Y(dx, dy) (cy + (int)(((int64_t)(dx) * sn + (int64_t)(dy) * cs) >> 16))
            setXY4(poly,
                   ROT_X(dx0, dy0), ROT_Y(dx0, dy0),
                   ROT_X(dx1, dy0), ROT_Y(dx1, dy0),
                   ROT_X(dx0, dy1), ROT_Y(dx0, dy1),
                   ROT_X(dx1, dy1), ROT_Y(dx1, dy1));
#undef ROT_X
#undef ROT_Y
        } else {
            setXY4(poly, x, y,
                   x + w, y,
                   x, y + h,
                   x + w, y + h);
        }
        /* PS1 texture export uploads image rows flipped for 3D mesh UVs.
         * UI quads use screen-space top-left coordinates, so flip V here.
         * Textured PS1 polygons treat RGB 128 as neutral modulation; 255 makes
         * UI images look emissive/overbright compared to the editor preview. */
        setUV4(poly,
               clamp_ui_uv(uoff), clamp_ui_uv(voff + th),
               clamp_ui_uv(uoff + tw), clamp_ui_uv(voff + th),
               clamp_ui_uv(uoff), clamp_ui_uv(voff),
               clamp_ui_uv(uoff + tw), clamp_ui_uv(voff));
        setRGB0(poly,
                (uint8_t)((ui->color_r + 1u) >> 1),
                (uint8_t)((ui->color_g + 1u) >> 1),
                (uint8_t)((ui->color_b + 1u) >> 1));
        poly->tpage = tpage;
        poly->clut = 0;
        addPrim(ot + UI_OT_SLOT, poly);
        *pri = (char*)(poly + 1);
    } else {
        POLY_F4* poly;
        if ((const char*)(*pri + sizeof(POLY_F4)) > packet_end)
            return;
        poly = (POLY_F4*)*pri;
        setPolyF4(poly);
        setXY4(poly, x, y,
               x + w, y,
               x, y + h,
               x + w, y + h);
        setRGB0(poly, ui->color_r, ui->color_g, ui->color_b);
        addPrim(ot + UI_OT_SLOT, poly);
        *pri = (char*)(poly + 1);
    }
}

static void draw_text(uint32_t* ot, char** pri, const char* packet_end, const ps1_ui_element* ui) {
    const char* text;
    unsigned int index;
    int x;
    int y;
    int width;
    int height;

    if (ui->color_a == 0 || ui->text_length == 0)
        return;
    if (!g_ps1_scene.ui_text || ui->text_offset + ui->text_length > g_ps1_scene.ui_text_size)
        return;

    text = g_ps1_scene.ui_text + ui->text_offset;
    width = text_width_px(text, ui->text_length, ui);
    height = text_height_px(text, ui->text_length, ui);
    x = ui->x;
    if (ui->alignment == 1)
        x = ui->x + (ui->w - width) / 2;
    else if (ui->alignment == 2)
        x = ui->x + ui->w - width;
    y = ui->y + (ui->h - height) / 2;

    index = 0;
    while (index < ui->text_length) {
        const uint32_t cp = utf8_next(text, ui->text_length, &index);
        int row, col, scale;
        if (cp == '\n') {
            x = ui->x;
            y += height + 1;
            continue;
        }

        if (cp < 0x80u) {
            const uint8_t* rows = font_rows((char)cp);
            scale = glyph_scale_for_ascii(ui);
            if (rows) {
                for (row = 0; row < 7; ++row) {
                    for (col = 0; col < 5; ++col) {
                        if (rows[row] & (uint8_t)(1u << (4 - col))) {
                            draw_tile(ot, pri, packet_end,
                                      x + col * scale, y + row * scale, scale, scale,
                                      ui->color_r, ui->color_g, ui->color_b);
                            if ((const char*)*pri >= packet_end)
                                return;
                        }
                    }
                }
            }
            x += 6 * scale;
            continue;
        }

        {
            const ps1_ui_glyph* glyph = find_glyph(cp);
            const uint16_t* rows;
            scale = glyph_scale_for_bitmap(ui);
            if (!glyph || !g_ps1_scene.ui_glyph_rows ||
                glyph->row_offset + glyph->height > g_ps1_scene.ui_glyph_row_count) {
                x += 6 * glyph_scale_for_ascii(ui);
                continue;
            }
            rows = g_ps1_scene.ui_glyph_rows + glyph->row_offset;
            for (row = 0; row < (int)glyph->height; ++row) {
                for (col = 0; col < (int)glyph->width; ++col) {
                    if (rows[row] & (uint16_t)(1u << (glyph->width - 1 - col))) {
                        draw_tile(ot, pri, packet_end,
                                  x + col * scale, y + row * scale, scale, scale,
                                  ui->color_r, ui->color_g, ui->color_b);
                        if ((const char*)*pri >= packet_end)
                            return;
                    }
                }
            }
            x += (int)glyph->advance * scale;
        }
    }
}

static void draw_audio_spectrum(uint32_t* ot, char** pri, const char* packet_end,
                                const ps1_ui_element* ui) {
    uint8_t levels[32];
    unsigned int bars = ui->spectrum_bars;
    int gap, bar_width;
    if (bars < 4u) bars = 4u;
    if (bars > 32u) bars = 32u;
    ps1_audio_get_spectrum(levels, bars);
    gap = ui->spectrum_gap;
    bar_width = (ui->w - gap * ((int)bars - 1)) / (int)bars;
    if (bar_width < 1) bar_width = 1;
    for (unsigned int i = 0; i < bars; ++i) {
        uint32_t target = ((uint32_t)levels[i] * ui->spectrum_sensitivity_q8) >> 8;
        int height, x;
        if (target > 255u) target = 255u;
        if (target >= s_spectrum_display[i])
            s_spectrum_display[i] = (uint8_t)target;
        else
            s_spectrum_display[i] = (uint8_t)((s_spectrum_display[i] * 3u + target) / 4u);
        height = (ui->h * s_spectrum_display[i]) / 255;
        if (height <= 0) continue;
        x = ui->x + (int)i * (bar_width + gap);
        draw_tile(ot, pri, packet_end, x, ui->y + ui->h - height,
                  bar_width, height, ui->color_r, ui->color_g, ui->color_b);
        if ((const char*)*pri >= packet_end) return;
    }
    /* addPrim() prepends to an ordering-table bucket, so primitives in the
       same bucket are drawn in reverse submission order. Submit the
       background last so it is rendered first and the bars remain on top. */
    if (ui->spectrum_background[3] != 0)
        draw_tile(ot, pri, packet_end, ui->x, ui->y, ui->w, ui->h,
                  ui->spectrum_background[0], ui->spectrum_background[1],
                  ui->spectrum_background[2]);
}

static void init_button_group_state(void) {
    unsigned int i;
    if (s_button_groups_initialized)
        return;
    for (i = 0; i < g_ps1_scene.ui_button_group_count && i < 32u; ++i) {
        const ps1_ui_button_group* group = &g_ps1_scene.ui_button_groups[i];
        uint8_t selected = group->selected_index;
        if (group->button_count > 0 && selected >= group->button_count)
            selected = (uint8_t)(group->button_count - 1u);
        s_button_group_selected[i] = selected;
    }
    s_button_groups_initialized = 1;
}

void ps1_ui_update(void) {
    unsigned int i;
    int nav_up;
    int nav_down;
    init_button_group_state();
    nav_up = ps1_input_key_down("Up");
    nav_down = ps1_input_key_down("Down");
    if (!nav_up && !nav_down)
        return;
    for (i = 0; i < g_ps1_scene.ui_button_group_count && i < 32u; ++i) {
        const ps1_ui_button_group* group = &g_ps1_scene.ui_button_groups[i];
        uint8_t selected;
        if (!group->gamepad_navigation || group->button_count == 0)
            continue;
        selected = s_button_group_selected[i];
        if (selected >= group->button_count)
            selected = 0;
        if (nav_up) {
            if (selected > 0)
                --selected;
            else if (group->wrap_navigation)
                selected = (uint8_t)(group->button_count - 1u);
        } else if (nav_down) {
            if (selected + 1u < group->button_count)
                ++selected;
            else if (group->wrap_navigation)
                selected = 0;
        }
        s_button_group_selected[i] = selected;
    }
}

static void draw_default_cursor(uint32_t* ot, char** pri, const char* packet_end,
                                int x, int y, int w, int h) {
    int bar = w / 3;
    if (bar < 2) bar = 2;
    draw_tile(ot, pri, packet_end, x, y + h / 4, bar, h / 2, 255, 255, 255);
    draw_tile(ot, pri, packet_end, x + bar, y + h / 3, bar, h / 3, 255, 255, 255);
    draw_tile(ot, pri, packet_end, x + bar * 2, y + h / 2 - bar / 2, bar, bar, 255, 255, 255);
}

static void draw_button_group_cursors(uint32_t* ot, char** pri, const char* packet_end) {
    unsigned int i;
    init_button_group_state();
    for (i = 0; i < g_ps1_scene.ui_button_group_count && i < 32u; ++i) {
        const ps1_ui_button_group* group = &g_ps1_scene.ui_button_groups[i];
        unsigned int rect_index;
        const ps1_ui_button_rect* rect;
        int cx, cy, x, y, w, h;
        if (group->button_count == 0)
            continue;
        rect_index = (unsigned int)group->button_rect_offset + (unsigned int)s_button_group_selected[i];
        if (rect_index >= g_ps1_scene.ui_button_rect_count)
            continue;
        rect = &g_ps1_scene.ui_button_rects[rect_index];
        w = group->cursor_w;
        h = group->cursor_h;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        cx = rect->x + group->cursor_offset_x;
        cy = rect->y + rect->h / 2 + group->cursor_offset_y;
        x = cx - w / 2;
        y = cy - h / 2;
        if (group->cursor_texture_index != 0) {
            ps1_ui_element cursor;
            cursor.kind = PS1_UI_IMAGE;
            cursor.x = (int16_t)x;
            cursor.y = (int16_t)y;
            cursor.w = (int16_t)w;
            cursor.h = (int16_t)h;
            cursor.color_r = 255;
            cursor.color_g = 255;
            cursor.color_b = 255;
            cursor.color_a = 255;
            cursor.texture_index = group->cursor_texture_index;
            cursor.preserve_aspect = 1;
            cursor.alignment = 1;
            cursor.font_size = 8;
            cursor.text_offset = 0;
            cursor.text_length = 0;
            cursor.spectrum_background[0] = 0;
            cursor.spectrum_background[1] = 0;
            cursor.spectrum_background[2] = 0;
            cursor.spectrum_background[3] = 0;
            cursor.spectrum_bars = 16;
            cursor.spectrum_gap = 1;
            cursor.spectrum_sensitivity_q8 = 256;
            cursor.rotation_degrees = 0;
            draw_image(ot, pri, packet_end, &cursor);
        } else {
            draw_default_cursor(ot, pri, packet_end, x, y, w, h);
        }
        if ((const char*)*pri >= packet_end)
            return;
    }
}

void ps1_ui_render(uint32_t* ot, char** nextpri, const char* packet_end) {
    unsigned int i;
    if (!ot || !nextpri || !*nextpri || !packet_end)
        return;
    draw_button_group_cursors(ot, nextpri, packet_end);
    for (i = 0; i < g_ps1_scene.ui_count; ++i) {
        const ps1_ui_element* ui = &g_ps1_scene.ui_elements[i];
        if ((const char*)*nextpri >= packet_end)
            return;
        if (ui->kind == PS1_UI_IMAGE)
            draw_image(ot, nextpri, packet_end, ui);
        else if (ui->kind == PS1_UI_TEXT)
            draw_text(ot, nextpri, packet_end, ui);
        else if (ui->kind == PS1_UI_AUDIO_SPECTRUM)
            draw_audio_spectrum(ot, nextpri, packet_end, ui);
    }
}
