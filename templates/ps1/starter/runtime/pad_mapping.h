#ifndef MIPSYNC_PAD_MAPPING_H
#define MIPSYNC_PAD_MAPPING_H
#include <stdint.h>
#include <string.h>
/* Standard PS1 wire protocol bits, shared by digital pads and DualShock. */
static inline uint16_t ps1_named_button_mask(const char* name) {
    static const char* const names[16] = {"Cross","Circle","Square","Triangle","Start","Select",
        "L1","R1","L2","R2","L3","R3","DPadUp","DPadDown","DPadLeft","DPadRight"};
    static const uint16_t masks[16] = {0x4000,0x2000,0x8000,0x1000,8,1,0x400,0x800,
        0x100,0x200,2,4,0x10,0x40,0x80,0x20};
    int i;
    if (!name) return 0;
    for (i=0;i<16;++i) if (strcmp(name,names[i])==0) return masks[i];
    if (strcmp(name,"Jump")==0) return 0x4000;
    if (strcmp(name,"Run")==0) return 0x8000;
    if (strcmp(name,"Aim")==0) return 0x200;
    if (strcmp(name,"StrafeLeft")==0) return 0x400;
    if (strcmp(name,"StrafeRight")==0) return 0x800;
    return 0;
}
static inline int32_t ps1_stick_axis_q16(int raw) {
    int magnitude=raw<0 ? -raw : raw;
    if (magnitude<=16) return 0;
    if (magnitude>127) magnitude=127;
    magnitude=((magnitude-16)*65536)/111;
    return raw<0 ? -magnitude : magnitude;
}
static inline int32_t ps1_combine_axis_q16(int32_t analog, int positive, int negative) {
    int32_t value=analog+(positive-negative)*65536;
    return value>65536 ? 65536 : value<-65536 ? -65536 : value;
}
#endif
