#ifndef MIPSYNC_MIDI_H
#define MIPSYNC_MIDI_H
#include <stdint.h>
typedef struct ps1_midi_event {
    uint32_t time_us;
    uint8_t status, data1, data2;
} ps1_midi_event;
typedef struct ps1_midi_sequence {
    const ps1_midi_event* events;
    uint32_t count, duration_us;
} ps1_midi_sequence;
extern const uint8_t g_ps1_midi_bank[];
extern const uint32_t g_ps1_midi_bank_size;
extern const uint16_t g_ps1_midi_pitch[128];
void ps1_midi_init(void);
int ps1_midi_play(unsigned entity, const ps1_midi_sequence* sequence);
int ps1_sample_play(unsigned entity, unsigned clip);
void ps1_midi_stop(unsigned entity);
void ps1_midi_pause(unsigned entity, int paused);
int ps1_midi_is_playing(unsigned entity);
void ps1_midi_apply(unsigned entity);
void ps1_midi_update(void);
#endif
