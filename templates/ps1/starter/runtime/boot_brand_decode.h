#ifndef MIPSYNC_BOOT_BRAND_DECODE_H
#define MIPSYNC_BOOT_BRAND_DECODE_H
#include <stdint.h>

typedef struct {
    unsigned int run;
    unsigned int remaining;
    uint16_t color;
} ps1_boot_brand_decoder;

/* Decode one row at a time so startup needs only a 320-byte stack buffer. */
static inline int ps1_boot_brand_decode_row(ps1_boot_brand_decoder* state,
        const uint16_t runs[][2], unsigned int count, uint16_t* row,
        unsigned int width) {
    unsigned int x;
    for (x = 0; x < width; ++x) {
        if (!state->remaining) {
            if (state->run >= count || !runs[state->run][1]) return 0;
            state->color = runs[state->run][0];
            state->remaining = runs[state->run++][1];
        }
        row[x] = state->color;
        --state->remaining;
    }
    return 1;
}
#endif
