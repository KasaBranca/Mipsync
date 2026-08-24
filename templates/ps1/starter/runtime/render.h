#ifndef MIPSYNC_RENDER_H
#define MIPSYNC_RENDER_H

#include <stdint.h>

void ps1_render_init(int screen_w, int screen_h);
/* nextpri points into the caller's packet buffer; packet_end must be the first byte after it. */
void ps1_render_frame(uint32_t* ot, char** nextpri, const char* packet_end);

#endif /* MIPSYNC_RENDER_H */
