#ifndef MIPSYNC_COLOR_GRADING_H
#define MIPSYNC_COLOR_GRADING_H
#include <stdint.h>
/* Offline LUT: no floating-point work on PS1. Preserve framebuffer mask. */
static inline uint32_t ps1_grade_pair(uint32_t pixels, const uint16_t* lut) {
    if (((pixels ^ (pixels >> 16)) & 0x7fffu)==0) {
        const uint32_t graded=lut[pixels & 0x7fffu];
        return graded | (graded << 16) | (pixels & 0x80008000u);
    }
    return (uint32_t)lut[pixels & 0x7fffu] |
        ((uint32_t)lut[(pixels >> 16) & 0x7fffu] << 16) | (pixels & 0x80008000u);
}
static inline void ps1_grade_pixels(uint16_t* pixels, unsigned int count, const uint16_t* lut) {
    if (!lut || !count) return;
#if defined(__GNUC__) && (!defined(__BYTE_ORDER__) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    /* Aligned word I/O halves framebuffer RAM traffic. Cache repeated pairs
     * (sky/fog/flat spans), and unroll the loop without changing any RGB555 bit. */
    typedef uint32_t grading_word __attribute__((__may_alias__));
    grading_word* words;
    unsigned int pairs;
    uint32_t previous=0, graded=ps1_grade_pair(0,lut);
    if ((uintptr_t)pixels & 2u) {
        const uint16_t pixel = *pixels;
        *pixels++ = lut[pixel & 0x7fffu] | (pixel & 0x8000u);
        --count;
    }
    words=(grading_word*)pixels; pairs=count/2;
#define PS1_GRADE_PAIR() do { \
    const uint32_t input=*words; \
    if (input!=previous) { graded=ps1_grade_pair(input,lut); previous=input; } \
    *words++=graded; \
} while (0)
    while (pairs>=4) {
        PS1_GRADE_PAIR(); PS1_GRADE_PAIR(); PS1_GRADE_PAIR(); PS1_GRADE_PAIR();
        pairs-=4;
    }
    while (pairs) { PS1_GRADE_PAIR(); --pairs; }
#undef PS1_GRADE_PAIR
    if (count & 1u) {
        pixels=(uint16_t*)words;
        const uint16_t pixel=*pixels;
        *pixels=lut[pixel & 0x7fffu] | (pixel & 0x8000u);
    }
#else
    unsigned int i;
    for (i=0; i<count; ++i) {
        const uint16_t pixel=pixels[i];
        pixels[i]=lut[pixel & 0x7fffu] | (pixel & 0x8000u);
    }
#endif
}
#endif
