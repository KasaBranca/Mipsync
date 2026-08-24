#include "AudioSystem.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include "../scene/Scene.h"

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace MipsyncEngine {

namespace {

#ifdef _WIN32
bool SendMci(const std::wstring& command, std::wstring* result = nullptr) {
    wchar_t buffer[256]{};
    const MCIERROR error = mciSendStringW(command.c_str(), buffer,
                                           static_cast<UINT>(std::size(buffer)), nullptr);
    if (error != 0) {
        wchar_t errorText[256]{};
        mciGetErrorStringW(error, errorText, static_cast<UINT>(std::size(errorText)));
        MIPSYNC_WARN("Audio: MCI command failed ({}): {}",
                     PathUtf8::ToString(std::filesystem::path(command)),
                     PathUtf8::ToString(std::filesystem::path(errorText)));
        return false;
    }
    if (result)
        *result = buffer;
    return true;
}
#endif

std::filesystem::path ResolveClip(const std::string& projectRoot, const std::string& clipPath) {
    std::filesystem::path path = PathUtf8::FromString(clipPath);
    if (!path.is_absolute())
        path = PathUtf8::FromString(projectRoot) / path;
    std::error_code ec;
    return std::filesystem::absolute(path, ec);
}

} // namespace

AudioSystem::~AudioSystem() {
    EndPlay();
}

void AudioSystem::BeginPlay(Scene& scene, const std::string& projectRoot) {
    EndPlay();
    m_ProjectRoot = projectRoot;
    m_Paused = false;
    for (const auto& entity : scene.GetEntities()) {
        if (!entity)
            continue;
        const auto* source = entity->GetComponent<AudioSourceComponent>();
        if (source && source->enabled && source->playOnAwake && !source->clipPath.empty())
            Play(scene, entity->GetID(), projectRoot);
    }
}

void AudioSystem::EndPlay() {
#ifdef _WIN32
    for (const auto& [id, voice] : m_Voices) {
        (void)id;
        SendMci(L"stop " + std::wstring(voice.alias.begin(), voice.alias.end()));
        SendMci(L"close " + std::wstring(voice.alias.begin(), voice.alias.end()));
    }
#endif
    m_Voices.clear();
    m_Paused = false;
}

bool AudioSystem::Play(Scene& scene, uint32_t entityId, const std::string& projectRoot) {
    Entity* entity = scene.FindEntity(entityId);
    auto* source = entity ? entity->GetComponent<AudioSourceComponent>() : nullptr;
    if (!source || !source->enabled || source->clipPath.empty())
        return false;

    const std::filesystem::path path = ResolveClip(projectRoot, source->clipPath);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        MIPSYNC_WARN("Audio clip not found: {}", PathUtf8::ToString(path));
        return false;
    }
    return OpenAndPlay(entityId, PathUtf8::ToString(path), source->volume, source->mute,
                       source->loop);
}

bool AudioSystem::OpenAndPlay(uint32_t entityId, const std::string& absolutePath,
                              float volume, bool mute, bool loop) {
    Stop(entityId);
#ifdef _WIN32
    const std::string alias = "mips_audio_" + std::to_string(entityId);
    const std::wstring aliasW(alias.begin(), alias.end());
    const std::wstring pathW = PathUtf8::FromString(absolutePath).wstring();
    if (!SendMci(L"open \"" + pathW + L"\" alias " + aliasW))
        return false;
    SendMci(L"set " + aliasW + L" time format milliseconds");

    Voice voice;
    voice.alias = alias;
    std::string decodeError;
    if (!DecodeAudioFile(absolutePath, voice.decoded, decodeError))
        MIPSYNC_WARN("Audio spectrum decode failed for {}: {}", absolutePath, decodeError);
    voice.spectrum.assign(32, 0.0f);
    auto [it, inserted] = m_Voices.emplace(entityId, std::move(voice));
    (void)inserted;
    ApplyVolume(it->second, volume, mute);
    if (!SendMci(L"play " + aliasW + (loop ? L" repeat" : L""))) {
        Stop(entityId);
        return false;
    }
    return true;
#else
    (void)absolutePath; (void)volume; (void)mute; (void)loop;
    MIPSYNC_WARN("Audio playback is not available on this desktop platform.");
    return false;
#endif
}

void AudioSystem::Stop(uint32_t entityId) {
    auto it = m_Voices.find(entityId);
    if (it == m_Voices.end())
        return;
#ifdef _WIN32
    const std::wstring alias(it->second.alias.begin(), it->second.alias.end());
    SendMci(L"stop " + alias);
    SendMci(L"close " + alias);
#endif
    m_Voices.erase(it);
}

void AudioSystem::Pause(uint32_t entityId) {
    auto it = m_Voices.find(entityId);
    if (it == m_Voices.end())
        return;
#ifdef _WIN32
    const std::wstring alias(it->second.alias.begin(), it->second.alias.end());
    SendMci(L"pause " + alias);
#endif
    it->second.paused = true;
    it->second.manuallyPaused = true;
}

void AudioSystem::Resume(uint32_t entityId) {
    auto it = m_Voices.find(entityId);
    if (it == m_Voices.end())
        return;
    it->second.manuallyPaused = false;
    if (m_Paused)
        return;
#ifdef _WIN32
    const std::wstring alias(it->second.alias.begin(), it->second.alias.end());
    SendMci(L"resume " + alias);
#endif
    it->second.paused = false;
}

