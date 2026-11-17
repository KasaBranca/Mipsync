// SPDX-License-Identifier: MIT
#ifndef MIPSYNC_PACKET_FRAGMENT_H
#define MIPSYNC_PACKET_FRAGMENT_H
#include <stddef.h>
#include <stdint.h>
#define PS1_PACKET_FRAGMENT_MAX_WORDS 65536u
static inline int ps1_packet_fragment_range(uint32_t base, uint32_t bytes) {
    return !(base & 3u) && !(bytes & 3u) &&
        base <= 0xffffffu && bytes <= 0x1000000u - base;
}
typedef struct { uint16_t bucket, head, tail; } ps1_packet_fragment_bucket;

/* Find every bucket-local chain owned by one contiguous emitted mesh. Links
 * to earlier entities terminate the fragment; never follow foreign memory.
 * A caller-owned bitset (ceil(words / 32) words) rejects payload links, cycles
 * and multiply owned headers. Failure leaves entry contents unspecified. */
static inline int ps1_packet_fragment_capture(const uint32_t* source,
    size_t words, uint32_t source_address, const uint32_t* ot, size_t buckets,
    ps1_packet_fragment_bucket* entries, size_t capacity, uint32_t* unvisited, size_t scratch_words,
    size_t* entry_count) {
    size_t index = 0, nodes = 0, visited = 0, count = 0;
    if (!source || !ot || !entries || !unvisited || !entry_count) return 0;
    *entry_count = 0;
    if (!words || words > PS1_PACKET_FRAGMENT_MAX_WORDS || buckets > 65536u ||
        scratch_words < (words + 31u) / 32u ||
        !ps1_packet_fragment_range(source_address, (uint32_t)words * 4u)) return 0;
    for (size_t i = 0; i < (words + 31u) / 32u; ++i) unvisited[i] = 0;
    while (index < words) {
        const size_t length = 1u + (source[index] >> 24);
        if (length > words - index) return 0;
        unvisited[index / 32u] |= 1u << (index % 32u);
        ++nodes;
        index += length;
    }
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        uint32_t address = ot[bucket] & 0xffffffu;
        size_t head, tail;
        if (address == 0xffffffu || address < source_address || address - source_address >= words * 4u) continue;
        if (count == capacity) return 0;
        head = (address - source_address) / 4u;
        for (;;) {
            size_t offset;
            uint32_t mask;
            if (address & 3u) return 0;
            offset = (address - source_address) / 4u;
            mask = 1u << (offset % 32u);
            if (!(unvisited[offset / 32u] & mask)) return 0;
            unvisited[offset / 32u] &= ~mask;
            ++visited;
            tail = offset;
            address = source[offset] & 0xffffffu;
            if (address == 0xffffffu || address < source_address || address - source_address >= words * 4u) {
                if (address != 0xffffffu && (address & 3u)) return 0;
                break;
            }
        }
        entries[count].bucket = (uint16_t)bucket;
        entries[count].head = (uint16_t)head;
        entries[count++].tail = (uint16_t)tail;
    }
    if (visited != nodes) return 0;
    *entry_count = count;
    return 1;
}

/* Validated immutable fragments may move downwards in the same packet arena.
 * Rebase internal tags while copying forwards, before overlap can overwrite a
 * source header. Attach each tail to THIS frame's preceding entity chain.
 * The caller retains immutable validated source/entries, checks destination
 * capacity and 24-bit ranges, and forbids upwards overlapping copies. */
static inline void ps1_packet_fragment_copy(uint32_t* target,
    const uint32_t* source, size_t words, uint32_t source_address,
    uint32_t target_address, uint32_t* ot,
    const ps1_packet_fragment_bucket* entries, size_t entry_count) {
    /* Retained commands at the same CPU and DMA addresses already have valid
     * internal links and payload. Only bucket tails need this frame's heads. */
    if (target != source || target_address != source_address) {
        size_t index = 0;
        while (index < words) {
            const uint32_t tag = source[index];
            const uint32_t address = tag & 0xffffffu;
            const size_t length = 1u + (tag >> 24);
            const uint32_t link = address != 0xffffffu && address >= source_address &&
                address - source_address < words * 4u
                ? target_address + address - source_address : 0xffffffu;
            for (size_t i = 1; i < length; ++i) target[index + i] = source[index + i];
            target[index] = (tag & 0xff000000u) | link;
            index += length;
        }
    }
    for (size_t i = 0; i < entry_count; ++i) {
        const ps1_packet_fragment_bucket entry = entries[i];
        target[entry.tail] = (target[entry.tail] & 0xff000000u) | (ot[entry.bucket] & 0xffffffu);
        ot[entry.bucket] = (ot[entry.bucket] & 0xff000000u) | (target_address + entry.head * 4u);
    }
}
#endif
