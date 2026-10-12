#include "midi.h"
#include "scene.h"
#include "audio.h"
#include <psxspu.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <hwregs_c.h>
#include <string.h>

/* Voice 0 and 0x1010..0x1100f belong to CD BGM. SFX performs no CD reads
   or DMA during playback; all tones are uploaded once at scene startup. */
#define BANK_ADDR 0x11040u
#define TONE_BYTES 256u
#define SEQUENCES 8u
typedef struct midi_player {
    const ps1_midi_sequence* sequence;
    unsigned entity;
    uint32_t time, event, clock, remainder;
    uint8_t paused, program[16], volume[16], expression[16], pan[16], sustain[16];
    uint16_t bend[16];
} midi_player;
typedef struct midi_voice {
    uint8_t active, held, owner, channel, note, velocity;
    uint32_t age;
} midi_voice;
static midi_player s_players[SEQUENCES];
static midi_voice s_voices[23];
typedef struct sample_player {
    unsigned entity, clip;
    uint32_t clock, elapsed_frames, duration_frames;
    uint8_t paused;
} sample_player;
static sample_player s_samples[23];
static uint32_t s_sample_addr[64];
static uint32_t s_sample_play_count;
static uint32_t s_age, s_play_serial;
static unsigned s_hz;

static void release_voice(unsigned v) {
    SpuSetKey(0, 1u << (v+1));
    s_voices[v].active=0;
}
static void release_player(unsigned p) {
    for(unsigned v=0;v<23;++v) if(s_voices[v].active==1 && s_voices[v].owner==p) release_voice(v);
}
static void voice_volume(unsigned v) {
    if(s_voices[v].active==2) {
        sample_player* player=&s_samples[v];
        const ps1_entity* entity=ps1_scene_entity(player->entity);
        uint32_t volume=(!entity || !entity->audio_enabled || entity->audio_mute || player->paused)
            ? 0u : (uint32_t)entity->audio_volume_q8*0x3fffu/255u;
        SPU_CH_VOL_L(v+1)=(uint16_t)volume;SPU_CH_VOL_R(v+1)=(uint16_t)volume;
        SPU_CH_FREQ(v+1)=player->paused?0:getSPUSampleRate(g_ps1_audio_clips[player->clip].sample_rate);
        return;
    }
    midi_voice* voice=&s_voices[v]; midi_player* p=&s_players[voice->owner];
    const ps1_entity* e=ps1_scene_entity(p->entity);
    uint32_t level=(!e || !e->audio_enabled || e->audio_mute || p->paused)?0:e->audio_volume_q8;
    level=level*voice->velocity/127u;
    level=level*p->volume[voice->channel]/127u;
    level=level*p->expression[voice->channel]/127u;
    level=level*0x1800u/255u;
    unsigned pan=p->pan[voice->channel];
    SPU_CH_VOL_L(v+1)=(uint16_t)(level*(pan>64?127-pan:63)/63u);
    SPU_CH_VOL_R(v+1)=(uint16_t)(level*(pan<64?pan:64)/64u);
    /* MIDI pitch wheel: +/- two semitones, interpolated between note pitches. */
    int offset=(int)p->bend[voice->channel]-8192;
    int semitone=offset/4096, fraction=offset%4096;
    int key=(int)voice->note+semitone;
    if(key<0)key=0; if(key>127)key=127;
    int next=key+(fraction<0?-1:1); if(next<0)next=0;if(next>127)next=127;
    if(fraction<0)fraction=-fraction;
    SPU_CH_FREQ(v+1)=(uint16_t)((int)g_ps1_midi_pitch[key]+
        ((int)g_ps1_midi_pitch[next]-(int)g_ps1_midi_pitch[key])*fraction/4096);
}
static void defaults(midi_player* p) {
    memset(p->program,0,16);memset(p->volume,127,16);memset(p->expression,127,16);
    memset(p->pan,64,16);memset(p->sustain,0,16);
    for(unsigned ch=0;ch<16;++ch)p->bend[ch]=8192;
}
static void dispatch(unsigned owner, const ps1_midi_event* e) {
    midi_player* p=&s_players[owner]; unsigned ch=e->status&15u, kind=e->status&0xf0u;
    if(kind==0xc0)p->program[ch]=e->data1;
    else if(kind==0xe0)p->bend[ch]=(uint16_t)(e->data1+(e->data2<<7));
    else if(kind==0xb0) {
        if(e->data1==7)p->volume[ch]=e->data2;
        if(e->data1==11)p->expression[ch]=e->data2;
        if(e->data1==10)p->pan[ch]=e->data2;
        if(e->data1==64)p->sustain[ch]=e->data2>=64;
        if(e->data1==121) {p->volume[ch]=127;p->expression[ch]=127;p->pan[ch]=64;p->sustain[ch]=0;p->bend[ch]=8192;}
        for(unsigned v=0;v<23;++v) if(s_voices[v].active==1 && s_voices[v].owner==owner && s_voices[v].channel==ch) {
            if(e->data1==120 || e->data1==123 || (!p->sustain[ch]&&!s_voices[v].held))release_voice(v);
            else voice_volume(v);
        }
    } else if(kind==0x80 || (kind==0x90 && !e->data2)) {
        for(unsigned v=0;v<23;++v) if(s_voices[v].active==1 && s_voices[v].held && s_voices[v].owner==owner && s_voices[v].channel==ch && s_voices[v].note==e->data1) {
            s_voices[v].held=0;if(!p->sustain[ch])release_voice(v);
        }
    } else if(kind==0x90) {
        unsigned v, oldest=0;
        for(v=0;v<23;++v) {if(!s_voices[v].active)break;if(s_voices[v].age<s_voices[oldest].age)oldest=v;}
        if(v==23)v=oldest;
        SpuSetKey(0,1u<<(v+1));
        s_voices[v]=(midi_voice){1,1,(uint8_t)owner,(uint8_t)ch,e->data1,e->data2,++s_age};
        unsigned family=ch==9?7u:p->program[ch]/16u;
        SPU_CH_ADDR(v+1)=getSPUAddr(BANK_ADDR+family*TONE_BYTES);
        SPU_CH_LOOP_ADDR(v+1)=getSPUAddr(BANK_ADDR+family*TONE_BYTES);
        SPU_CH_ADSR1(v+1)=0x00ff;
        /* RR=9 is a ~23 ms linear release at 44.1 kHz. RR=16 would
           take about three seconds, making short UI notes ring for ages. */
        SPU_CH_ADSR2(v+1)=0x0009;
        voice_volume(v);
        SpuSetKey(1,1u<<(v+1));
    }
    if(kind==0xe0)for(unsigned v=0;v<23;++v)if(s_voices[v].active==1&&s_voices[v].owner==owner&&s_voices[v].channel==ch)voice_volume(v);
}
static void due(unsigned i) {
    midi_player* p=&s_players[i];
    while(p->event<p->sequence->count && p->sequence->events[p->event].time_us<=p->time)
        dispatch(i,&p->sequence->events[p->event++]);
}
void ps1_midi_init(void) {
    memset(s_players,0,sizeof(s_players));memset(s_voices,0,sizeof(s_voices));s_age=0;s_play_serial=0;
    s_hz=GetVideoMode()?50u:60u;
    memset(s_samples,0,sizeof(s_samples));memset(s_sample_addr,0,sizeof(s_sample_addr));s_sample_play_count=0;
    uint32_t addr=BANK_ADDR;
    if(g_ps1_midi_bank_size) {
        SpuSetTransferMode(SPU_TRANSFER_BY_DMA);SpuSetTransferStartAddr(addr);
        SpuWrite((const uint32_t*)g_ps1_midi_bank,g_ps1_midi_bank_size);
        while(DMA_CHCR(DMA_SPU)&(1u<<24)) {}
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        addr+=(g_ps1_midi_bank_size+63u)&~63u;
    }
    for(unsigned i=0;i<g_ps1_audio_clip_count && i<64u;++i) {
        const ps1_audio_clip* clip=&g_ps1_audio_clips[i];
        if(!clip->sample_data || !clip->size || clip->size>0x80000u-addr)continue;
        s_sample_addr[i]=addr;
        SpuSetTransferMode(SPU_TRANSFER_BY_DMA);SpuSetTransferStartAddr(addr);
        SpuWrite((const uint32_t*)clip->sample_data,clip->size);
        while(DMA_CHCR(DMA_SPU)&(1u<<24)) {}
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        addr+=clip->size;
    }
}
int ps1_midi_play(unsigned entity, const ps1_midi_sequence* sequence) {
    if(!sequence || !sequence->count || !g_ps1_midi_bank_size)return 0;
    unsigned i, freeSlot=SEQUENCES;
    for(i=0;i<SEQUENCES;++i) {if(s_players[i].sequence && s_players[i].entity==entity)break;if(!s_players[i].sequence)freeSlot=i;}
    if(i==SEQUENCES)i=freeSlot<SEQUENCES?freeSlot:(s_play_serial++%SEQUENCES);
    release_player(i); midi_player* p=&s_players[i];memset(p,0,sizeof(*p));
    p->entity=entity;p->sequence=sequence;p->clock=(uint32_t)VSync(-1);defaults(p);due(i);
    return 1;
}
int ps1_sample_play(unsigned entity, unsigned clip) {
    const ps1_entity* source=ps1_scene_entity(entity);
    if(!source || !source->audio_enabled || clip>=g_ps1_audio_clip_count || clip>=64u || !s_sample_addr[clip])return 0;
    unsigned v,oldest=0;
    for(v=0;v<23;++v) {
        if(s_voices[v].active==2 && s_samples[v].entity==entity)break;
        if(!s_voices[v].active)oldest=v;
    }
    if(v==23) {
        for(v=0;v<23;++v)if(!s_voices[v].active)break;
        if(v==23) {oldest=0;for(v=1;v<23;++v)if(s_voices[v].age<s_voices[oldest].age)oldest=v;v=oldest;}
    }
    SpuSetKey(0,1u<<(v+1));
    s_voices[v]=(midi_voice){2,0,0,0,0,0,++s_age};
    const ps1_audio_clip* sample=&g_ps1_audio_clips[clip];
    uint32_t duration=((sample->size/16u)*28u*s_hz+sample->sample_rate-1u)/sample->sample_rate;
    s_samples[v]=(sample_player){entity,clip,(uint32_t)VSync(-1),0,duration?duration:1,0};
    SPU_CH_ADDR(v+1)=getSPUAddr(s_sample_addr[clip]);
    // A one-shot reaches the SDK's silent end block without waiting for a
    // render frame. No DMA or CD operations occur in this playback path.
    SPU_CH_LOOP_ADDR(v+1)=getSPUAddr(source->audio_loop?s_sample_addr[clip]:0x1000u);
    SPU_CH_ADSR1(v+1)=0x000f;SPU_CH_ADSR2(v+1)=0x0009;
    voice_volume(v);SpuSetKey(1,1u<<(v+1));++s_sample_play_count;
    return 1;
}
void ps1_midi_stop(unsigned entity) {
    for(unsigned v=0;v<23;++v)if(s_voices[v].active==2&&s_samples[v].entity==entity)release_voice(v);
    for(unsigned i=0;i<SEQUENCES;++i)if(s_players[i].sequence&&s_players[i].entity==entity){release_player(i);s_players[i].sequence=0;}
}
void ps1_midi_pause(unsigned entity,int paused) {
    for(unsigned v=0;v<23;++v)if(s_voices[v].active==2&&s_samples[v].entity==entity){s_samples[v].paused=paused!=0;s_samples[v].clock=(uint32_t)VSync(-1);}
    for(unsigned i=0;i<SEQUENCES;++i)if(s_players[i].sequence&&s_players[i].entity==entity){s_players[i].paused=paused!=0;s_players[i].clock=(uint32_t)VSync(-1);}
    ps1_midi_apply(entity);
}
void ps1_midi_apply(unsigned entity) {
    for(unsigned v=0;v<23;++v)if((s_voices[v].active==1&&s_players[s_voices[v].owner].entity==entity) || (s_voices[v].active==2&&s_samples[v].entity==entity))voice_volume(v);
}
int ps1_midi_is_playing(unsigned entity) {
    for(unsigned v=0;v<23;++v)if(s_voices[v].active==2&&s_samples[v].entity==entity)return !s_samples[v].paused;
    for(unsigned i=0;i<SEQUENCES;++i)if(s_players[i].sequence&&s_players[i].entity==entity)return !s_players[i].paused;
    return 0;
}
void ps1_midi_update(void) {
    uint32_t clock=(uint32_t)VSync(-1);
    for(unsigned v=0;v<23;++v) {
        if(s_voices[v].active!=2)continue;
        sample_player* player=&s_samples[v]; const ps1_entity* source=ps1_scene_entity(player->entity);
        if(!source || !source->audio_enabled) {release_voice(v);continue;}
        uint32_t elapsed=clock-player->clock;player->clock=clock;
        if(!player->paused)player->elapsed_frames+=elapsed;
        if(!source->audio_loop && player->elapsed_frames>=player->duration_frames){release_voice(v);continue;}
        if(source->audio_loop)player->elapsed_frames%=player->duration_frames;
        SPU_CH_LOOP_ADDR(v+1)=getSPUAddr(source->audio_loop?s_sample_addr[player->clip]:0x1000u);
        voice_volume(v);
    }
    for(unsigned i=0;i<SEQUENCES;++i) {
        midi_player* p=&s_players[i];if(!p->sequence)continue;
        const ps1_entity* e=ps1_scene_entity(p->entity);
        if(!e || !e->audio_enabled){ps1_midi_stop(p->entity);continue;}
        uint32_t frames=clock-p->clock;p->clock=clock;if(p->paused)continue;
        /* 10-minute clips keep arithmetic bounded; preserve fractional ticks. */
        if(frames>600u)frames=600u;
        uint32_t elapsed=frames*1000000u+p->remainder;p->time+=elapsed/s_hz;p->remainder=elapsed%s_hz;
        due(i);
        if(p->time>=p->sequence->duration_us) {
            release_player(i);
            if(e->audio_loop){p->time%=p->sequence->duration_us;p->event=0;defaults(p);due(i);}
            else p->sequence=0;
        }
        ps1_midi_apply(p->entity);
    }
}
