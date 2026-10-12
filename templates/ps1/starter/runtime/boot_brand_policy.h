#ifndef MIPSYNC_BOOT_BRAND_POLICY_H
#define MIPSYNC_BOOT_BRAND_POLICY_H
#include <stdint.h>

/* A one-shot scene handoff in the BIOS's SYSTEM.CNF argument buffer. This
 * buffer lies outside the loaded executable and its cleared BSS. Consume it
 * before initializing SDK drivers, and preserve ordinary boot arguments. */
enum { MIPSYNC_BOOT_HANDOFF_BYTES = 16 };
static const uint8_t k_mipsync_boot_handoff[MIPSYNC_BOOT_HANDOFF_BYTES] = {
    'M', 'I', 'P', 'S', 'Y', 'N', 'C', '_', 'S', 'C', 'E', 'N', 'E', '_', '1', 0
};

static inline void ps1_boot_handoff_clear(volatile uint8_t* buffer) {
    unsigned int i;
    for (i = 0; i < MIPSYNC_BOOT_HANDOFF_BYTES; ++i) buffer[i] = 0;
}

static inline void ps1_boot_handoff_stamp(volatile uint8_t* buffer) {
    unsigned int i;
    for (i = 0; i < MIPSYNC_BOOT_HANDOFF_BYTES; ++i)
        buffer[i] = k_mipsync_boot_handoff[i];
}

static inline int ps1_boot_handoff_consume(volatile uint8_t* buffer) {
    unsigned int i;
    for (i = 0; i < MIPSYNC_BOOT_HANDOFF_BYTES; ++i)
        if (buffer[i] != k_mipsync_boot_handoff[i]) return 0;
    ps1_boot_handoff_clear(buffer);
    return 1;
}
#endif
