#include "audio.h"
#include "scene.h"

#include <psxspu.h>
#include <psxcd.h>
#include <psxapi.h>
#include <psxetc.h>
#include <hwregs_c.h>
#include <string.h>

/* Long clips are streamed through two SPU buffers instead of being copied
   wholesale at startup. This keeps startup responsive and leaves most of the
   512 KiB SPU RAM available to the game. PS1 currently exposes one streamed
   voice; starting another AudioSource replaces the previous voice. */
#define AUDIO_CHANNEL        0
#define AUDIO_BUFFER_BYTES   32768u
#define AUDIO_BUFFER0_ADDR   0x1010u
#define AUDIO_BUFFER1_ADDR   (AUDIO_BUFFER0_ADDR + AUDIO_BUFFER_BYTES)
#define AUDIO_DUMMY_ADDR     0x1000u
#define AUDIO_SPECTRUM_FRAMES 32u
#define AUDIO_SPECTRUM_BANDS  32u
#define AUDIO_ANALYSIS_SAMPLES 256u

typedef struct audio_file_info {
    uint32_t start_lba;
    uint32_t size;
    uint32_t sample_rate;
    uint8_t  found;
} audio_file_info;

static audio_file_info s_files[64];
static uint32_t s_cd_buffer[AUDIO_BUFFER_BYTES / 4u];
static int32_t s_entity = -1;
static uint32_t s_clip_index;
static uint32_t s_file_offset;
static uint32_t s_buffer_bytes[2];
static uint8_t s_buffer_final[2];
static uint8_t s_current_buffer;
static uint8_t s_paused;
static uint32_t s_frames_remaining;
static volatile uint8_t s_buffer_started;
static uint8_t s_buffer_spectrum[2][AUDIO_SPECTRUM_FRAMES][AUDIO_SPECTRUM_BANDS];
static int16_t s_analysis_samples[AUDIO_SPECTRUM_FRAMES][AUDIO_ANALYSIS_SAMPLES];
static uint16_t s_analysis_counts[AUDIO_SPECTRUM_FRAMES];
static uint32_t s_spectrum_ticks;
static uint32_t s_current_duration_frames;

static uint32_t buffer_addr(unsigned int buffer) {
    return buffer ? AUDIO_BUFFER1_ADDR : AUDIO_BUFFER0_ADDR;
}

/* The SPU tells us when it has actually started the queued buffer.  CD reads
   and frame pacing are not exact enough to infer this from VSync; doing so can
   overwrite a buffer which is still playing and produces short rewinds. */
static void audio_spu_irq(void) {
    SPU_CTRL &= ~(1u << 6);
    s_buffer_started = 1;
}

static void arm_buffer_irq(unsigned int buffer) {
    SPU_IRQ_ADDR = getSPUAddr(buffer_addr(buffer));
    SPU_CTRL |= 1u << 6;
}

static void reset_spu_channels(void) {
    SpuSetKey(0, 0x00ffffffu);
    for (unsigned int i = 0; i < 24u; ++i) {
        SPU_CH_ADDR(i) = getSPUAddr(AUDIO_DUMMY_ADDR);
        SPU_CH_FREQ(i) = 0x1000;
        SPU_CH_VOL_L(i) = 0;
        SPU_CH_VOL_R(i) = 0;
    }
    SpuSetKey(1, 0x00ffffffu);
}

static const ps1_entity* active_entity(void) {
    return s_entity < 0 ? 0 : ps1_scene_entity((unsigned int)s_entity);
}

static uint32_t buffer_duration_frames(uint32_t bytes, uint32_t sample_rate) {
    const uint32_t blocks = (bytes + 15u) / 16u;
    const uint32_t rate = sample_rate ? sample_rate : 44100u;
    const uint32_t frames = (blocks * 28u * 60u + rate - 1u) / rate;
    return (frames ? frames : 1u) + 2u;
}

