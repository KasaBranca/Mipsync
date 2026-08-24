#ifndef MIPSYNC_INPUT_H
#define MIPSYNC_INPUT_H

#include <stdint.h>

void ps1_input_init(void);
void ps1_input_poll(void);

/* Returns 1 if the named key/button is held (WASD/Space/Arrow mapped to pad). */
int ps1_input_key_held(const char* name);

/* Returns 1 on the frame the button was first pressed. */
int ps1_input_key_down(const char* name);
int ps1_input_key_up(const char* name);

/* Right stick look deltas in Q16.16 units per frame (maps mouseDeltaX/Y). */
int32_t ps1_input_look_delta_x_q16(void);
int32_t ps1_input_look_delta_y_q16(void);

#endif /* MIPSYNC_INPUT_H */
