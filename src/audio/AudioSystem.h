#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "AudioDecoder.h"

namespace MipsyncEngine {

class Scene;
class MemoryAudioVoice;

/// Small editor/player audio backend for scene AudioSource components.
/// Loose editor clips use MCI; protected players submit PCM buffers in memory.
class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    void BeginPlay(Scene& scene, const std::string& projectRoot);
    void EndPlay();
    void Update(Scene& scene, bool paused);

    bool Play(Scene& scene, uint32_t entityId, const std::string& projectRoot);
    void Stop(uint32_t entityId);
    void Pause(uint32_t entityId);
    void Resume(uint32_t entityId);
    bool IsPlaying(uint32_t entityId) const;
    const std::vector<float>& GetSpectrum(uint32_t entityId = 0) const;

private:
    struct Voice {
        std::string alias;
        int appliedVolume = -1;
        bool paused = false;
        bool manuallyPaused = false;
        bool sfx = false;
        DecodedAudio decoded;
        std::vector<float> spectrum;
        std::shared_ptr<MemoryAudioVoice> memory;
    };

    bool OpenAndPlay(uint32_t entityId, const std::string& absolutePath,
                     float volume, bool mute, bool loop, bool sfx);
    void ApplyVolume(Voice& voice, float volume, bool mute);
    void UpdateSpectrum(Voice& voice);
    std::string PrepareResidentClip(const std::string& absolutePath, bool midi);
    bool PrepareMemoryClip(const std::string& absolutePath);

    std::unordered_map<uint32_t, Voice> m_Voices;
    std::string m_ProjectRoot;
    std::unordered_map<std::string, std::string> m_MidiCache;
    std::unordered_map<std::string, DecodedAudio> m_MemoryCache;
    bool m_Paused = false;
};

} // namespace MipsyncEngine