static void analyze_buffer(unsigned int buffer, const uint8_t* data, uint32_t bytes) {
    static const int coef[5][2] = {
        { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 }
    };
    const uint32_t blocks = bytes / 16u;
    const uint32_t total_samples = blocks * 28u;
    int h1 = 0, h2 = 0;
    uint32_t sample_index = 0;
    unsigned int frame, i, stage, band;
    memset(s_analysis_counts, 0, sizeof(s_analysis_counts));
    memset(s_buffer_spectrum[buffer], 0, sizeof(s_buffer_spectrum[buffer]));
    if (!data || total_samples == 0)
        return;

    for (uint32_t block = 0; block < blocks; ++block) {
        const uint8_t* src = data + block * 16u;
        const int predictor = (src[0] >> 4) < 5 ? (src[0] >> 4) : 4;
        const int shift = (src[0] & 15) < 13 ? (src[0] & 15) : 12;
        for (i = 0; i < 28u; ++i, ++sample_index) {
            const uint8_t packed = src[2u + i / 2u];
            int nibble = (i & 1u) ? (packed >> 4) : (packed & 15);
            int sample;
            if (nibble >= 8) nibble -= 16;
            sample = (nibble << 12) >> shift;
            sample += (h1 * coef[predictor][0] + h2 * coef[predictor][1] + 32) >> 6;
            if (sample < -32768) sample = -32768;
            if (sample > 32767) sample = 32767;
            h2 = h1; h1 = sample;
            frame = (unsigned int)(((uint64_t)sample_index * AUDIO_SPECTRUM_FRAMES) / total_samples);
            if (frame >= AUDIO_SPECTRUM_FRAMES) frame = AUDIO_SPECTRUM_FRAMES - 1u;
            if (s_analysis_counts[frame] < AUDIO_ANALYSIS_SAMPLES)
                s_analysis_samples[frame][s_analysis_counts[frame]++] = (int16_t)sample;
        }
    }

    for (frame = 0; frame < AUDIO_SPECTRUM_FRAMES; ++frame) {
        int32_t work[AUDIO_ANALYSIS_SAMPLES];
        uint32_t energies[AUDIO_SPECTRUM_BANDS];
        uint32_t peak = 1, average = 0;
        const unsigned int count = s_analysis_counts[frame];
        memset(work, 0, sizeof(work));
        memset(energies, 0, sizeof(energies));
        for (i = 0; i < count; ++i) {
            work[i] = s_analysis_samples[frame][i] >> 3;
            average += (uint32_t)(work[i] < 0 ? -work[i] : work[i]);
        }
        for (stage = 1; stage < AUDIO_ANALYSIS_SAMPLES; stage <<= 1) {
            for (i = 0; i < AUDIO_ANALYSIS_SAMPLES; i += stage << 1) {
                for (unsigned int j = 0; j < stage; ++j) {
                    const int32_t a = work[i + j], b = work[i + j + stage];
                    work[i + j] = a + b;
                    work[i + j + stage] = a - b;
                }
            }
        }
        for (band = 0; band < AUDIO_SPECTRUM_BANDS; ++band) {
            uint32_t energy = 0;
            const unsigned int first = 1u + band * 7u;
            for (i = 0; i < 7u; ++i) {
                const int32_t value = work[first + i];
                energy += (uint32_t)(value < 0 ? -value : value);
            }
            energies[band] = energy;
            if (energy > peak) peak = energy;
        }
        average = count ? average / count : 0;
        /* work[] samples were already divided by 8. Using average / 16 again
           made ordinary music peak around 20/255, which rounded most UI bars
           to zero pixels at 240p. Normalize the spectral shape to the audible
           chunk level and leave user gain to spectrumSensitivityQ8. */
        if (average > 255u) average = 255u;
        for (band = 0; band < AUDIO_SPECTRUM_BANDS; ++band) {
            uint32_t value = (energies[band] * average) / peak;
            if (value > 255u) value = 255u;
            s_buffer_spectrum[buffer][frame][band] = (uint8_t)value;
        }
    }
}

static int read_buffer(unsigned int buffer, int loop) {
    const audio_file_info* file = &s_files[s_clip_index];
    uint32_t bytes;
    uint32_t upload_bytes;
    int sectors;
    CdlLOC position;

    if (!file->found || file->size == 0) return 0;
    if (s_file_offset >= file->size) {
        if (!loop) return 0;
        s_file_offset = 0;
    }

    bytes = file->size - s_file_offset;
    if (bytes > AUDIO_BUFFER_BYTES) bytes = AUDIO_BUFFER_BYTES;
    sectors = (int)((bytes + 2047u) / 2048u);
    memset(s_cd_buffer, 0, sizeof(s_cd_buffer));
    CdIntToPos((int)(file->start_lba + s_file_offset / 2048u), &position);
    CdControl(CdlSetloc, &position, 0);
    CdReadCallback(0);
    if (!CdRead(sectors, s_cd_buffer, CdlModeSpeed)) return 0;
    if (CdReadSync(0, 0) < 0) return 0;

    analyze_buffer(buffer, (const uint8_t*)s_cd_buffer, bytes);

    s_file_offset += bytes;
    s_buffer_bytes[buffer] = bytes;
    s_buffer_final[buffer] = (s_file_offset >= file->size && !loop) ? 1u : 0u;

    /* Every non-final chunk loops to the address selected in
       SPU_CH_LOOP_ADDR. The other buffer is selected before this chunk ends. */
    if (!s_buffer_final[buffer] && bytes >= 16u)
        ((uint8_t*)s_cd_buffer)[bytes - 15u] = 3u;

    upload_bytes = (bytes + 63u) & ~63u;
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr(buffer_addr(buffer));
    if (SpuWrite(s_cd_buffer, upload_bytes) != upload_bytes) return 0;
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    return 1;
}

