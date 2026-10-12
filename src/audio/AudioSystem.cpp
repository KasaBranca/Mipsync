#include "../core/PreparationCancellation.h"
#include "../core/AssetFiles.h"
#include "MemoryAudioVoice.h"
#include "AudioSystem.h"
#include "MidiFile.h"
#include "../core/Log.h"
#include "../assets/AssetManager.h"
#include "../scene/Scene.h"

#include <algorithm>
#include <filesystem>
#include <cmath>
#include <unordered_set>
#include <fstream>

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
    // Decode/synthesize once before input begins, so a UI click only opens a
    // tiny cached WAV. PS1 keeps MIDI notes, rather than these preview WAVs.
    for (const auto& entity : scene.GetEntities()) {
        CheckPreparationCancelled();
        const auto* source = entity ? entity->GetComponent<AudioSourceComponent>() : nullptr;
        if (source && source->enabled && source->type != AudioSourceType::BGM && !source->clipPath.empty())
            PrepareResidentClip(PathUtf8::ToString(ResolveClip(projectRoot, source->clipPath)), source->type == AudioSourceType::SFX);
    }
    for (const auto& entity : scene.GetEntities()) {
        CheckPreparationCancelled();
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
        if (voice.memory) continue;
        (void)id;
        SendMci(L"stop " + std::wstring(voice.alias.begin(), voice.alias.end()));
        SendMci(L"close " + std::wstring(voice.alias.begin(), voice.alias.end()));
    }
#endif
    m_Voices.clear();
    m_MemoryCache.clear();
    for (const auto& [source, path] : m_MidiCache) {
        (void)source;
        std::error_code ec;
        std::filesystem::remove(PathUtf8::FromString(path), ec);
    }
    m_MidiCache.clear();
    m_Paused = false;
}

bool AudioSystem::Play(Scene& scene, uint32_t entityId, const std::string& projectRoot) {
    Entity* entity = scene.FindEntity(entityId);
    auto* source = entity ? entity->GetComponent<AudioSourceComponent>() : nullptr;
    if (!source || !source->enabled || source->clipPath.empty())
        return false;

    const std::filesystem::path path = ResolveClip(projectRoot, source->clipPath);
    std::error_code ec;
    if (!AssetFiles::Exists(path)) {
        MIPSYNC_WARN("Audio clip not found: {}", PathUtf8::ToString(path));
        return false;
    }
    if ((source->type == AudioSourceType::SFX) != IsMidiPath(source->clipPath)) {
        MIPSYNC_WARN("AudioSource type and clip format do not match: {}", source->clipPath);
        return false;
    }
    const std::string playbackPath = source->type != AudioSourceType::BGM
        ? PrepareResidentClip(PathUtf8::ToString(path), source->type == AudioSourceType::SFX) : PathUtf8::ToString(path);
    if (playbackPath.empty()) return false;
    return OpenAndPlay(entityId, playbackPath, source->volume, source->mute,
                       source->loop, source->type != AudioSourceType::BGM);
}

std::string AudioSystem::PrepareResidentClip(const std::string& absolutePath, bool isMidi) {
#ifdef _WIN32
    return PrepareMemoryClip(absolutePath) ? absolutePath : std::string{};
#endif
    if (AssetFiles::IsPackedPath(PathUtf8::FromString(absolutePath)))
        return PrepareMemoryClip(absolutePath) ? absolutePath : std::string{};
    auto found = m_MidiCache.find(absolutePath);
    if (found != m_MidiCache.end()) return found->second;
    std::string error;
    uint32_t sampleRate = 44100;
    std::vector<int16_t> pcm;
    if (isMidi) {
        MidiSequence midi;
        if (!ReadMidi(absolutePath, midi, error)) { MIPSYNC_WARN("MIDI SFX '{}': {}", absolutePath, error); return {}; }
        pcm = RenderMidi(midi);
    } else {
        DecodedAudio decoded;
        if (!DecodeAudioFile(absolutePath, decoded, error)) { MIPSYNC_WARN("Sample SFX '{}': {}", absolutePath, error); return {}; }
        sampleRate = decoded.sampleRate;
        pcm.reserve(decoded.monoSamples.size());
        for (float sample : decoded.monoSamples) pcm.push_back(static_cast<int16_t>(std::clamp(sample, -0.999969f, 0.999969f) * 32768.0f));
    }
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "MipsyncMidiPreview";
    if (ec) return {};
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};
    const auto path = dir / (std::to_string(reinterpret_cast<uintptr_t>(this)) + "_" +
                            std::to_string(std::hash<std::string>{}(absolutePath)) + ".wav");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto word = [&](uint32_t v, int bytes) { while (bytes--) { out.put(static_cast<char>(v & 255)); v >>= 8; } };
    out.write("RIFF",4); word(36+static_cast<uint32_t>(pcm.size()*2),4); out.write("WAVEfmt ",8);
    word(16,4);word(1,2);word(1,2);word(sampleRate,4);word(sampleRate*2,4);word(2,2);word(16,2);
    out.write("data",4);word(static_cast<uint32_t>(pcm.size()*2),4);
    for (int16_t sample : pcm) word(static_cast<uint16_t>(sample),2);
    out.close();
    if (!out) { MIPSYNC_WARN("Cannot cache MIDI preview: {}", absolutePath); return {}; }
    const auto result = PathUtf8::ToString(path);
    m_MidiCache.emplace(absolutePath,result);
    return result;
}

