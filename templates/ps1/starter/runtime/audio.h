#ifndef MIPSYNC_AUDIO_H
#define MIPSYNC_AUDIO_H

#include <stdint.h>
#include "midi.h"

typedef struct ps1_audio_clip {
    const char* cd_path;
    uint32_t size;
    uint16_t sample_rate;
    const ps1_midi_sequence* midi; /* non-null: resident SPU SFX, never CD I/O */
    const uint8_t* sample_data; /* non-null: resident ADPCM SFX */
    const char* source_path; /* project-relative path for runtime clip assignment */
} ps1_audio_clip;

extern const ps1_audio_clip g_ps1_audio_clips[];
extern const unsigned int g_ps1_audio_clip_count;

void ps1_audio_init(void);
void ps1_audio_begin_scene(void);
void ps1_audio_update(void);
int  ps1_audio_play(unsigned int entity_index);
void ps1_audio_stop(unsigned int entity_index);
void ps1_audio_pause(unsigned int entity_index);
void ps1_audio_unpause(unsigned int entity_index);
int  ps1_audio_is_playing(unsigned int entity_index);
void ps1_audio_apply_entity(unsigned int entity_index);
void ps1_audio_get_spectrum(uint8_t* bands, unsigned int count);
int ps1_audio_find_clip(const char* path); /* 1-based index, 0 empty, -1 missing */
const char* ps1_audio_clip_path(unsigned int index);

#endif
