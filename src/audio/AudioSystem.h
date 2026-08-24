#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "AudioDecoder.h"

namespace MipsyncEngine {

class Scene;

/// Small editor/player audio backend for scene AudioSource components.
/// Windows uses MCI, giving each entity an independent WAV/MP3 voice.
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
        DecodedAudio decoded;
        std::vector<float> spectrum;
    };

    bool OpenAndPlay(uint32_t entityId, const std::string& absolutePath,
                     float volume, bool mute, bool loop);
    void ApplyVolume(Voice& voice, float volume, bool mute);
    void UpdateSpectrum(Voice& voice);

    std::unordered_map<uint32_t, Voice> m_Voices;
    std::string m_ProjectRoot;
    bool m_Paused = false;
};

} // namespace MipsyncEngine
