#ifndef MIPSYNC_FRAME_TIMING_H
#define MIPSYNC_FRAME_TIMING_H

#include <stdint.h>

/* Shared by the PS1 loop and editor P-Sync. The presentation clock is the
 * hardware NTSC rate; game logic uses nominal 60 Hz Q16.16 field deltas. */
#define PS1_LOGIC_FIELD_HZ 60
#define PS1_MAX_LOGIC_VBLANKS 4

static inline int32_t ps1_frame_delta_q16(int32_t elapsed_vblanks) {
    if (elapsed_vblanks < 1) elapsed_vblanks = 1;
    if (elapsed_vblanks > PS1_MAX_LOGIC_VBLANKS) elapsed_vblanks = PS1_MAX_LOGIC_VBLANKS;
    return (elapsed_vblanks * 65536) / PS1_LOGIC_FIELD_HZ;
}

#endif