static void key_on_current(void) {
    const audio_file_info* file = &s_files[s_clip_index];
    const ps1_entity* entity = active_entity();
    const int volume = (!entity || !entity->audio_enabled || entity->audio_mute)
        ? 0 : ((int)entity->audio_volume_q8 * 0x3fff) / 255;

    SpuSetKey(0, 1u << AUDIO_CHANNEL);
    SPU_CH_FREQ(AUDIO_CHANNEL) = getSPUSampleRate(file->sample_rate);
    SPU_CH_ADDR(AUDIO_CHANNEL) = getSPUAddr(buffer_addr(s_current_buffer));
    SPU_CH_LOOP_ADDR(AUDIO_CHANNEL) =
        getSPUAddr(buffer_addr(s_current_buffer ^ 1u));
    SPU_CH_VOL_L(AUDIO_CHANNEL) = (uint16_t)volume;
    SPU_CH_VOL_R(AUDIO_CHANNEL) = (uint16_t)volume;
    SPU_CH_ADSR1(AUDIO_CHANNEL) = 0x00ff;
    SPU_CH_ADSR2(AUDIO_CHANNEL) = 0x0000;
    s_buffer_started = 0;
    if (!s_buffer_final[s_current_buffer])
        arm_buffer_irq(s_current_buffer ^ 1u);
    else
        SPU_CTRL &= ~(1u << 6);
    SpuSetKey(1, 1u << AUDIO_CHANNEL);
    s_frames_remaining = buffer_duration_frames(
        s_buffer_bytes[s_current_buffer], file->sample_rate);
    s_current_duration_frames = s_frames_remaining > 2u ? s_frames_remaining - 2u : s_frames_remaining;
    s_spectrum_ticks = 0;
}

void ps1_audio_init(void) {
    SpuInit();
    CdInit();
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    reset_spu_channels();
    EnterCriticalSection();
    InterruptCallback(IRQ_SPU, &audio_spu_irq);
    ExitCriticalSection();
    memset(s_files, 0, sizeof(s_files));
    s_entity = -1;
    s_paused = 0;
    s_buffer_started = 0;
    s_spectrum_ticks = 0;

    for (unsigned int i = 0; i < g_ps1_audio_clip_count && i < 64u; ++i) {
        CdlFILE file;
        if (!g_ps1_audio_clips[i].cd_path ||
            !CdSearchFile(&file, g_ps1_audio_clips[i].cd_path))
            continue;
        s_files[i].start_lba = (uint32_t)CdPosToInt(&file.pos);
        s_files[i].size = g_ps1_audio_clips[i].size;
        s_files[i].sample_rate = g_ps1_audio_clips[i].sample_rate;
        s_files[i].found = 1;
    }
}

void ps1_audio_apply_entity(unsigned int entity_index) {
    const ps1_entity* entity;
    int volume;
    if (s_entity != (int32_t)entity_index) return;
    entity = ps1_scene_entity(entity_index);
    if (!entity) return;
    volume = (!entity->audio_enabled || entity->audio_mute)
        ? 0 : ((int)entity->audio_volume_q8 * 0x3fff) / 255;
    SPU_CH_VOL_L(AUDIO_CHANNEL) = (uint16_t)volume;
    SPU_CH_VOL_R(AUDIO_CHANNEL) = (uint16_t)volume;
}

int ps1_audio_play(unsigned int entity_index) {
    const ps1_entity* entity = ps1_scene_entity(entity_index);
    unsigned int clip_index;
    if (!entity || !entity->audio_enabled || entity->audio_clip_index == 0) return 0;
    clip_index = entity->audio_clip_index - 1u;
    if (clip_index >= g_ps1_audio_clip_count || clip_index >= 64u ||
        !s_files[clip_index].found) return 0;

    SpuSetKey(0, 1u << AUDIO_CHANNEL);
    s_entity = (int32_t)entity_index;
    s_clip_index = clip_index;
    s_file_offset = 0;
    s_current_buffer = 0;
    s_paused = 0;
    s_buffer_started = 0;
    if (!read_buffer(0, entity->audio_loop)) { s_entity = -1; return 0; }
    if (!read_buffer(1, entity->audio_loop)) {
        /* A short one-buffer clip is valid; point the loop address at itself. */
        s_buffer_bytes[1] = s_buffer_bytes[0];
        s_buffer_final[1] = 1;
    }
    key_on_current();
    return 1;
}