bool AudioSystem::PrepareMemoryClip(const std::string& path) {
    if (m_MemoryCache.count(path)) return true;
    DecodedAudio decoded;
    std::string error;
    if (IsMidiPath(path)) {
        MidiSequence sequence;
        if (!ReadMidi(path, sequence, error)) { MIPSYNC_WARN("Packed MIDI: {}", error); return false; }
        decoded.sampleRate=44100;
        for (int16_t sample : RenderMidi(sequence)) decoded.monoSamples.push_back(sample/32768.0f);
    } else if (!DecodeAudioFile(path, decoded, error)) { MIPSYNC_WARN("Packed audio: {}", error); return false; }
    m_MemoryCache.emplace(path,std::move(decoded));
    return true;
}

bool AudioSystem::OpenAndPlay(uint32_t entityId, const std::string& absolutePath,
                              float volume, bool mute, bool loop, bool sfx) {
    Stop(entityId);
#ifdef _WIN32
    auto extension = PathUtf8::ToString(PathUtf8::FromString(absolutePath).extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    if (sfx || extension == ".wav" || extension == ".vag" || AssetFiles::IsPackedPath(PathUtf8::FromString(absolutePath))) {
        if (!PrepareMemoryClip(absolutePath)) return false;
        Voice voice;
        voice.decoded=m_MemoryCache.at(absolutePath);
        voice.sfx=sfx;
        voice.spectrum.assign(32,0.0f);
        voice.memory=std::make_shared<MemoryAudioVoice>();
        std::vector<int16_t> pcm;
        pcm.reserve(voice.decoded.monoSamples.size());
        for(float sample:voice.decoded.monoSamples) pcm.push_back(static_cast<int16_t>(std::clamp(sample,-0.999969f,0.999969f)*32768.0f));
        const bool multichannel=!voice.decoded.playbackSamples.empty();
        if(multichannel)pcm=voice.decoded.playbackSamples;
        if(!voice.memory->Play(std::move(pcm),voice.decoded.sampleRate,loop,multichannel?voice.decoded.channels:1)) return false;
        ApplyVolume(voice,volume,mute);
        MIPSYNC_DEBUG("PCM audio playing: entity {} ({} samples)",entityId,voice.decoded.monoSamples.size());
        m_Voices.emplace(entityId,std::move(voice));
        return true;
    }
    const std::string alias = "mips_audio_" + std::to_string(entityId);
    const std::wstring aliasW(alias.begin(), alias.end());
    const std::wstring pathW = PathUtf8::FromString(absolutePath).wstring();
    if (!SendMci(L"open \"" + pathW + L"\" alias " + aliasW))
        return false;
    SendMci(L"set " + aliasW + L" time format milliseconds");

    Voice voice;
    voice.alias = alias;
    voice.sfx = sfx;
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
    (void)absolutePath; (void)volume; (void)mute; (void)loop; (void)sfx;
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
    if(!it->second.memory) { SendMci(L"stop " + alias); SendMci(L"close " + alias); }
#endif
    m_Voices.erase(it);
}

void AudioSystem::Pause(uint32_t entityId) {
    auto it = m_Voices.find(entityId);
    if (it == m_Voices.end())
        return;
#ifdef _WIN32
    const std::wstring alias(it->second.alias.begin(), it->second.alias.end());
    if(it->second.memory) it->second.memory->Pause(true); else SendMci(L"pause " + alias);
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
    if(it->second.memory) it->second.memory->Pause(false); else SendMci(L"resume " + alias);
#endif
    it->second.paused = false;
}

void AudioSystem::ApplyVolume(Voice& voice, float volume, bool mute) {
    const int level = mute ? 0 : std::clamp(static_cast<int>(volume * 1000.0f), 0, 1000);
    if (voice.appliedVolume == level)
        return;
#ifdef _WIN32
    const std::wstring alias(voice.alias.begin(), voice.alias.end());
    if(voice.memory) voice.memory->SetVolume(level); else SendMci(L"setaudio " + alias + L" volume to " + std::to_wstring(level));
#endif
    voice.appliedVolume = level;
}

void AudioSystem::UpdateSpectrum(Voice& voice) {
#ifdef _WIN32
    if (voice.paused || voice.decoded.sampleRate == 0 || voice.decoded.monoSamples.empty())
        return;
    uint64_t positionMs = 0;
    if(voice.memory) positionMs=voice.memory->PositionMs();
    else {
        std::wstring value;
        const std::wstring alias(voice.alias.begin(),voice.alias.end());
        if(!SendMci(L"status " + alias + L" position",&value)) return;
        try { positionMs=std::stoull(value); } catch(...) { return; }
    }

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
                if(voice.memory) voice.memory->Pause(true); else SendMci(L"pause " + alias);
                voice.paused = true;
            } else if (!voice.manuallyPaused) {
                if(voice.memory) voice.memory->Pause(false); else SendMci(L"resume " + alias);
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
        if(!it->second.memory) { SendMci(L"stop " + alias); SendMci(L"close " + alias); }
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
        if (!entry.second.sfx && !entry.second.manuallyPaused && entry.first < selectedId) {
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
    if(it->second.memory) return it->second.memory->IsPlaying();
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
