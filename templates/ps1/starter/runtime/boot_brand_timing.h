#ifndef MIPSYNC_BOOT_BRAND_TIMING_H
#define MIPSYNC_BOOT_BRAND_TIMING_H
/* RGB modulation 128 is neutral on PS1. Approximately one third of a second
 * for each fade, with the same 1.5 second total duration on NTSC and PAL. */
static inline int ps1_boot_brand_brightness(int field, int duration) {
    int fade = (duration * 2 + 4) / 9;
    if (field < fade) return 128 * field / (fade - 1);
    if (field >= duration - fade)
        return 128 * (duration - 1 - field) / (fade - 1);
    return 128;
}
#endif
