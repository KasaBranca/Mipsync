#ifndef MIPSYNC_SAVE_HANDOFF_H
#define MIPSYNC_SAVE_HANDOFF_H
#include "save.h"
/* BIOS allocators may return physical, KSEG0 or KSEG1 addresses. Access the
 * snapshot through KSEG0, but retain the original pointer for BIOS free(). */
static inline uint32_t ps1_save_handoff_address(uint32_t raw){
    uint32_t region=raw&0xe0000000u,physical=raw&0x1fffffffu;
    if(region!=0&&region!=0x80000000u&&region!=0xa0000000u)return 0;
    if(physical<0x1000u||physical>0x10000u-PS1_SAVE_SNAPSHOT_BYTES)return 0;
    return physical|0x80000000u;
}
#endif
