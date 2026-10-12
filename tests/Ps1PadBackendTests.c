#include <stdio.h>
#include <stdlib.h>
#include "../templates/ps1/starter/runtime/input.c"
int EnterCriticalSection(void) { return 1; }
void ExitCriticalSection(void) {}
void StopPAD(void) {}
void StartPAD(void) {}
void InitPAD(unsigned char* a,int al,unsigned char* b,int bl) {(void)a;(void)al;(void)b;(void)bl;}
void ChangeClearPAD(int n) {(void)n;}
void (*VSyncCallback(void (*callback)(void)))(void) {(void)callback;return 0;}
static void check(int condition,const char* name,const char* operation) {
    if(!condition){fprintf(stderr,"%s: %s failed\n",name,operation);exit(1);}
}
static void packet(int type,unsigned int held) {
    // Feed BIOS response bytes, independently of the runtime's named masks.
    s_pad_buf[0][0]=0;s_pad_buf[0][1]=(type<<4)|(type==4?1:3);
    s_pad_buf[0][2]=(unsigned char)~held;s_pad_buf[0][3]=(unsigned char)(~held>>8);
    s_pad_buf[0][4]=s_pad_buf[0][5]=s_pad_buf[0][6]=s_pad_buf[0][7]=128;
    input_vblank();
}
int main(void) {
    static const char* names[]={"Select","L3","R3","Start","DPadUp","DPadRight","DPadDown","DPadLeft",
        "L2","R2","L1","R1","Triangle","Circle","Cross","Square"};
    s_initialized=1;
    for(int mode=0;mode<3;++mode)for(int bit=0;bit<16;++bit) {
        const int type=mode==0?4:mode==1?7:5;const char* name=names[bit];
        packet(type,0);ps1_input_poll();
        packet(type,1u<<bit);ps1_input_poll();
        check(ps1_input_key_held(name),name,"held");check(ps1_input_key_down(name),name,"down");
        check(!ps1_input_key_up(name),name,"no premature up");
        for(int other=0;other<16;++other)if(other!=bit)check(!ps1_input_key_held(names[other]),name,"no phantom button");
        packet(type,1u<<bit);ps1_input_poll();check(!ps1_input_key_down(name),name,"down only once");
        packet(type,0);ps1_input_poll();check(ps1_input_key_up(name)&&!ps1_input_key_held(name),name,"release");
        packet(type,1u<<bit);packet(type,0);ps1_input_poll();
        check(ps1_input_key_down(name)&&ps1_input_key_up(name)&&!ps1_input_key_held(name),name,"short tap between frames");
        ps1_input_poll();check(!ps1_input_key_down(name)&&!ps1_input_key_up(name),name,"tap edges consumed once");
    }
    /* The BIOS callback can run before its pad buffer refresh.  The runtime
     * must still detect a held packet from the game-tick fallback sample. */
    packet(7, 0); ps1_input_poll();
    s_pad_buf[0][2] = (unsigned char)~0x400u;
    s_pad_buf[0][3] = (unsigned char)(~0x400u >> 8);
    ps1_input_poll();
    check(ps1_input_key_down("L1"), "L1", "game-tick fallback edge");
    puts("All 16 PS1 buttons: digital/DualShock/analog stick, held/down/up and buffered short taps passed.");
}
