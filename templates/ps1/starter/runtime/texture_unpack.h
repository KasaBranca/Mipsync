#ifndef MIPSYNC_TEXTURE_UNPACK_H
#define MIPSYNC_TEXTURE_UNPACK_H

#include <stdint.h>
#include <string.h>

/* Each byte describes 1..128 bytes: high bit set means zeros; otherwise
 * that many literal bytes follow. Input and output must not overlap. */
static int ps1_texture_unpack_zero_runs(const void* source, unsigned int source_bytes,
                                      void* destination, unsigned int destination_bytes) {
    const uint8_t* input = (const uint8_t*)source;
    uint8_t* output = (uint8_t*)destination;
    const uintptr_t s = (uintptr_t)source, d = (uintptr_t)destination;
    unsigned int i = 0, o = 0;
    if (!source || !destination || !source_bytes || !destination_bytes) return 0;
    if (s <= d ? d - s < source_bytes : s - d < destination_bytes) return 0;
    while (i < source_bytes) {
        const unsigned int token = input[i++];
        const unsigned int count = (token & 127u) + 1u;
        if (destination_bytes - o < count) return 0;
        if (token & 128u) {
            memset(output + o, 0, count);
        } else {
            if (source_bytes - i < count) return 0;
            memcpy(output + o, input + i, count);
            i += count;
        }
        o += count;
    }
    return o == destination_bytes;
}

#endif /* MIPSYNC_TEXTURE_UNPACK_H */