void AudioSystem::ApplyVolume(Voice& voice, float volume, bool mute) {
    const int level = mute ? 0 : std::clamp(static_cast<int>(volume * 1000.0f), 0, 1000);
    if (voice.appliedVolume == level)
        return;
#ifdef _WIN32
    const std::wstring alias(voice.alias.begin(), voice.alias.end());
    SendMci(L"setaudio " + alias + L" volume to " + std::to_wstring(level));
#endif
    voice.appliedVolume = level;
}

void AudioSystem::UpdateSpectrum(Voice& voice) {
#ifdef _WIN32
    if (voice.paused || voice.decoded.sampleRate == 0 || voice.decoded.monoSamples.empty())
        return;
    std::wstring value;
    const std::wstring alias(voice.alias.begin(), voice.alias.end());
    if (!SendMci(L"status " + alias + L" position", &value))
        return;
    uint64_t positionMs = 0;
    try { positionMs = std::stoull(value); } catch (...) { return; }

    constexpr size_t windowSize = 512;
    constexpr size_t bandCount = 32;
    const auto& pcm = voice.decoded.monoSamples;
    const size_t center = static_cast<size_t>((positionMs * voice.decoded.sampleRate) / 1000u);
    const size_t start = center > windowSize / 2 ? center - windowSize / 2 : 0;
    if (voice.spectrum.size() != bandCount)
        voice.spectrum.assign(bandCount, 0.0f);

    const float nyquist = static_cast<float>(voice.decoded.sampleRate) * 0.5f;
    for (size_t band = 0; band < bandCount; ++band) {
        const float t = (static_cast<float>(band) + 0.5f) / static_cast<float>(bandCount);
        const float frequency = std::min(40.0f * std::pow(std::max(nyquist / 40.0f, 1.0f), t), nyquist * 0.96f);
        const float omega = 6.28318530718f * frequency / static_cast<float>(voice.decoded.sampleRate);
        float real = 0.0f, imag = 0.0f;
        for (size_t i = 0; i < windowSize; ++i) {
            const size_t sampleIndex = start + i;
            const float sample = sampleIndex < pcm.size() ? pcm[sampleIndex] : 0.0f;
            const float window = 0.5f - 0.5f * std::cos(6.28318530718f * static_cast<float>(i) /
                                                       static_cast<float>(windowSize - 1));
            const float phase = omega * static_cast<float>(i);
            real += sample * window * std::cos(phase);
            imag -= sample * window * std::sin(phase);
        }
        const float magnitude = std::sqrt(real * real + imag * imag) / 34.0f;
        const float normalized = std::clamp(std::sqrt(std::max(magnitude, 0.0f)), 0.0f, 1.0f);
        voice.spectrum[band] = voice.spectrum[band] * 0.72f + normalized * 0.28f;
    }
#else
    (void)voice;
#endif
}

void AudioSystem::Update(Scene& scene, bool paused) {
#ifdef _WIN32
    if (paused != m_Paused) {
        for (auto& [id, voice] : m_Voices) {
            (void)id;
            const std::wstring alias(voice.alias.begin(), voice.alias.end());
            if (paused) {
                SendMci(L"pause " + alias);
                voice.paused = true;
            } else if (!voice.manuallyPaused) {
                SendMci(L"resume " + alias);
                voice.paused = false;
            }
        }
        m_Paused = paused;
    }
#else
    (void)paused;
#endif

    std::unordered_set<uint32_t> live;
    for (const auto& entity : scene.GetEntities()) {
        if (!entity)
            continue;
        auto* source = entity->GetComponent<AudioSourceComponent>();
        if (!source)
            continue;
        live.insert(entity->GetID());
        auto voice = m_Voices.find(entity->GetID());
        if (voice != m_Voices.end()) {
            if (!source->enabled)
                Stop(entity->GetID());
            else {
                ApplyVolume(voice->second, source->volume, source->mute);
                UpdateSpectrum(voice->second);
            }
        }
    }

    for (auto it = m_Voices.begin(); it != m_Voices.end();) {
        if (live.count(it->first) != 0) {
            ++it;
            continue;
        }
#ifdef _WIN32
        const std::wstring alias(it->second.alias.begin(), it->second.alias.end());
        SendMci(L"stop " + alias);
        SendMci(L"close " + alias);
#endif
        it = m_Voices.erase(it);
    }
}

const std::vector<float>& AudioSystem::GetSpectrum(uint32_t entityId) const {
    static const std::vector<float> empty(32, 0.0f);
    if (entityId != 0) {
        const auto it = m_Voices.find(entityId);
        return it == m_Voices.end() ? empty : it->second.spectrum;
    }
    const Voice* selected = nullptr;
    uint32_t selectedId = UINT32_MAX;
    for (const auto& entry : m_Voices) {
        if (!entry.second.manuallyPaused && entry.first < selectedId) {
            selected = &entry.second;
            selectedId = entry.first;
        }
    }
    return selected ? selected->spectrum : empty;
}

bool AudioSystem::IsPlaying(uint32_t entityId) const {
    const auto it = m_Voices.find(entityId);
    if (it == m_Voices.end() || it->second.manuallyPaused)
        return false;
#ifdef _WIN32
    const std::wstring alias(it->second.alias.begin(), it->second.alias.end());
    wchar_t mode[32]{};
    if (mciSendStringW((L"status " + alias + L" mode").c_str(), mode,
                       static_cast<UINT>(std::size(mode)), nullptr) != 0)
        return false;
    return std::wstring(mode) == L"playing";
#else
    return false;
#endif
}

} // namespace MipsyncEngine
