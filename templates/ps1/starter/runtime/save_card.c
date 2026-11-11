#include "save.h"
#include "save_handoff.h"
#include <psxapi.h>
static int s_card_started;
static void begin(void){if(!s_card_started){InitCARD(1);StartCARD();_bu_init();s_card_started=1;}}
int ps1_save_card_read(const char* file,void* data,unsigned size){
    int fd,result;begin();fd=open(file,1);if(fd<0)return 0;
    result=read(fd,data,size);close(fd);return result==(int)size;
}
int ps1_save_card_write(const char* file,unsigned offset,const void* data,unsigned size,int create){
    int fd,result;begin();fd=open(file,create?(2|0x200|0x10000):2);if(fd<0)return 0;
    if(lseek(fd,offset,0)!=(int)offset){close(fd);return 0;}
    result=write(fd,data,size);close(fd);return result==(int)size;
}

/* LoadExec replaces user RAM, while BIOS kernel allocations survive. The
 * boot-brand marker uses the preceding 16 bytes of the SYSTEM.CNF arg buffer. */
#define SAVE_HANDOFF ((volatile uint32_t*)0x80000190u)
#define HANDOFF_MAGIC 0x4d534156u
void ps1_save_cancel_handoff(void){
    uintptr_t address=SAVE_HANDOFF[2];
    if(SAVE_HANDOFF[0]==HANDOFF_MAGIC && SAVE_HANDOFF[1]==~HANDOFF_MAGIC &&
       ps1_save_handoff_address((uint32_t)address))
        free_kernel_memory((void*)address);
    SAVE_HANDOFF[0]=SAVE_HANDOFF[1]=SAVE_HANDOFF[2]=0;
}
int ps1_save_prepare_handoff(void){
    void* data;
    ps1_save_cancel_handoff();
    if(!ps1_save_has_values())return 1;
    data=alloc_kernel_memory(PS1_SAVE_SNAPSHOT_BYTES);
    if(!data)return 0;
    if(!ps1_save_handoff_address((uint32_t)(uintptr_t)data)){free_kernel_memory(data);return 0;}
    if(!ps1_save_snapshot((void*)(uintptr_t)ps1_save_handoff_address((uint32_t)(uintptr_t)data),PS1_SAVE_SNAPSHOT_BYTES)){free_kernel_memory(data);return 0;}
    SAVE_HANDOFF[2]=(uint32_t)(uintptr_t)data;
    SAVE_HANDOFF[1]=~HANDOFF_MAGIC;SAVE_HANDOFF[0]=HANDOFF_MAGIC;return 1;
}
void ps1_save_restore_handoff(void){
    uintptr_t address=SAVE_HANDOFF[2];
    if(SAVE_HANDOFF[0]==HANDOFF_MAGIC && SAVE_HANDOFF[1]==~HANDOFF_MAGIC &&
       ps1_save_handoff_address((uint32_t)address))
        ps1_save_restore_snapshot((const void*)(uintptr_t)ps1_save_handoff_address((uint32_t)address),PS1_SAVE_SNAPSHOT_BYTES);
    ps1_save_cancel_handoff();
}
