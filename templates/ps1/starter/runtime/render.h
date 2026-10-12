#ifndef MIPSYNC_RENDER_H
#define MIPSYNC_RENDER_H

#include <stdint.h>
#define PS1_RENDER_OT_LEN 2048

void ps1_render_init(int screen_w, int screen_h);
/* nextpri points into the caller's packet buffer; packet_end must be the first byte after it. */
void ps1_render_frame(uint32_t* ot, char** nextpri, const char* packet_end);
/* Submit world, apply authored grading, then submit ungraded overlay UI. */
void ps1_render_submit(uint32_t* ot, int framebuffer_x, int framebuffer_y, int width, int height, const uint16_t* lut);

#endif /* MIPSYNC_RENDER_H */