void ps1_audio_stop(unsigned int entity_index) {
    if (s_entity != (int32_t)entity_index) return;
    SpuSetKey(0, 1u << AUDIO_CHANNEL);
    s_entity = -1;
    s_paused = 0;
    s_frames_remaining = 0;
    s_buffer_started = 0;
    SPU_CTRL &= ~(1u << 6);
}

void ps1_audio_pause(unsigned int entity_index) {
    if (s_entity != (int32_t)entity_index) return;
    SpuSetKey(0, 1u << AUDIO_CHANNEL);
    SPU_CTRL &= ~(1u << 6);
    s_buffer_started = 0;
    s_paused = 1;
}

void ps1_audio_unpause(unsigned int entity_index) {
    if (s_entity == (int32_t)entity_index && s_paused) {
        s_paused = 0;
        key_on_current();
    }
}

int ps1_audio_is_playing(unsigned int entity_index) {
    return s_entity == (int32_t)entity_index && !s_paused;
}

void ps1_audio_update(void) {
    const ps1_entity* entity = active_entity();
    unsigned int finished;
    if (!entity || s_paused) return;
    ++s_spectrum_ticks;
    ps1_audio_apply_entity((unsigned int)s_entity);

    /* A final chunk has an end flag and no following IRQ.  The timer is only
       used to retire that final chunk, never to drive double buffering. */
    if (s_buffer_final[s_current_buffer] && !s_buffer_started) {
        if (s_frames_remaining > 0) {
            --s_frames_remaining;
            return;
        }
        ps1_audio_stop((unsigned int)s_entity);
        return;
    }
    if (!s_buffer_started)
        return;

    s_buffer_started = 0;
    finished = s_current_buffer;
    s_current_buffer ^= 1u;
    s_spectrum_ticks = 0;
    s_current_duration_frames = buffer_duration_frames(
        s_buffer_bytes[s_current_buffer], s_files[s_clip_index].sample_rate);
    if (s_current_duration_frames > 2u) s_current_duration_frames -= 2u;

    /* The newly started buffer is the last one.  Let its end flag stop the
       voice; there is no reason to refill or arm another interrupt. */
    if (s_buffer_final[s_current_buffer]) {
        s_frames_remaining = buffer_duration_frames(
            s_buffer_bytes[s_current_buffer], s_files[s_clip_index].sample_rate);
        return;
    }

    if (!read_buffer(finished, entity->audio_loop)) {
        ps1_audio_stop((unsigned int)s_entity);
        return;
    }
    SPU_CH_LOOP_ADDR(AUDIO_CHANNEL) = getSPUAddr(buffer_addr(finished));
    arm_buffer_irq(finished);
}

void ps1_audio_get_spectrum(uint8_t* bands, unsigned int count) {
    unsigned int frame;
    const ps1_entity* entity = active_entity();
    if (!bands || count == 0) return;
    if (!entity || s_paused || entity->audio_mute || !entity->audio_enabled) {
        memset(bands, 0, count);
        return;
    }
    frame = s_current_duration_frames ?
        (unsigned int)((s_spectrum_ticks * AUDIO_SPECTRUM_FRAMES) / s_current_duration_frames) : 0u;
    if (frame >= AUDIO_SPECTRUM_FRAMES) frame = AUDIO_SPECTRUM_FRAMES - 1u;
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned int source = (i * AUDIO_SPECTRUM_BANDS) / count;
        bands[i] = (uint8_t)(((unsigned int)s_buffer_spectrum[s_current_buffer][frame][source] *
                              entity->audio_volume_q8) / 255u);
    }
}

void ps1_audio_begin_scene(void) {
    const unsigned int count = ps1_scene_entity_count();
    for (unsigned int i = 0; i < count; ++i) {
        const ps1_entity* entity = ps1_scene_entity(i);
        if (entity && entity->audio_enabled && entity->audio_play_on_awake &&
            entity->audio_clip_index != 0) {
            /* Do not restart an identical clip for duplicate AudioSources. */
            const unsigned int clip = entity->audio_clip_index - 1u;
            if (s_entity >= 0 && s_clip_index == clip) continue;
            ps1_audio_play(i);
        }
    }
}
