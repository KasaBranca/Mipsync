#ifndef MIPSYNC_UI_H
#define MIPSYNC_UI_H

#include <stdint.h>

void ps1_ui_update(void);
void ps1_ui_render(uint32_t* ot, char** nextpri, const char* packet_end);

#endif /* MIPSYNC_UI_H */
