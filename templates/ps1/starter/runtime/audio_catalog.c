#include "audio.h"
static int same_path(const char* a,const char* b){
    if(!a||!b)return 0;
    while(a[0]=='.'&&(a[1]=='/'||a[1]=='\\'))a+=2;
    while(b[0]=='.'&&(b[1]=='/'||b[1]=='\\'))b+=2;
    while(*a&&*b){char x=*a++,y=*b++;if(x=='\\')x='/';if(y=='\\')y='/';if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}
    return !*a&&!*b;
}
int ps1_audio_find_clip(const char* path){
    unsigned i;if(!path||!*path)return 0;
    for(i=0;i<g_ps1_audio_clip_count;++i)if(same_path(path,g_ps1_audio_clips[i].source_path))return (int)i+1;
    return -1;
}
const char* ps1_audio_clip_path(unsigned int index){
    return index&&index<=g_ps1_audio_clip_count&&g_ps1_audio_clips[index-1].source_path?g_ps1_audio_clips[index-1].source_path:"";
}
