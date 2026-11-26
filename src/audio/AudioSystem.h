#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include "AudioDecoder.h"

namespace MipsyncEngine {

class Scene;
class MemoryAudioVoice;

/// Small editor/player audio backend for scene AudioSource components.
/// Loose editor clips use MCI; protected players submit PCM buffers in memory.
///
/// Output devices belong to the thread that constructed the system (the main
/// thread). MCI aliases are only visible to the thread that opened them, so a
/// BGM opened by the Play-preparation worker could never be stopped again.
/// Calls from other threads still decode/prepare their clips there, but every
/// device operation is queued and performed by the owner thread in Update().
class AudioSystem {
public:
    AudioSystem() : m_OwnerThread(std::this_thread::get_id()) {}
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

    struct PendingOp {
        enum class Kind { Start, Stop, Pause, Resume } kind = Kind::Start;
        uint32_t entityId = 0;
        std::string path;
        float volume = 1.0f;
        bool mute = false;
        bool loop = false;
        bool sfx = false;
    };

    bool OnOwnerThread() const { return std::this_thread::get_id() == m_OwnerThread; }
    void Defer(PendingOp op);
    bool HasPendingStart(uint32_t entityId) const;
    void FlushPending();

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
    std::thread::id m_OwnerThread;
    mutable std::mutex m_PendingMutex;
    std::vector<PendingOp> m_Pending;
};

} // namespace MipsyncEngine
