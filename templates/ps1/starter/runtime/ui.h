#ifndef MIPSYNC_UI_H
#define MIPSYNC_UI_H

#include <stdint.h>

void ps1_ui_update(void);
int ps1_ui_set_text_visible(uint32_t entity_id, int visible);
int ps1_ui_set_image_visible(uint32_t entity_id, int visible);
int ps1_ui_set_text(uint32_t entity_id, const char* text);
int ps1_ui_set_color(uint32_t entity_id, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void ps1_ui_reset(void);
void mipsync_ui_invoke(uint16_t entity_index, uint8_t module_index, const char* method_name);
void ps1_ui_render(uint32_t* ot, char** nextpri, const char* packet_end);

#endif /* MIPSYNC_UI_H */
