#include "save.h"
#include <string.h>
#include <stdio.h>

typedef struct { char key[PS1_SAVE_KEY_BYTES]; char text[PS1_SAVE_STRING_BYTES]; fix16_t number; uint8_t type; } save_entry;
static save_entry s_entries[PS1_SAVE_ENTRIES], s_pending[PS1_SAVE_ENTRIES];
static unsigned s_count;
static char s_game[96];
static uint8_t s_block[PS1_SAVE_BLOCK_BYTES];
enum { BANK_BYTES=3840, BANK0=512, BANK1=4352, RECORDS=224 };
static uint32_t hash(const char* s) { uint32_t h=2166136261u; while(*s){ h^=(uint8_t)*s++;h*=16777619u; }return h; }
static uint32_t u32(const uint8_t* p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t* p,uint32_t n){unsigned i;for(i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static uint32_t crc(const uint8_t* p,unsigned n,unsigned zero){
    uint32_t c=~0u;unsigned i,j;
    for(i=0;i<n;++i){c^=(i>=zero&&i<zero+4)?0:p[i];for(j=0;j<8;++j)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
    return ~c;
}
void ps1_save_init(const char* game){
    s_count=0;memset(s_entries,0,sizeof(s_entries));
    if(!game)game="Mipsync";
    snprintf(s_game,sizeof(s_game),"%.72s/%08X",game,(unsigned)hash(game));
}
int ps1_save_set(const char* key,int type,fix16_t number,const char* text){
    unsigned i;
    if(!key||strlen(key)>=PS1_SAVE_KEY_BYTES||type<1||type>4||
       (type==PS1_SAVE_STRING&&(!text||strlen(text)>=PS1_SAVE_STRING_BYTES)))return 0;
    for(i=0;i<s_count;++i)if(strcmp(s_entries[i].key,key)==0)break;
    if(i==s_count){if(s_count==PS1_SAVE_ENTRIES)return 0;++s_count;}
    strcpy(s_entries[i].key,key);s_entries[i].type=(uint8_t)type;s_entries[i].number=number;
    strcpy(s_entries[i].text,type==PS1_SAVE_STRING?text:"");return 1;
}
int ps1_save_get(const char* key,int type,fix16_t* number,const char** text){
    unsigned i;if(!key)return 0;
    for(i=0;i<s_count;++i)if(strcmp(s_entries[i].key,key)==0){
        if(s_entries[i].type!=type&&!(type==PS1_SAVE_FLOAT&&s_entries[i].type==PS1_SAVE_INT))return 0;
        if(number)*number=s_entries[i].number;if(text)*text=s_entries[i].text;return 1;
    }return 0;
}
static int filename(const char* slot,char out[32]){
    if(!slot||!slot[0]||strlen(slot)>=96)return 0;
    snprintf(out,32,"bu00:MPS%08X%08X",(unsigned)hash(s_game),(unsigned)hash(slot));return 1;
}
static int identity(const uint8_t* p,const char* slot){
    return memcmp(p,"MIPSYNC SAVE1",12)==0&&memchr(p+16,0,96)&&memchr(p+112,0,96)&&
        strcmp((const char*)p+16,s_game)==0&&strcmp((const char*)p+112,slot)==0&&u32(p+208)==crc(p,212,208);
}
static void header(const char* slot){
    unsigned i;memset(s_block,0,sizeof(s_block));s_block[0]='S';s_block[1]='C';s_block[2]=0x11;s_block[3]=1;
    snprintf((char*)s_block+4,64,"Mipsync %.55s",s_game);
    s_block[0x62]=0xe0;s_block[0x63]=0x7f;
    for(i=0;i<128;++i)s_block[0x80+i]=0x11;
    memcpy(s_block+256,"MIPSYNC SAVE1",12);strcpy((char*)s_block+272,s_game);strcpy((char*)s_block+368,slot);
    put32(s_block+464,crc(s_block+256,212,208));
}
static int decode_bank(const uint8_t* p,const char* slot,int commit){
    unsigned count,offset=RECORDS,i,j;
    if(memcmp(p,"MSV1",4)||u32(p+20)!=crc(p,BANK_BYTES,20)||!memchr(p+32,0,96)||!memchr(p+128,0,96)||
       strcmp((const char*)p+32,s_game)||strcmp((const char*)p+128,slot))return 0;
    count=u32(p+16);if(count>PS1_SAVE_ENTRIES)return 0;
    memset(s_pending,0,sizeof(s_pending));
    for(i=0;i<count;++i){
        unsigned type,k,t;if(offset+8>BANK_BYTES)return 0;
        type=p[offset];k=p[offset+1];t=p[offset+2];
        if(type<1||type>4||k>=PS1_SAVE_KEY_BYTES||t>=PS1_SAVE_STRING_BYTES||offset+8+k+t>BANK_BYTES)return 0;
        s_pending[i].type=(uint8_t)type;s_pending[i].number=(fix16_t)u32(p+offset+4);offset+=8;
        memcpy(s_pending[i].key,p+offset,k);offset+=k;memcpy(s_pending[i].text,p+offset,t);offset+=t;
        if(memchr(s_pending[i].key,0,k)||memchr(s_pending[i].text,0,t))return 0;
        for(j=0;j<i;++j)if(!strcmp(s_pending[j].key,s_pending[i].key))return 0;
    }
    if(commit){memcpy(s_entries,s_pending,sizeof(s_entries));s_count=count;}return 1;
}
static int newest(const char* slot){
    int a=decode_bank(s_block+BANK0,slot,0),b=decode_bank(s_block+BANK1,slot,0);
    if(!a)return b?BANK1:0;if(!b)return BANK0;
    return (int32_t)(u32(s_block+BANK1+4)-u32(s_block+BANK0+4))>0?BANK1:BANK0;
}
int ps1_save_read(const char* slot){
    char file[32];int bank;
    if(!filename(slot,file)||!ps1_save_card_read(file,s_block,sizeof(s_block))||s_block[0]!='S'||s_block[1]!='C'||!identity(s_block+256,slot))return 0;
    bank=newest(slot);return bank?decode_bank(s_block+bank,slot,1):0;
}
static int encode_bank(uint8_t* p,const char* slot,uint32_t generation){
    unsigned i,pos=RECORDS;memset(p,0,BANK_BYTES);
    memcpy(p,"MSV1",4);put32(p+4,generation);put32(p+16,s_count);strcpy((char*)p+32,s_game);strcpy((char*)p+128,slot);
    for(i=0;i<s_count;++i){
        unsigned k=(unsigned)strlen(s_entries[i].key),t=(unsigned)strlen(s_entries[i].text);
        if(pos+8+k+t>BANK_BYTES)return 0;
        p[pos]=s_entries[i].type;p[pos+1]=(uint8_t)k;p[pos+2]=(uint8_t)t;put32(p+pos+4,(uint32_t)s_entries[i].number);pos+=8;
        memcpy(p+pos,s_entries[i].key,k);pos+=k;memcpy(p+pos,s_entries[i].text,t);pos+=t;
    }
    put32(p+20,crc(p,BANK_BYTES,20));
    return 1;
}
unsigned ps1_save_snapshot(void* out,unsigned capacity){
    if(capacity<BANK_BYTES||!out)return 0;
    return encode_bank((uint8_t*)out,"__scene_handoff__",0)?BANK_BYTES:0;
}
int ps1_save_restore_snapshot(const void* data,unsigned size){
    return size==BANK_BYTES&&decode_bank((const uint8_t*)data,"__scene_handoff__",1);
}
int ps1_save_has_values(void){return s_count!=0;}

int ps1_save_write(const char* slot){
    char file[32];int bank,offset;uint8_t* p;uint32_t generation=1;
    if(!filename(slot,file))return 0;
    if(ps1_save_card_read(file,s_block,sizeof(s_block))){
        if(s_block[0]!='S'||s_block[1]!='C'||!identity(s_block+256,slot))return 0;
        bank=newest(slot);if(bank)generation=u32(s_block+bank+4)+1u;
    }else{
        header(slot);if(!ps1_save_card_write(file,0,s_block,512,1))return 0;bank=0;
    }
    offset=bank==BANK0?BANK1:BANK0;p=s_block+offset;memset(p,0,BANK_BYTES);
    if(!encode_bank(p,slot,generation))return 0;
    /* Only replace the older bank; an interrupted write preserves the previous one. */
    return ps1_save_card_write(file,(unsigned)offset,p,BANK_BYTES,0);
}
